#include "theme/Theme.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
        for (const QString& file : {QStringLiteral("theme.toml"), QStringLiteral("Main.qml"), QStringLiteral("TextPresentation.qml")}) {
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
        check(catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("held_key_height"), 31}}, &heldOptions, &error) &&
                  heldOptions.value(QStringLiteral("held_key_height")).toInt() == 31,
              "held example declares its own style options");
        check(!catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("held_key_height"), -1}}, &heldOptions, &error) &&
                  !catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("show_held_keys"), QStringLiteral("true")}}, &heldOptions, &error),
              "held example rejects invalid theme-local options");
        const QString heldLabel = presenter.heldKeyItems().front().toMap().value(QStringLiteral("label")).toString();
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 4, .keycode = 42, .pressed = false});
        QCoreApplication::processEvents();
        check(presenter.heldKeyCount() == 0 && presenter.historyModel().rowCount() == historyBeforeHeld + 1,
              "modifier release removes held state and records standalone modifier history");
        check(heldHistoryArea && heldHistoryArea->y() == historyYBeforeHeld && heldHistoryArea->height() == historyHeightBeforeHeld,
              "held release preserves the reserved history geometry");
        presenter.clearHistory();
        QCoreApplication::processEvents();
        check(!hasVisualText(view.rootObject(), heldLabel) && heldFill && !heldFill->isVisible(),
              "held example removes feedback and decoration after history clears");

        config.dynamicSize = false;
        config.height = 60;
        for (const auto& layout : {QStringLiteral("auto"), QStringLiteral("compact"), QStringLiteral("stacked")}) {
            for (const auto& side : {QStringLiteral("left"), QStringLiteral("right")}) {
                config.themeOptions = {{QStringLiteral("layout"), layout}, {QStringLiteral("held_side"), side},
                                       {QStringLiteral("compact_held_fraction"), 0.3}};
                check(runtime.prepareSwitch(config, &error), "prepare held composition options");
                runtime.applyAcceptedConfiguration(config);
                QCoreApplication::processEvents();
                auto* history = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHistoryArea"));
                auto* held = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHeldViewport"));
                check(history && held && history->height() > 0, "short surfaces retain history for every held layout");
                if (!history || !held)
                    continue;
                if (layout != QStringLiteral("stacked")) {
                    check(side == QStringLiteral("left") ? held->x() + held->width() < history->x() :
                          history->x() + history->width() < held->x(), "compact held side leaves a nonoverlapping history area");
                } else {
                    check(history->y() + history->height() <= held->y(), "forced stacked keeps history above held keys");
                }
                const QRectF before(history->x(), history->y(), history->width(), history->height());
                presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 5, .keycode = 42, .pressed = true});
                QCoreApplication::processEvents();
                check(before == QRectF(history->x(), history->y(), history->width(), history->height()), "compact/forced press preserves history geometry");
                presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 6, .keycode = 42, .pressed = false});
                QCoreApplication::processEvents();
                check(before == QRectF(history->x(), history->y(), history->width(), history->height()), "compact/forced release preserves history geometry");
                presenter.clearHistory();
            }
        }
        check(!catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("layout"), QStringLiteral("floating")}}, &heldOptions, &error) &&
              !catalog.validateOptions(QStringLiteral("text-held"), {{QStringLiteral("compact_held_fraction"), 0.9}}, &heldOptions, &error),
              "held layout enums and proportions reject invalid values");
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

        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheld_key_height=30\n")),
              "select standalone held feedback via live config");
        manager.reloadNow();
        check(runtime.activeThemeId() == QStringLiteral("text-held") && manager.config().themeOptions.value(QStringLiteral("held_key_height")).toLongLong() == 30,
              "live config accepts declared held style");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheld_key_height=32\n")),
              "update held style without switching themes");
        manager.reloadNow();
        auto* heldRow = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("hyprcastHeldRow"), Qt::FindChildrenRecursively);
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 5, .keycode = 42, .pressed = true});
        QCoreApplication::processEvents();
        check(manager.config().themeOptions.value(QStringLiteral("held_key_height")).toLongLong() == 32 && heldRow && heldRow->isVisible() && heldRow->height() == 32,
              "accepted held style change reaches visible example geometry");
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheld_key_height=32\nshow_held_keys=false\n")),
              "hide example held feedback via live theme option");
        manager.reloadNow();
        QCoreApplication::processEvents();
        check(heldRow && !heldRow->isVisible() && presenter.heldKeyCount() == 1,
              "example option hides the row without changing backend held state");
        presenter.processMessage(KeyMessage{.keyboardId = 1, .timeMs = 6, .keycode = 42, .pressed = false});
        presenter.clearHistory();
        presenter.historyModel().apply(typed);
        check(writeFile(configPath, QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheld_key_height=0\n")),
              "write invalid held style for live reload");
        manager.reloadNow();
        check(manager.config().themeOptions.value(QStringLiteral("held_key_height")).toLongLong() == 32 &&
                  !manager.config().themeOptions.value(QStringLiteral("show_held_keys")).toBool() && runtime.activeThemeId() == QStringLiteral("text-held"),
              "invalid held style keeps last accepted theme and options");

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
