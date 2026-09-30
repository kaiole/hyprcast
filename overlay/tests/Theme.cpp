#include "theme/Theme.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QGuiApplication>
#include <QQuickView>
#include <QTemporaryDir>
#include <QUrl>
#include <QtMath>

#include <xkbcommon/xkbcommon.h>

#include <cstdlib>
#include <iostream>

using namespace Hyprcast::Overlay;

namespace {
    int  failures = 0;

    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    QString testKeymap() {
        xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_rule_names names{};
        names.rules = "evdev";
        names.model = "pc105";
        names.layout = "us";
        xkb_keymap* map = xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        char* text = xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1);
        const QString result = QString::fromUtf8(text);
        std::free(text);
        xkb_keymap_unref(map);
        xkb_context_unref(context);
        return result;
    }

    bool hasVisualText(QQuickItem* item, const QString& text) {
        if (item->property("text").toString() == text) {
            return true;
        }
        for (auto* child : item->childItems()) {
            if (hasVisualText(child, text)) {
                return true;
            }
        }
        return false;
    }

    QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems())
            if (auto* result = findVisualItem(child, name)) return result;
        return nullptr;
    }

    void polishVisualTree(QQuickItem* item) {
        for (auto* child : item->childItems()) polishVisualTree(child);
        item->ensurePolished();
    }

    bool writeFile(const QString& path, const QByteArray& contents) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    }

    QString themeDirectory(const QString& root, const QString& id, const QByteArray& manifest, const QByteArray& qml) {
        const QString directory = QDir(root).filePath(id);
        QDir().mkpath(directory);
        writeFile(QDir(directory).filePath(QStringLiteral("theme.toml")), manifest);
        if (!qml.isEmpty()) {
            writeFile(QDir(directory).filePath(QStringLiteral("Main.qml")), qml);
        }
        return directory;
    }

    QByteArray manifest(const QByteArray& id, const QByteArray& extra = {}) {
        return QByteArrayLiteral("[theme]\nid = '") + id + QByteArrayLiteral("'\nname = 'Test theme'\napi_version = 1\nentry = 'Main.qml'\n") + extra;
    }

    QByteArray validQml(const QByteArray& name = QByteArrayLiteral("theme")) {
        return QByteArrayLiteral("import QtQuick\nItem { anchors.fill: parent; objectName: '") + name + QByteArrayLiteral("-' + hyprcast.themeId }\n");
    }

    QByteArray optionQml() {
        return QByteArrayLiteral("import QtQuick\nItem { anchors.fill: parent; objectName: 'gap-' + hyprcast.options.gap }\n");
    }

    void testDiscoveryAndPrecedence(const QString& userRoot, const QString& systemRoot, const QString& bundledRoot) {
        const QString userDefault  = themeDirectory(userRoot, QStringLiteral("default"), manifest(QByteArrayLiteral("default")), validQml());
        const QString userShared   = themeDirectory(userRoot, QStringLiteral("shared"), manifest(QByteArrayLiteral("shared")), validQml());
        const QString systemShared = themeDirectory(systemRoot, QStringLiteral("shared"), manifest(QByteArrayLiteral("shared")), validQml());
        const QString systemOnly   = themeDirectory(systemRoot, QStringLiteral("system-only"), manifest(QByteArrayLiteral("system-only")), validQml());
        themeDirectory(userRoot, QStringLiteral("future"), QByteArrayLiteral("[theme]\nid='future'\napi_version=99\nentry='Main.qml'\n"), validQml());
        themeDirectory(systemRoot, QStringLiteral("future"), manifest(QByteArrayLiteral("future")), validQml());
        themeDirectory(userRoot, QStringLiteral("missing"), manifest(QByteArrayLiteral("missing")), {});
        themeDirectory(userRoot, QStringLiteral("badmanifest"), QByteArrayLiteral("[theme]\nid='badmanifest'\napi_version=1\nentry='Main.qml'\nunexpected=true\n"), validQml());
        themeDirectory(userRoot, QStringLiteral("badpath"), QByteArrayLiteral("[theme]\nid='badpath'\napi_version=1\nentry='../outside.qml'\n"), validQml());
        const QString builtinDirectory = themeDirectory(bundledRoot, QStringLiteral("default"), manifest(QByteArrayLiteral("default")), validQml());

        ThemeCatalog  catalog({userRoot, systemRoot}, QUrl::fromLocalFile(QDir(builtinDirectory).filePath(QStringLiteral("theme.toml"))));
        QStringList   warnings;
        QString       error;
        check(catalog.discover(&warnings, &error), "discover valid user, system, and bundled themes");
        check(error.isEmpty(), "successful discovery has no error");

        ThemeDescriptor descriptor;
        check(catalog.resolve(QStringLiteral("shared"), &descriptor, &error), "resolve duplicate theme id");
        check(descriptor.entryUrl.toLocalFile().startsWith(userShared), "user theme deterministically shadows system theme");
        check(catalog.resolve(QStringLiteral("system-only"), &descriptor, &error) && descriptor.entryUrl.toLocalFile().startsWith(systemOnly),
              "system themes are available when there is no user override");
        check(catalog.resolve(QStringLiteral("default"), &descriptor, &error) && descriptor.entryUrl.toLocalFile().startsWith(userDefault),
              "user theme may shadow the bundled theme's ordinary id");
        check(catalog.resolve(QStringLiteral("builtin:default"), &descriptor, &error) && descriptor.bundled && descriptor.entryUrl.toLocalFile().startsWith(builtinDirectory),
              "the explicit bundled alias remains accessible when its ordinary id is shadowed");
        check(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("shadowed")), "duplicate installation produces a precedence warning");
        check(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("builtin:default")), "shadowing the default name explains the protected built-in alias");
        check(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("API version 99")), "incompatible manifests are diagnosed and ignored");
        check(!catalog.resolve(QStringLiteral("future"), &descriptor, &error) && error.contains(QStringLiteral("API version 99")),
              "selecting an incompatible theme reports the compatibility failure directly");
        check(warnings.join(QLatin1Char('\n')).contains(QStringLiteral("missing")), "missing entry points are diagnosed and ignored");
        check(!catalog.resolve(QStringLiteral("badmanifest"), &descriptor, &error) && error.contains(QStringLiteral("unknown theme manifest key")),
              "strict manifest validation rejects unknown fields");
        check(catalog.availableThemeIds().contains(QStringLiteral("builtin:default")), "available themes include the protected bundled alias");
        Q_UNUSED(systemShared);
    }

    void testOptions(const QString& userRoot, const QString& bundledRoot) {
        const QByteArray optionDeclarations = QByteArrayLiteral("[options.gap]\ntype='integer'\ndefault=5\nminimum=0\nmaximum=20\ndescription='Space between items.'\n"
                                                                "[options.mode]\ntype='enum'\ndefault='rows'\nvalues=['rows','compact']\n"
                                                                "[options.tint]\ntype='color'\ndefault='#ffffff'\n");
        themeDirectory(userRoot, QStringLiteral("options"), manifest(QByteArrayLiteral("options"), optionDeclarations), validQml());
        const QString bundledDirectory = themeDirectory(bundledRoot, QStringLiteral("default"), manifest(QByteArrayLiteral("default")), validQml());
        ThemeCatalog  catalog({userRoot}, QUrl::fromLocalFile(QDir(bundledDirectory).filePath(QStringLiteral("theme.toml"))));
        QString       error;
        check(catalog.discover(nullptr, &error), "discover a theme with declared options");

        QVariantMap effective;
        check(catalog.validateOptions(QStringLiteral("options"), {}, &effective, &error), "apply declared defaults for omitted theme options");
        check(effective.value(QStringLiteral("gap")).toInt() == 5 && effective.value(QStringLiteral("mode")).toString() == QStringLiteral("rows"),
              "theme option defaults are exposed to QML");

        QVariantMap valid{{QStringLiteral("gap"), 12}, {QStringLiteral("mode"), QStringLiteral("compact")}, {QStringLiteral("tint"), QStringLiteral("red")}};
        check(catalog.validateOptions(QStringLiteral("options"), valid, &effective, &error), "validate well-typed theme option values");
        check(effective.value(QStringLiteral("gap")).toInt() == 12 && effective.value(QStringLiteral("mode")).toString() == QStringLiteral("compact"),
              "accepted option overrides are reflected in the effective map");

        check(!catalog.validateOptions(QStringLiteral("options"), {{QStringLiteral("unknown"), 1}}, &effective, &error) && error.contains(QStringLiteral("unknown option")),
              "reject undeclared theme options");
        check(!catalog.validateOptions(QStringLiteral("options"), {{QStringLiteral("gap"), QStringLiteral("12")}}, &effective, &error), "reject mistyped theme options");
        check(!catalog.validateOptions(QStringLiteral("options"), {{QStringLiteral("gap"), 21}}, &effective, &error) && error.contains(QStringLiteral("at most 20")),
              "enforce declared numeric ranges");
        check(!catalog.validateOptions(QStringLiteral("options"), {{QStringLiteral("mode"), QStringLiteral("wide")}}, &effective, &error) &&
                  error.contains(QStringLiteral("rows, compact")),
              "enforce declared enum values");
    }

    void testRuntimeLoadingAndSwitchRecovery(const QString& userRoot) {
        const QByteArray gapDeclaration = QByteArrayLiteral("[options.gap]\ntype='integer'\ndefault=5\nminimum=0\nmaximum=20\n");
        const QString    goodDirectory  = themeDirectory(userRoot, QStringLiteral("good"), manifest(QByteArrayLiteral("good"), gapDeclaration), optionQml());
        Q_UNUSED(goodDirectory);
        themeDirectory(userRoot, QStringLiteral("badroot"), manifest(QByteArrayLiteral("badroot")), QByteArrayLiteral("import QtQml\nQtObject {}\n"));
        themeDirectory(userRoot, QStringLiteral("syntax"), manifest(QByteArrayLiteral("syntax")), QByteArrayLiteral("import QtQuick\nItem { broken\n"));
        const QString bundledManifest = QDir(QStringLiteral(HYPRCAST_OVERLAY_SOURCE_DIR)).filePath(QStringLiteral("qml/themes/default/theme.toml"));
        const QString sampleThemes    = QDir(QStringLiteral(HYPRCAST_OVERLAY_SOURCE_DIR)).filePath(QStringLiteral("examples/themes"));
        const QString heldSource = QDir(sampleThemes).filePath(QStringLiteral("text-held"));
        const QString heldCopy = QDir(userRoot).filePath(QStringLiteral("text-held"));
        check(QDir().mkpath(heldCopy), "create isolated held-key example package");
        for (const QString& file : {QStringLiteral("theme.toml"), QStringLiteral("Main.qml"), QStringLiteral("Keycap.qml"), QStringLiteral("RibbonGroup.qml"), QStringLiteral("RibbonStage.qml")}) {
            check(QFile::copy(QDir(heldSource).filePath(file), QDir(heldCopy).filePath(file)), "copy standalone text-held package");
        }
        const QString capSource = QDir(sampleThemes).filePath(QStringLiteral("keycaps"));
        const QString capCopy = QDir(userRoot).filePath(QStringLiteral("keycaps"));
        check(QDir().mkpath(capCopy), "create isolated user theme package");
        for (const QString& file : {QStringLiteral("theme.toml"), QStringLiteral("Main.qml"), QStringLiteral("KeycapEntry.qml"), QStringLiteral("KeycapPresentation.qml")}) {
            check(QFile::copy(QDir(capSource).filePath(file), QDir(capCopy).filePath(file)), "copy self-contained keycaps package");
        }
        ThemeCatalog  catalog({userRoot, sampleThemes}, QUrl::fromLocalFile(bundledManifest));
        QString       error;
        check(catalog.discover(nullptr, &error), "discover themes before QML loading test");

        QTemporaryDir hostDirectory;
        const QString hostPath = hostDirectory.filePath(QStringLiteral("Host.qml"));
        check(writeFile(hostPath, QByteArrayLiteral("import QtQuick\nItem { width: 600; height: 120 }\n")), "write QML host fixture");
        QQuickView view;
        view.resize(600, 120);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(hostPath));
        check(view.status() == QQuickView::Ready && view.rootObject(), "load a simple offscreen QML host");

        KeyboardPresenter presenter;
        ThemeRuntime      runtime(catalog, view, presenter);
        OverlayConfig     config;
        config.width             = 600;
        config.height            = 120;
        config.dynamicSize       = true;
        config.minWidth          = 160;
        config.minHeight         = 50;
        config.panelVisibility   = QStringLiteral("with-content");
        config.backgroundOpacity = 0.0;
        config.panelBorderWidth  = 2;
        config.panelBorderColor  = QStringLiteral("#80ffffff");
        check(runtime.loadInitial(config, &error), "load the bundled default as a real QML component");
        check(runtime.activeThemeId() == QStringLiteral("builtin:default"), "bundled default is active initially");
        view.show();
        QCoreApplication::processEvents();
        auto* panelFrame  = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastPanelFrame"), Qt::FindChildrenRecursively);
        auto* panelFill   = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastPanelBackground"), Qt::FindChildrenRecursively);
        auto* panelBorder = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastPanelBorder"), Qt::FindChildrenRecursively);
        check(panelFrame && panelFill && panelBorder, "bundled theme exposes its panel frame, fill, and border to the offscreen scene");
        if (panelFrame && panelFill && panelBorder) {
            const qreal emptyWidth = panelFrame->width();
            check(emptyWidth >= config.minWidth && emptyWidth < config.width && !panelFill->isVisible(),
                  "dynamic empty panel starts at its minimum and with-content hides only its decoration");
            InterpretedAction longText;
            longText.kind = InterpretedActionKind::Text;
            longText.text = QString(80, QLatin1Char('a'));
            presenter.historyModel().apply(longText);
            QCoreApplication::processEvents();
            auto* tailText = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastTailText"), Qt::FindChildrenRecursively);
            check(tailText && tailText->x() < 0 && tailText->x() + tailText->width() <= tailText->parentItem()->width() + 1,
                  "overflowing rich text follows newest characters rather than clipping the tail");
            const qreal  fullWidth = panelFrame->width();
            const QColor fillColor = panelFill->property("color").value<QColor>();
            check(qFuzzyCompare(fullWidth, static_cast<qreal>(config.width)) && panelFill->isVisible() && panelBorder->isVisible() && fillColor.alphaF() == 0.0,
                  "measured history grows to the maximum while a visible border remains independent of transparent fill opacity");
            InterpretedAction backspace;
            backspace.kind = InterpretedActionKind::Key;
            backspace.key  = QStringLiteral("Backspace");
            for (int i = 0; i < 60; ++i) {
                presenter.historyModel().apply(backspace);
            }
            QCoreApplication::processEvents();
            check(panelFrame->width() < fullWidth, "deleting content shrinks the dynamic panel and recovers viewport space");
            check(tailText && tailText->x() == 0, "shortened text returns to the start of the viewport");
            config.panelVisibility = QStringLiteral("never");
            runtime.applyAcceptedConfiguration(config);
            QCoreApplication::processEvents();
            check(!panelFill->isVisible() && presenter.historyModel().rowCount() == 1, "never suppresses the outer fill while preserving visible semantic input");
            presenter.historyModel().clear();
            config.panelVisibility = QStringLiteral("with-content");
            runtime.applyAcceptedConfiguration(config);
            QCoreApplication::processEvents();
            check(!panelFill->isVisible(), "with-content hides the panel decoration after history is cleared");

            presenter.processMessage(KeyboardSnapshotMessage{.id = 1, .name = QStringLiteral("test"), .keymap = testKeymap()});
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 1, .keycode = 42, .pressed = true});
            QCoreApplication::processEvents();
            check(presenter.heldKeyCount() == 1 && !panelFill->isVisible() && panelFrame->width() == emptyWidth &&
                      !hasVisualText(view.rootObject(), QStringLiteral("Shift")),
                  "default ignores held-only state for visibility, geometry, and content");
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 2, .keycode = 42, .pressed = false});
            presenter.historyModel().clear();
            QCoreApplication::processEvents();

            config.themeId              = QStringLiteral("keycaps");
            config.height               = 60;
            config.themeOptions         = {{QStringLiteral("height"), 44}, {QStringLiteral("border_width"), 0}};
            check(runtime.prepareSwitch(config, &error), "load keycaps through ordinary theme discovery");
            config.repeatPresentation   = QStringLiteral("counted");
            config.repeatCountThreshold = 2;
            config.symbolFontFamily     = QStringLiteral("Symbols Nerd Font");
            config.keySymbols           = {{QStringLiteral("C"), QStringLiteral("COPY")}};
            config.modifierSymbols      = {{QStringLiteral("Ctrl"), QStringLiteral("CTRL")}};
            InputHistoryOptions historyOptions;
            historyOptions.presentation.countedRepeats   = true;
            historyOptions.presentation.repeatThreshold  = 2;
            historyOptions.presentation.symbolFontFamily = config.symbolFontFamily;
            historyOptions.presentation.spaceSymbol      = QStringLiteral("␣");
            historyOptions.presentation.keySymbols       = config.keySymbols;
            historyOptions.presentation.modifierSymbols  = config.modifierSymbols;
            presenter.setHistoryOptions(historyOptions);
            runtime.applyAcceptedConfiguration(config);
            auto* capPanel = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastKeycapsPanelFrame"), Qt::FindChildrenRecursively);
            check(capPanel && capPanel->height() >= config.minHeight, "keycaps controls its own visible panel");
            QVariantMap capOptions;
            check(catalog.validateOptions(QStringLiteral("keycaps"), {{QStringLiteral("height"), 45}}, &capOptions, &error) && capOptions.value(QStringLiteral("height")).toInt() == 45,
                  "keycaps validates declared height option");
            check(!catalog.validateOptions(QStringLiteral("keycaps"), {{QStringLiteral("height"), -1}}, &capOptions, &error), "keycaps rejects invalid height option");
            check(!catalog.validateOptions(QStringLiteral("keycaps"), {{QStringLiteral("edge_depth"), -1}}, &capOptions, &error), "keycaps rejects negative edge depth");
            check(!catalog.validateOptions(QStringLiteral("keycaps"), {{QStringLiteral("edge_depth"), 25}}, &capOptions, &error), "keycaps rejects excessive edge depth");
            InterpretedAction space;
            space.kind = InterpretedActionKind::Text;
            space.text = QStringLiteral(" ");
            presenter.historyModel().apply(space);
            QCoreApplication::processEvents();
            check(hasVisualText(view.rootObject(), QStringLiteral("␣")) && presenter.historyModel().displayText() == QStringLiteral(" "),
                  "keycaps displays the mapped space while retaining a literal input space");
            presenter.historyModel().clear();

            InterpretedAction chord;
            chord.kind       = InterpretedActionKind::Chord;
            chord.keyboardId = 1;
            chord.keycode    = 46;
            chord.key        = QStringLiteral("C");
            chord.modifiers  = {QStringLiteral("Ctrl")};
            presenter.historyModel().apply(chord);
            chord.repeated    = true;
            chord.repeatCount = 1;
            presenter.historyModel().apply(chord);
            QCoreApplication::processEvents();
            check(presenter.historyModel().presentationModel().displayText() == QStringLiteral("CTRL+COPY…2x ") && capPanel &&
                      hasVisualText(view.rootObject(), QStringLiteral("CTRL")) && hasVisualText(view.rootObject(), QStringLiteral("COPY")) &&
                      hasVisualText(view.rootObject(), QStringLiteral("…2x")),
                  "copied keycaps QML renders resolved chords and counted-repeat badges");
            auto checkCapGeometry = [&](qreal expectedHeight, qreal expectedDepth) {
                // Positioners polish asynchronously; flush nested rows explicitly
                // instead of relying on a render frame or an incidental event delay.
                polishVisualTree(view.rootObject());
                polishVisualTree(view.rootObject());
                QList<QQuickItem*> caps;
                QList<QQuickItem*> pending{view.rootObject()};
                while (!pending.isEmpty()) {
                    auto* item = pending.takeLast();
                    if (item->objectName() == QStringLiteral("hyprcastKeycap")) caps.append(item);
                    pending.append(item->childItems());
                }
                check(caps.size() == 2, "counted chord retains separate modifier and key caps");
                for (auto* cap : caps) {
                    auto* face = cap->findChild<QQuickItem*>(QStringLiteral("hyprcastKeycapFace"));
                    if (capPanel) {
                        const auto bounds = cap->mapRectToItem(capPanel, QRectF(0, 0, cap->width(), cap->height()));
                        check(bounds.top() >= 0 && bounds.bottom() <= capPanel->height() && capPanel->height() <= 60,
                              "raised caps fit vertically inside a tight 60-pixel panel");
                    }
                    check(face && qFuzzyCompare(cap->height(), expectedHeight) &&
                              qFuzzyCompare(face->height(), expectedHeight - expectedDepth) &&
                              face->y() == 0 && face->width() == cap->width(),
                          "raised cap face and lower edge stay inside declared geometry");
                    if (face) {
                        auto* border = face->property("border").value<QObject*>();
                        check(face->property("radius").toReal() <= face->height() / 2 && border &&
                                  border->property("width").toReal() <= face->height() / 2,
                              "cap radius and border are bounded by face geometry");
                    }
                }
            };
            checkCapGeometry(44, 3);
            const QString keycapsCapture = qEnvironmentVariable("HYPRCAST_KEYCAPS_SCREENSHOT");
            if (!keycapsCapture.isEmpty()) {
                QEventLoop captureLoop;
                QTimer::singleShot(100, &captureLoop, &QEventLoop::quit);
                captureLoop.exec();
                check(view.grabWindow().save(keycapsCapture), "save optional keycaps rendering artifact");
            }
            config.themeOptions.insert(QStringLiteral("edge_depth"), 0);
            runtime.applyAcceptedConfiguration(config);
            QCoreApplication::processEvents();
            checkCapGeometry(44, 0);
            config.themeOptions = {{QStringLiteral("height"), 1}, {QStringLiteral("edge_depth"), 24},
                                   {QStringLiteral("radius"), 256}, {QStringLiteral("border_width"), 64}};
            runtime.applyAcceptedConfiguration(config);
            QCoreApplication::processEvents();
            checkCapGeometry(1, 0.5);
            config.themeOptions = {{QStringLiteral("height"), 44}, {QStringLiteral("border_width"), 0}};
            runtime.applyAcceptedConfiguration(config);
            QCoreApplication::processEvents();
            checkCapGeometry(44, 3);
            presenter.historyModel().clear();
            config.minWidth = config.width;
            runtime.applyAcceptedConfiguration(config);

            // Reproduce live symbol reload, then append fresh actions. Inspect actual
            // QML text items as well as the model: a correct projection alone is insufficient.
            historyOptions.backspaceMode = BackspaceMode::Symbol;
            historyOptions.presentation.countedRepeats = false;
            historyOptions.presentation.keySymbols = {{QStringLiteral("Backspace"), QStringLiteral("ERASE")},
                                                       {QStringLiteral("Enter"), QStringLiteral("RETURN")}};
            presenter.setHistoryOptions(historyOptions);
            InterpretedAction special;
            special.kind = InterpretedActionKind::Key;
            special.key = QStringLiteral("Backspace");
            presenter.historyModel().apply(special);
            QCoreApplication::processEvents();
            check(hasVisualText(view.rootObject(), QStringLiteral("ERASE")), "keycap renders the initial symbol mapping");

            historyOptions.presentation.keySymbols = {{QStringLiteral("Backspace"), QStringLiteral("⌫")},
                                                       {QStringLiteral("Enter"), QStringLiteral("↵")}};
            historyOptions.presentation.modifierSymbols = {{QStringLiteral("Ctrl"), QStringLiteral("⌃")},
                                                            {QStringLiteral("Shift"), QStringLiteral("⇧")}};
            presenter.setHistoryOptions(historyOptions);
            QCoreApplication::processEvents();
            check(hasVisualText(view.rootObject(), QStringLiteral("⌫")), "symbol reload updates existing keycaps");
            special.key = QStringLiteral("Enter");
            presenter.historyModel().apply(special);
            presenter.historyModel().apply(chord);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            check(hasVisualText(view.rootObject(), QStringLiteral("↵")), "new special-key keycaps use the reloaded symbol mapping");
            check(hasVisualText(view.rootObject(), QStringLiteral("⌃")), "new chord keycaps use the reloaded modifier mapping");
            special.key = QStringLiteral("Shift");
            presenter.historyModel().apply(special);
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            check(hasVisualText(view.rootObject(), QStringLiteral("⇧")), "new standalone modifier keycaps use the reloaded modifier mapping");
            check(presenter.historyModel().presentationModel().displayText().endsWith(QStringLiteral("⇧ ")) &&
                      presenter.historyModel().entries().back().action.key == QStringLiteral("Shift"),
                  "standalone modifier projection resolves glyphs without changing canonical identity");
            const auto expirationStart = KeyboardPresenter::Clock::now();
            presenter.setExpiration(1, 250, expirationStart);
            presenter.advance(expirationStart + std::chrono::milliseconds(2));
            QCoreApplication::processEvents();
            check(presenter.fading() && presenter.historyModel().rowCount() == 0 && hasVisualText(view.rootObject(), QStringLiteral("⌫")),
                  "keycaps renders the fading snapshot after editable history expires");
            presenter.setExpiration(0, 0);
            presenter.clearHistory();
            historyOptions.backspaceMode = BackspaceMode::Delete;
            presenter.setHistoryOptions(historyOptions);
        }

        // Cascade uses a virtualized model view but repositions its visual groups
        // independently. Assert final geometry with motion disabled, not timers.
        config.themeId = QStringLiteral("cascade");
        config.width = 420;
        config.height = 320;
        config.dynamicSize = false;
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("reduced")}};
        view.resize(420, 320);
        check(runtime.prepareSwitch(config, &error), "load Cascade through the normal catalog");
        runtime.applyAcceptedConfiguration(config);
        auto settle = [&]() {
            for (int i = 0; i < 5; ++i) {
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                for (const auto& name : {QStringLiteral("cascadeModelView"), QStringLiteral("ribbonModelView")})
                    for (auto* modelView : view.rootObject()->findChildren<QQuickItem*>(name))
                        QMetaObject::invokeMethod(modelView, "forceLayout");
            }
        };
        settle();
        auto* cascade = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("cascadeActiveStage"));
        check(cascade && cascade->property("capacity").toInt() == 6, "Cascade fits six groups in the demo surface");
        InterpretedAction cascadeText;
        cascadeText.kind = InterpretedActionKind::Text;
        cascadeText.text = QStringLiteral("<b>🙂</b>");
        presenter.historyModel().apply(cascadeText);
        InterpretedAction cascadeChord;
        cascadeChord.kind = InterpretedActionKind::Chord;
        cascadeChord.key = QStringLiteral("C");
        cascadeChord.modifiers = {QStringLiteral("Ctrl")};
        presenter.historyModel().apply(cascadeChord);
        settle();
        check(hasVisualText(view.rootObject(), cascadeText.text) && hasVisualText(view.rootObject(), QStringLiteral("C")),
              "Cascade preserves Unicode text and separate chord labels");
        if (cascade) {
            int shown = 0;
            for (auto* child : cascade->childItems()) {
                if (child->objectName() != QStringLiteral("cascadeGroup") || !child->property("inStack").toBool()) continue;
                ++shown;
                check(child->y() >= 0 && child->y() + child->height() <= cascade->height(), "Cascade group stays within stage bounds");
                const QRectF visual = child->mapRectToItem(cascade, QRectF(0, 0, child->width(), child->height()));
                check(visual.left() >= -0.5 && visual.right() <= cascade->width() + 0.5,
                      "Cascade transformed chord fits the available width");
                if (child->property("depth").toInt() == 0)
                    check(qFuzzyCompare(child->y() + child->height(), cascade->height()), "Cascade newest group is bottom anchored");
            }
            check(shown == 2, "Cascade displays one group per projected action");
        }
        // Optional software-rendered inspection artifact; ordinary assertions
        // above do not wait for animation or depend on a screenshot.
        const QString cascadeCapture = qEnvironmentVariable("HYPRCAST_CASCADE_SCREENSHOT");
        if (!cascadeCapture.isEmpty()) {
            for (const QString& key : {QStringLiteral("Enter"), QStringLiteral("Tab"), QStringLiteral("Escape"), QStringLiteral("Space")}) {
                InterpretedAction cap;
                cap.kind = InterpretedActionKind::Key;
                cap.key = key;
                presenter.historyModel().apply(cap);
            }
            settle();
            QEventLoop captureLoop;
            QTimer::singleShot(200, &captureLoop, &QEventLoop::quit);
            captureLoop.exec();
            check(view.grabWindow().save(cascadeCapture), "save optional Cascade rendering artifact");
        }
        for (int i = 0; i < 200; ++i) presenter.historyModel().apply(cascadeChord);
        settle();
        if (cascade) {
            int instantiated = 0;
            for (auto* child : cascade->childItems())
                if (child->objectName() == QStringLiteral("cascadeGroup")) ++instantiated;
            check(instantiated <= 10, "Cascade rendering stays bounded with large history");
        }
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("reduced")}, {QStringLiteral("visible_groups"), 3},
                               {QStringLiteral("group_alignment"), QStringLiteral("left")}};
        runtime.applyAcceptedConfiguration(config);
        settle();
        check(cascade && cascade->property("capacity").toInt() == 3, "Cascade live depth settings update capacity");
        InputHistoryOptions trimmedCascade;
        trimmedCascade.maxRetainedUtf16CodeUnits = 48;
        presenter.setHistoryOptions(trimmedCascade);
        settle();
        if (cascade) {
            for (auto* child : cascade->childItems()) {
                if (child->objectName() == QStringLiteral("cascadeGroup") && child->property("depth").toInt() == 0)
                    check(qFuzzyCompare(child->y() + child->height(), cascade->height()), "Cascade retention trimming preserves newest anchor");
            }
        }
        presenter.setHistoryOptions(InputHistoryOptions{});
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("full")}};
        runtime.applyAcceptedConfiguration(config);
        for (int i = 0; i < 80; ++i) {
            presenter.historyModel().apply(cascadeChord);
            settle();
        }
        if (cascade) {
            int animatedDelegates = 0;
            for (auto* child : cascade->childItems())
                if (child->objectName() == QStringLiteral("cascadeGroup")) ++animatedDelegates;
            if (animatedDelegates > 12) std::cerr << "Cascade animated delegates: " << animatedDelegates << '\n';
            check(animatedDelegates <= 12, "Cascade full-motion bursts do not retain an unbounded transition queue");
        }
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("reduced")}};
        runtime.applyAcceptedConfiguration(config);
        presenter.clearHistory();
        InputHistoryOptions cascadeOptions;
        cascadeOptions.presentation.countedRepeats = true;
        cascadeOptions.presentation.repeatThreshold = 3;
        presenter.setHistoryOptions(cascadeOptions);
        for (int i = 0; i < 5; ++i) presenter.historyModel().apply(cascadeChord);
        settle();
        check(hasVisualText(view.rootObject(), QStringLiteral("×5")), "Cascade counted repeats use one updating badge");
        InterpretedAction cascadeBackspace;
        cascadeBackspace.kind = InterpretedActionKind::Key;
        cascadeBackspace.key = QStringLiteral("Backspace");
        presenter.historyModel().apply(cascadeBackspace);
        settle();
        check(hasVisualText(view.rootObject(), QStringLiteral("×4")), "Cascade Backspace updates the projected repeat group");
        const auto cascadeExpiration = KeyboardPresenter::Clock::now();
        presenter.setExpiration(1, 250, cascadeExpiration);
        presenter.advance(cascadeExpiration + std::chrono::milliseconds(2));
        settle();
        auto* expiredCascade = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("cascadeExpiredStage"));
        check(cascade && !cascade->isVisible() && expiredCascade && expiredCascade->isVisible() &&
                  hasVisualText(expiredCascade, QStringLiteral("×4")), "Cascade transfers expiration to the supplied snapshot");
        presenter.historyModel().apply(cascadeText);
        settle();
        check(cascade && cascade->isVisible() && expiredCascade && !expiredCascade->isVisible(),
              "Cascade never layers active and expired stacks on fresh input");
        presenter.clearHistory();
        presenter.setExpiration(1, 0, cascadeExpiration);
        presenter.historyModel().apply(cascadeText);
        presenter.advance(cascadeExpiration + std::chrono::milliseconds(3));
        settle();
        check(!presenter.fading() && expiredCascade && !expiredCascade->isVisible(), "zero-duration Cascade expiration leaves no snapshot");
        presenter.setExpiration(0, 0);
        presenter.setHistoryOptions(InputHistoryOptions{});
        config.height = 70;
        view.resize(420, 70);
        runtime.applyAcceptedConfiguration(config);
        settle();
        check(cascade && cascade->property("capacity").toInt() == 1, "short Cascade surface prioritizes one readable group");
        config.height = 10;
        view.resize(420, 10);
        runtime.applyAcceptedConfiguration(config);
        settle();
        check(cascade && cascade->height() == 0 && cascade->property("capacity").toInt() == 0,
              "too-small Cascade surface safely zero-sizes the stage");
        presenter.clearHistory();
        config.width = 600;
        config.height = 120;
        config.dynamicSize = true;
        view.resize(600, 120);
        config.minWidth = 160;
        config.themeId = QStringLiteral("text-held");
        config.themeOptions.clear();
        check(runtime.prepareSwitch(config, &error), "load standalone held-key example through normal theme discovery");
        runtime.applyAcceptedConfiguration(config);
        auto* heldPanel = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastPanelFrame"), Qt::FindChildrenRecursively);
        auto* heldFill = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastPanelBackground"), Qt::FindChildrenRecursively);
        QCoreApplication::processEvents();
        auto* heldHistoryArea = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHistoryArea"), Qt::FindChildrenRecursively);
        check(heldHistoryArea != nullptr, "held example exposes its reserved history area");
        const qreal historyYBeforeHeld = heldHistoryArea ? heldHistoryArea->y() : 0;
        const qreal historyHeightBeforeHeld = heldHistoryArea ? heldHistoryArea->height() : 0;
        const qreal panelHeightBeforeHeld = heldPanel ? heldPanel->height() : 0;
        const int historyBeforeHeld = presenter.historyModel().rowCount();
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 3, .keycode = 42, .pressed = true});
        QCoreApplication::processEvents();
        check(heldPanel && heldFill && heldFill->isVisible() && hasVisualText(view.rootObject(), presenter.heldKeyItems().front().toMap().value(QStringLiteral("label")).toString()) &&
                  presenter.historyModel().rowCount() == historyBeforeHeld,
              "held example reacts to modifier press without adding history");
        check(heldHistoryArea && heldHistoryArea->y() == historyYBeforeHeld && heldHistoryArea->height() == historyHeightBeforeHeld &&
                  heldPanel && heldPanel->height() == panelHeightBeforeHeld,
              "held press preserves history geometry and panel height");
        QVariantMap heldOptions;
        check(catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("height"), 31}}, &heldOptions, &error) &&
                  heldOptions.value(QStringLiteral("height")).toInt() == 31,
              "held example declares its own style options");
        check(!catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("height"), -1}}, &heldOptions, &error) &&
                  !catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("show_held_keys"), QStringLiteral("true")}}, &heldOptions, &error),
              "held example rejects invalid theme-local options");
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 4, .keycode = 42, .pressed = false});
        QCoreApplication::processEvents();
        check(presenter.heldKeyCount() == 0 && presenter.historyModel().rowCount() == historyBeforeHeld + 1,
              "modifier release removes held state and records standalone modifier history");
        check(heldHistoryArea && heldHistoryArea->y() == historyYBeforeHeld && heldHistoryArea->height() == historyHeightBeforeHeld,
              "held release preserves the reserved history geometry");
        presenter.clearHistory();
        QCoreApplication::processEvents();
        auto* idleDock = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHeldViewport"));
        auto* idleShift = findVisualItem(view.rootObject(), QStringLiteral("modifierSlotShift"));
        check(idleDock && !idleDock->isVisible() && idleShift && !idleShift->property("pressed").toBool() && heldFill && !heldFill->isVisible(),
              "held example hides idle placeholders and decoration after history clears");

        config.dynamicSize = false;
        config.height = 60;
        for (const auto& alignment : {QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right")}) {
            config.themeOptions = {{QStringLiteral("dock_alignment"), alignment}, {QStringLiteral("motion"), QStringLiteral("reduced")}};
            check(runtime.prepareSwitch(config, &error), "prepare fixed dock alignment");
            runtime.applyAcceptedConfiguration(config);
            settle();
            auto* history = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHistoryArea"));
            auto* held = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHeldViewport"));
            auto* shift = findVisualItem(view.rootObject(), QStringLiteral("modifierSlotShift"));
            check(history && held && history->height() > 0 && history->y() + history->height() <= held->y(),
                  "short surface retains separate stacked bands");
            if (!history || !held || !shift) continue;
            const QRectF before(history->x(), history->y(), history->width(), history->height());
            const qreal slotX = shift->x();
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 5, .keycode = 42, .pressed = true});
            settle();
            check(shift->property("pressed").toBool() && shift->x() == slotX &&
                  before == QRectF(history->x(), history->y(), history->width(), history->height()),
                  "canonical modifier activates fixed slot without moving history");
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 6, .keycode = 42, .pressed = false});
            settle();
            check(!shift->property("pressed").toBool() && shift->x() == slotX,
                  "release clears live state while historical modifier remains");
            presenter.clearHistory();
        }
        check(!catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("layout"), QStringLiteral("floating")}}, &heldOptions, &error) &&
              !catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("compact_held_fraction"), 0.9}}, &heldOptions, &error),
              "held layout enums and proportions reject invalid values");
        config.height = 140;
        config.width = 600;
        view.resize(600, 140);
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("reduced")}, {QStringLiteral("show_altgr"), true}};
        runtime.applyAcceptedConfiguration(config);
        settle();
        auto* ribbon = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("textHeldActiveStage"));
        auto* expiredRibbon = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("textHeldExpiredStage"));
        check(findVisualItem(view.rootObject(), QStringLiteral("modifierSlotAltGr")) != nullptr, "optional canonical AltGr has its own slot");
        presenter.historyModel().apply(cascadeText);
        presenter.historyModel().apply(cascadeChord);
        settle();
        check(ribbon && hasVisualText(ribbon, cascadeText.text) && hasVisualText(ribbon, QStringLiteral("Ctrl")) && hasVisualText(ribbon, QStringLiteral("C")),
              "ribbon preserves Unicode and distinct chord caps");
        auto checkRibbon = [&]() {
            if (!ribbon) return;
            int instantiated = 0;
            int newest = 0;
            QList<QRectF> rectangles;
            for (auto* group : ribbon->childItems()) {
                if (group->objectName() != QStringLiteral("ribbonGroup")) continue;
                ++instantiated;
                const QRectF visual = group->mapRectToItem(ribbon, QRectF(0, 0, group->width(), group->height()));
                if (group->property("depth").toInt() == 0) {
                    ++newest;
                    check(qAbs(visual.right() - ribbon->width()) < 0.5, "newest ribbon group stays right anchored");
                    check(visual.left() >= -0.5, "newest long chord fits bounded viewport");
                }
                if (group->width() <= ribbon->width())
                    check(qFuzzyCompare(group->scale(), 1.0), "ribbon age never shrinks a fitting group");
                if (group->opacity() > 0) {
                    const qreal fadeWidth = ribbon->property("edgeFadeWidth").toReal();
                    if (group->x() >= fadeWidth)
                        check(qFuzzyCompare(group->opacity(), 1.0), "ribbon remains fully opaque away from the far left edge");
                    check(visual.left() >= -0.5 && visual.right() <= ribbon->width() + 0.5 && visual.top() >= -0.5,
                          "visible ribbon geometry stays inside the reserved band");
                    for (const auto& other : rectangles)
                        check(!visual.intersects(other), "variable-width fitted groups never collide in final state");
                    rectangles.push_back(visual);
                }
            }
            check(newest == 1, "nonempty ribbon realizes exactly one newest projected group");
            check(instantiated <= 16, "ribbon delegates remain bounded independently of retention");
        };
        checkRibbon();
        check(!catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("depth_scale"), 0.9}}, &heldOptions, &error) &&
              !catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("depth_opacity"), 0.7}}, &heldOptions, &error),
              "removed receding controls are rejected rather than ignored");
        presenter.clearHistory();
        InterpretedAction edgeText;
        edgeText.kind = InterpretedActionKind::Text;
        edgeText.text = QStringLiteral("a");
        for (int i = 0; i < 3; ++i) presenter.historyModel().apply(edgeText);
        settle();
        QQuickItem* edgeGroup = nullptr;
        if (ribbon) for (auto* group : ribbon->childItems())
            if (group->objectName() == QStringLiteral("ribbonGroup") && group->property("depth").toInt() == 2) edgeGroup = group;
        check(edgeGroup != nullptr, "edge-fade fixture realizes the oldest of three groups");
        if (edgeGroup) {
            config.width = qRound(config.width - edgeGroup->x() + 5);
            view.resize(config.width, config.height);
            runtime.applyAcceptedConfiguration(config);
            settle();
            const qreal fadeWidth = ribbon->property("edgeFadeWidth").toReal();
            check(edgeGroup->x() > 0 && edgeGroup->x() < fadeWidth && fadeWidth <= 12 &&
                  qAbs(edgeGroup->opacity() - edgeGroup->x() / fadeWidth) < 0.01 && qFuzzyCompare(edgeGroup->scale(), 1.0),
                  "only the far-left group fades in a narrow edge zone, without shrinking");
            checkRibbon();
        }
        config.width = 600;
        view.resize(600, 140);
        runtime.applyAcceptedConfiguration(config);
        presenter.clearHistory();
        for (int i = 0; i < 200; ++i) presenter.historyModel().apply(i % 2 ? cascadeChord : cascadeText);
        settle();
        checkRibbon();
        presenter.historyModel().apply(cascadeBackspace);
        settle();
        checkRibbon();
        InputHistoryOptions ribbonOptions;
        ribbonOptions.presentation.countedRepeats = true;
        ribbonOptions.presentation.repeatThreshold = 3;
        ribbonOptions.presentation.modifierSymbols = {{QStringLiteral("Ctrl"), QStringLiteral("same")}, {QStringLiteral("Shift"), QStringLiteral("same")}};
        presenter.clearHistory();
        presenter.setHistoryOptions(ribbonOptions);
        config.modifierSymbols = ribbonOptions.presentation.modifierSymbols;
        runtime.applyAcceptedConfiguration(config);
        for (int i = 0; i < 5; ++i) presenter.historyModel().apply(cascadeChord);
        settle();
        check(ribbon && hasVisualText(ribbon, QStringLiteral("×5")), "ribbon updates counted repeat badge in place");
        presenter.historyModel().apply(cascadeBackspace);
        settle();
        check(ribbon && hasVisualText(ribbon, QStringLiteral("×4")), "ribbon Backspace decrements counted group");
        auto* ctrlSlot = findVisualItem(view.rootObject(), QStringLiteral("modifierSlotCtrl"));
        auto* shiftSlot = findVisualItem(view.rootObject(), QStringLiteral("modifierSlotShift"));
        presenter.processMessage(KeyboardSnapshotMessage{.id = 2, .name = QStringLiteral("second"), .keymap = testKeymap()});
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 10, .keycode = 42, .pressed = true});
        presenter.processMessage(KeyMessage{.keyboardId = 2, .timeMs = 10, .keycode = 42, .pressed = true});
        settle();
        check(shiftSlot && shiftSlot->property("pressed").toBool() && ctrlSlot && !ctrlSlot->property("pressed").toBool() &&
              shiftSlot->property("label").toString() == ctrlSlot->property("label").toString(),
              "duplicate display symbols do not conflate canonical modifiers");
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 11, .keycode = 42, .pressed = false});
        settle();
        check(shiftSlot && shiftSlot->property("pressed").toBool(), "slot stays held while another keyboard observes the modifier");
        const auto ribbonExpiration = KeyboardPresenter::Clock::now();
        presenter.setExpiration(1, 250, ribbonExpiration);
        presenter.advance(ribbonExpiration + std::chrono::milliseconds(2));
        settle();
        check(ribbon && !ribbon->isVisible() && expiredRibbon && expiredRibbon->isVisible() && shiftSlot && shiftSlot->property("pressed").toBool(),
              "expiration transfers ribbon while preserving independently held dock");
        presenter.historyModel().apply(cascadeText);
        settle();
        check(ribbon && ribbon->isVisible() && expiredRibbon && !expiredRibbon->isVisible(), "fresh input never layers both ribbons");
        presenter.processMessage(KeyMessage{.keyboardId = 2, .timeMs = 12, .keycode = 42, .pressed = false});
        settle();
        check(shiftSlot && !shiftSlot->property("pressed").toBool(), "final observed release immediately clears slot state");
        const auto idleExpiration = KeyboardPresenter::Clock::now();
        presenter.setExpiration(1, 10000, idleExpiration);
        presenter.advance(idleExpiration + std::chrono::milliseconds(2));
        settle();
        auto* fadingDock = findVisualItem(view.rootObject(), QStringLiteral("hyprcastHeldViewport"));
        for (const QString& visibility : {QStringLiteral("with-content"), QStringLiteral("never"), QStringLiteral("always")}) {
            config.panelVisibility = visibility;
            runtime.applyAcceptedConfiguration(config);
            settle();
            // Set a deterministic intermediate fade value rather than waiting on animation timing.
            if (expiredRibbon) expiredRibbon->setOpacity(0.4);
            const qreal expectedOpacity = visibility == QStringLiteral("always") ? 1.0 : 0.4;
            check(heldFill && fadingDock && qAbs(heldFill->opacity() - expectedOpacity) < 0.001 &&
                      qAbs(fadingDock->opacity() - expectedOpacity) < 0.001,
                  "panel and idle dock fade together unless panel visibility is always");
            check(heldFill && heldFill->isVisible() == (visibility != QStringLiteral("never")),
                  "never hides decoration while preserving dock fade behavior");
        }
        config.panelVisibility = QStringLiteral("with-content");
        runtime.applyAcceptedConfiguration(config);
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 13, .keycode = 42, .pressed = true});
        settle();
        if (expiredRibbon) expiredRibbon->setOpacity(0.4);
        check(heldFill && fadingDock && qFuzzyCompare(heldFill->opacity(), 1.0) && qFuzzyCompare(fadingDock->opacity(), 1.0),
              "observed held modifier keeps panel and dock opaque during history fade");
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 14, .keycode = 42, .pressed = false});
        presenter.clearHistory();
        presenter.setExpiration(0, 0);
        presenter.setHistoryOptions(InputHistoryOptions{});
        config.modifierSymbols.clear();
        runtime.applyAcceptedConfiguration(config);
        InterpretedAction longChord = cascadeChord;
        longChord.key = QString(300, QLatin1Char('W'));
        longChord.modifiers = {QStringLiteral("Ctrl"), QStringLiteral("Shift"), QStringLiteral("Alt"), QStringLiteral("Super")};
        presenter.historyModel().apply(longChord);
        config.width = 100;
        config.height = 60;
        view.resize(100, 60);
        runtime.applyAcceptedConfiguration(config);
        settle();
        checkRibbon();
        if (shiftSlot) {
            const QRectF slotRect = shiftSlot->mapRectToItem(view.rootObject(), QRectF(0, 0, shiftSlot->width(), shiftSlot->height()));
            check(slotRect.left() >= 0 && slotRect.right() <= 100, "narrow dock bounds every fixed slot rather than clipping arbitrary caps");
        }
        config.width = 600;
        config.height = 140;
        view.resize(600, 140);
        runtime.applyAcceptedConfiguration(config);
        presenter.clearHistory();
        presenter.historyModel().apply(cascadeText);
        presenter.historyModel().apply(cascadeChord);
        settle();
        const QString ribbonCapture = qEnvironmentVariable("HYPRCAST_TEXT_HELD_SCREENSHOT");
        if (!ribbonCapture.isEmpty()) {
            for (const QString& key : {QStringLiteral("Tab"), QStringLiteral("Enter"), QStringLiteral("Escape"), QStringLiteral("Space")}) {
                InterpretedAction cap;
                cap.kind = InterpretedActionKind::Key;
                cap.key = key;
                presenter.historyModel().apply(cap);
            }
            presenter.historyModel().apply(cascadeChord);
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 13, .keycode = 29, .pressed = true});
            settle();
            QEventLoop captureLoop;
            QTimer::singleShot(200, &captureLoop, &QEventLoop::quit);
            captureLoop.exec();
            check(view.grabWindow().save(ribbonCapture), "save optional text-held rendering artifact");
            presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 14, .keycode = 29, .pressed = false});
        }
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("full")}};
        runtime.applyAcceptedConfiguration(config);
        for (int i = 0; i < 40; ++i) {
            presenter.historyModel().apply(cascadeChord);
            settle();
        }
        int movingGroups = 0;
        if (ribbon) for (auto* group : ribbon->childItems())
            if (group->objectName() == QStringLiteral("ribbonGroup")) ++movingGroups;
        check(movingGroups > 0 && movingGroups <= 16, "full-motion input burst keeps delegates bounded without a removal queue");
        config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("reduced")}, {QStringLiteral("edge_depth"), 0}};
        runtime.applyAcceptedConfiguration(config);
        InputHistoryOptions ribbonTrim;
        ribbonTrim.maxRetainedUtf16CodeUnits = 48;
        presenter.setHistoryOptions(ribbonTrim);
        settle();
        checkRibbon();
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 15, .keycode = 42, .pressed = true});
        presenter.resetConnection();
        settle();
        shiftSlot = findVisualItem(view.rootObject(), QStringLiteral("modifierSlotShift"));
        check(shiftSlot && !shiftSlot->property("pressed").toBool(), "held-list reset immediately clears pressed visuals");
        presenter.processMessage(KeyboardSnapshotMessage{.id = 1, .name = QStringLiteral("test"), .keymap = testKeymap()});
        presenter.clearHistory();
        presenter.setHistoryOptions(InputHistoryOptions{});
        const auto immediateRibbonExpiration = KeyboardPresenter::Clock::now();
        presenter.historyModel().apply(cascadeText);
        presenter.setExpiration(1, 0, immediateRibbonExpiration);
        presenter.advance(immediateRibbonExpiration + std::chrono::milliseconds(3));
        settle();
        check(presenter.historyModel().rowCount() == 0 && !presenter.fading() && expiredRibbon && !expiredRibbon->isVisible(), "zero-duration ribbon expiration is immediate");
        presenter.setExpiration(0, 0);
        presenter.clearHistory();
        config.height = 10;
        view.resize(600, 10);
        runtime.applyAcceptedConfiguration(config);
        settle();
        check(ribbon && ribbon->height() == 0 && ribbon->property("capacity").toInt() == 0, "impossible short surface safely zero-sizes both bands");
        config.height = 120;
        view.resize(600, 120);
        QVariantMap ledgerOptions;
        check(catalog.validateOptions(QStringLiteral("ledger"), {{QStringLiteral("rail_width"), 0}, {QStringLiteral("inner_padding"), 8},
              {QStringLiteral("text_scale"), 1.5}}, &ledgerOptions, &error), "ledger validates curated layout options");
        check(!catalog.validateOptions(QStringLiteral("ledger"), {{QStringLiteral("text_scale"), 0.0}}, &ledgerOptions, &error),
              "ledger rejects unreadable text scales");
        config.dynamicSize = true;
        config.height = 120;
        config.themeId      = QStringLiteral("ledger");
        config.themeOptions = {{QStringLiteral("item_spacing"), 8}, {QStringLiteral("accent"), QStringLiteral("#40ccaa")}};
        check(runtime.prepareSwitch(config, &error), "load the shipped, distinctly composed QML example");
        runtime.applyAcceptedConfiguration(config);
        check(view.rootObject()->findChild<QQuickItem*>(QStringLiteral("ledger-8"), Qt::FindChildrenRecursively) != nullptr,
              "the shipped example receives its declared options through the public API");
        auto* ledgerPanel = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastLedgerPanelFrame"), Qt::FindChildrenRecursively);
        check(ledgerPanel != nullptr, "ledger exposes its dynamic panel frame");
        if (ledgerPanel) {
            InterpretedAction line;
            line.kind = InterpretedActionKind::Text;
            for (int i = 0; i < 2; ++i) {
                line.text = QString::number(i);
                presenter.historyModel().apply(line);
            }
            QCoreApplication::processEvents();
            const qreal growingHeight = ledgerPanel->height();
            check(growingHeight > config.minHeight && growingHeight < config.height, "ledger grows vertically with its timeline content");
            for (int i = 2; i < 10; ++i) {
                line.text = QString::number(i);
                presenter.historyModel().apply(line);
            }
            QCoreApplication::processEvents();
            const qreal fullHeight = ledgerPanel->height();
            check(qFuzzyCompare(fullHeight, static_cast<qreal>(config.height)), "ledger caps its dynamic height at the configured maximum");
            InterpretedAction backspace;
            backspace.kind = InterpretedActionKind::Key;
            backspace.key  = QStringLiteral("Backspace");
            for (int i = 0; i < 8; ++i) {
                presenter.historyModel().apply(backspace);
            }
            QCoreApplication::processEvents();
            check(ledgerPanel->height() < fullHeight, "ledger panel shrinks as Backspace shortens the timeline");
            presenter.historyModel().clear();
        }
        config.themeId = QStringLiteral("builtin:default");
        config.themeOptions.clear();
        check(runtime.prepareSwitch(config, &error), "stage return to the bundled theme");
        runtime.applyAcceptedConfiguration(config);

        InterpretedAction typed;
        typed.kind = InterpretedActionKind::Text;
        typed.text = QStringLiteral("kept");
        presenter.historyModel().apply(typed);

        const QString configPath = hostDirectory.filePath(QStringLiteral("overlay.toml"));
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='builtin:default'\n")), "write initial theme config");
        OverlayConfigManager manager(configPath, true);
        manager.setRuntimeValidator([&catalog, &runtime](const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate, QString* validationError) {
            QVariantMap effective;
            if (!catalog.validateOptions(candidate.themeId, candidate.themeOptions, &effective, validationError)) {
                return false;
            }
            return !previous || previous->themeId == candidate.themeId || runtime.prepareSwitch(candidate, validationError);
        });
        check(manager.initialize(&error), "initialize config manager with the active bundled theme");
        QObject::connect(&manager, &OverlayConfigManager::configurationChanged, &view, [&manager, &runtime] { runtime.applyAcceptedConfiguration(manager.config()); });

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheight=30\n")),
              "select standalone held feedback via live config");
        manager.reloadNow();
        check(runtime.activeThemeId() == QStringLiteral("text-held") && manager.config().themeOptions.value(QStringLiteral("height")).toLongLong() == 30,
              "live config accepts declared cap style");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheight=32\n")),
              "update cap style without switching themes");
        manager.reloadNow();
        auto* heldRow = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHeldViewport"), Qt::FindChildrenRecursively);
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 5, .keycode = 42, .pressed = true});
        QCoreApplication::processEvents();
        check(manager.config().themeOptions.value(QStringLiteral("height")).toLongLong() == 32 && heldRow && heldRow->isVisible() && heldRow->height() > 0 && heldRow->height() <= 32,
              "accepted cap style reaches bounded two-band geometry");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheight=32\nshow_held_keys=false\n")),
              "hide example held feedback via live theme option");
        manager.reloadNow();
        QCoreApplication::processEvents();
        check(heldRow && !heldRow->isVisible() && presenter.heldKeyCount() == 1,
              "example option hides the dock without changing backend held state");
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 6, .keycode = 42, .pressed = false});
        presenter.clearHistory();
        presenter.historyModel().apply(typed);
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheight=0\n")),
              "write invalid cap style for live reload");
        manager.reloadNow();
        check(manager.config().themeOptions.value(QStringLiteral("height")).toLongLong() == 32 &&
                  !manager.config().themeOptions.value(QStringLiteral("show_held_keys")).toBool() && runtime.activeThemeId() == QStringLiteral("text-held"),
              "invalid cap style keeps last accepted theme and options");

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=12\n")), "write config selecting a valid external theme and option");
        manager.reloadNow();
        check(manager.config().themeId == QStringLiteral("good") && runtime.activeThemeId() == QStringLiteral("good"),
              "a validated live config atomically accepts a prepared theme");
        check(view.rootObject()->findChild<QQuickItem*>(QStringLiteral("gap-12"), Qt::FindChildrenRecursively) != nullptr,
              "QML receives declared option values and defaults through the versioned API");
        check(presenter.outputText() == QStringLiteral("kept"), "theme switching preserves editable input history");

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n")), "write a live update to a declared option");
        manager.reloadNow();
        check(manager.config().themeOptions.value(QStringLiteral("gap")).toLongLong() == 14 &&
                  view.rootObject()->findChild<QQuickItem*>(QStringLiteral("gap-14"), Qt::FindChildrenRecursively) != nullptr,
              "accepted option changes notify the active QML theme without recreating history");

        int rejections = 0;
        QObject::connect(&manager, &OverlayConfigManager::reloadRejected, &view, [&rejections] { ++rejections; });
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap='wrong'\n")), "write a config with a mistyped theme option");
        manager.reloadNow();
        check(rejections == 1 && manager.config().themeOptions.value(QStringLiteral("gap")).toLongLong() == 14 &&
                  view.rootObject()->findChild<QQuickItem*>(QStringLiteral("gap-14"), Qt::FindChildrenRecursively) != nullptr,
              "invalid options leave accepted settings and the active visual unchanged");

        check(writeFile(configPath, QByteArrayLiteral("[window]\nwidth=800\n[theme]\nid='badroot'\n")), "write config with a broken root theme and other changes");
        manager.reloadNow();
        check(rejections == 2 && manager.config().themeId == QStringLiteral("good") && manager.config().width == 600 &&
                  manager.config().themeOptions.value(QStringLiteral("gap")).toLongLong() == 14,
              "a rejected theme switch leaves the entire accepted config unchanged");
        check(runtime.activeThemeId() == QStringLiteral("good") && presenter.outputText() == QStringLiteral("kept"),
              "a rejected root component keeps the old visual theme and history live");

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='syntax'\n")), "write config selecting a syntactically broken theme");
        manager.reloadNow();
        check(rejections == 3 && manager.config().themeId == QStringLiteral("good") && runtime.activeThemeId() == QStringLiteral("good"),
              "QML compilation failure rejects the switch without replacing the accepted theme");

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='ledger'\n[theme.options]\nitem_spacing=8\naccent='#40ccaa'\n")),
              "write a valid config after rejected theme attempts");
        manager.reloadNow();
        check(manager.config().themeId == QStringLiteral("ledger") && runtime.activeThemeId() == QStringLiteral("ledger") &&
                  view.rootObject()->findChild<QQuickItem*>(QStringLiteral("ledger-8"), Qt::FindChildrenRecursively) != nullptr,
              "a later valid theme config recovers normally after rejected attempts");
        check(presenter.outputText() == QStringLiteral("kept"), "theme switching and recovery preserve input history");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='keycaps'\n[theme.options]\nheight=44\n")), "select copied keycaps package through live config");
        manager.reloadNow();
        check(runtime.activeThemeId() == QStringLiteral("keycaps") && presenter.outputText() == QStringLiteral("kept"),
              "switching to standalone keycaps keeps history");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='keycaps'\n[theme.options]\nheight=60\n")), "change live keycaps height");
        manager.reloadNow();
        auto* capFrame = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastKeycapsPanelFrame"), Qt::FindChildrenRecursively);
        check(capFrame && capFrame->height() >= 68 && presenter.outputText() == QStringLiteral("kept"),
              "live keycaps option affects panel geometry without clearing history");
    }
} // namespace

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    QTemporaryDir   user;
    QTemporaryDir   system;
    QTemporaryDir   bundled;
    check(user.isValid() && system.isValid() && bundled.isValid(), "create isolated theme roots");

    testDiscoveryAndPrecedence(user.path(), system.path(), bundled.path());

    QTemporaryDir optionUser;
    QTemporaryDir optionBundled;
    testOptions(optionUser.path(), optionBundled.path());

    QTemporaryDir runtimeUser;
    testRuntimeLoadingAndSwitchRecovery(runtimeUser.path());

    if (failures == 0) {
        std::cout << "Overlay theme tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
