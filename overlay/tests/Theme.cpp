#include "theme/Theme.hpp"

#include "Support.hpp"
#include "config/OverlayConfig.hpp"
#include "history/BackspaceMode.hpp"
#include "history/InputHistory.hpp"
#include "history/InterpretedAction.hpp"
#include "hyprcast/protocol/keyboard_id.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/Protocol.hpp"

#include <gtest/gtest.h>
#include <xkbcommon/xkbcommon.h>

#include <QByteArray>
#include <QChar>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIODevice>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QQuickView>
#include <QString>
#include <QTemporaryDir>
#include <QUrl>
#include <QtMath>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <utility>

using namespace hyprcast::overlay;

namespace {

// Adapters that turn the std::expected APIs into a bool and an error text.
template <typename T>
bool succeeded(std::expected<T, QString> result, QString* error) {
    if (!result) {
        *error = result.error();
        return false;
    }
    return true;
}

bool discover(ThemeCatalog& catalog, QStringList* warnings, QString* error) {
    auto result = catalog.discover();
    if (result && warnings) {
        *warnings = *result;
    }
    return succeeded(std::move(result), error);
}

bool resolve(const ThemeCatalog& catalog,
             const QString& id,
             ThemeDescriptor* descriptor,
             QString* error) {
    const auto result = catalog.resolve(id);
    if (result) {
        *descriptor = **result;
    }
    return succeeded(result, error);
}

bool validateOptions(const ThemeCatalog& catalog,
                     const QString& id,
                     const QVariantMap& supplied,
                     QVariantMap* effective,
                     QString* error) {
    auto result = catalog.validateOptions(id, supplied);
    if (result) {
        *effective = *result;
    }
    return succeeded(std::move(result), error);
}

bool loadInitial(ThemeRuntime& runtime,
                 const OverlayConfig& config,
                 QString* error) {
    return succeeded(runtime.loadInitial(config), error);
}

bool prepareSwitch(ThemeRuntime& runtime,
                   const OverlayConfig& config,
                   QString* error) {
    return succeeded(runtime.prepareSwitch(config), error);
}

QString testKeymap() {
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    xkb_rule_names names {};
    names.rules = "evdev";
    names.model = "pc105";
    names.layout = "us";
    xkb_keymap* map =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    // xkb_keymap_get_as_string() returns a malloc()ed string.
    const std::unique_ptr<char, decltype(&std::free)> text(
        xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1),
        &std::free);
    const QString result = QString::fromUtf8(text.get());
    xkb_keymap_unref(map);
    xkb_context_unref(context);
    return result;
}

// The next three helpers walk the QML item tree of a theme under test, which
// is a few levels deep.
// NOLINTBEGIN(misc-no-recursion)
bool hasVisualText(QQuickItem* item, const QString& text) {
    if (item->property("text").toString() == text) {
        return true;
    }
    return std::ranges::any_of(item->childItems(), [&text](auto* child) {
        return hasVisualText(child, text);
    });
}

QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
    if (item->objectName() == name) {
        return item;
    }
    for (auto* child : item->childItems()) {
        if (auto* result = findVisualItem(child, name)) {
            return result;
        }
    }
    return nullptr;
}

void polishVisualTree(QQuickItem* item) {
    for (auto* child : item->childItems()) {
        polishVisualTree(child);
    }
    item->ensurePolished();
}
// NOLINTEND(misc-no-recursion)

bool writeFile(const QString& path, const QByteArray& contents) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(contents) == contents.size();
}

QString themeDirectory(const QString& root,
                       const QString& id,
                       const QByteArray& manifest,
                       const QByteArray& qml) {
    const QString directory = QDir(root).filePath(id);
    QDir().mkpath(directory);
    writeFile(QDir(directory).filePath(QStringLiteral("theme.toml")), manifest);
    if (!qml.isEmpty()) {
        writeFile(QDir(directory).filePath(QStringLiteral("Main.qml")), qml);
    }
    return directory;
}

QByteArray manifest(const QByteArray& id, const QByteArray& extra = {}) {
    return QByteArrayLiteral("[theme]\nid = '") + id
        + QByteArrayLiteral(
               "'\nname = 'Test theme'\napi_version = 1\nentry = 'Main.qml'\n")
        + extra;
}

QByteArray validQml(const QByteArray& name = QByteArrayLiteral("theme")) {
    return QByteArrayLiteral(
               "import QtQuick\nItem { anchors.fill: parent; objectName: '")
        + name + QByteArrayLiteral("-' + hyprcast.themeId }\n");
}

QByteArray optionQml() {
    return QByteArrayLiteral("import QtQuick\nItem { anchors.fill: parent; "
                             "objectName: 'gap-' + hyprcast.options.gap }\n");
}

InterpretedAction textAction(const QString& text) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Text;
    action.text = text;
    return action;
}

InterpretedAction keyAction(const QString& key) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Key;
    action.key = key;
    return action;
}

InterpretedAction chordAction(const QString& key,
                              const QStringList& modifiers) {
    InterpretedAction action;
    action.kind = InterpretedActionKind::Chord;
    action.key = key;
    action.modifiers = modifiers;
    return action;
}

// User and system roots that overlap each other and the bundled default, next
// to packages that discovery has to ignore.
class ThemeDiscoveryTest : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(user.isValid() && system.isValid() && bundled.isValid())
            << "create isolated theme roots";
        const QString userRoot = user.path();
        const QString systemRoot = system.path();
        userDefault = themeDirectory(userRoot,
                                     QStringLiteral("default"),
                                     manifest(QByteArrayLiteral("default")),
                                     validQml());
        userShared = themeDirectory(userRoot,
                                    QStringLiteral("shared"),
                                    manifest(QByteArrayLiteral("shared")),
                                    validQml());
        themeDirectory(systemRoot,
                       QStringLiteral("shared"),
                       manifest(QByteArrayLiteral("shared")),
                       validQml());
        systemOnly = themeDirectory(systemRoot,
                                    QStringLiteral("system-only"),
                                    manifest(QByteArrayLiteral("system-only")),
                                    validQml());
        themeDirectory(
            userRoot,
            QStringLiteral("future"),
            QByteArrayLiteral(
                "[theme]\nid='future'\napi_version=99\nentry='Main.qml'\n"),
            validQml());
        themeDirectory(systemRoot,
                       QStringLiteral("future"),
                       manifest(QByteArrayLiteral("future")),
                       validQml());
        themeDirectory(userRoot,
                       QStringLiteral("missing"),
                       manifest(QByteArrayLiteral("missing")),
                       {});
        themeDirectory(
            userRoot,
            QStringLiteral("badmanifest"),
            QByteArrayLiteral("[theme]\nid='badmanifest'\napi_version="
                              "1\nentry='Main.qml'\nunexpected=true\n"),
            validQml());
        themeDirectory(userRoot,
                       QStringLiteral("badpath"),
                       QByteArrayLiteral("[theme]\nid='badpath'\napi_version="
                                         "1\nentry='../outside.qml'\n"),
                       validQml());
        builtinDirectory =
            themeDirectory(bundled.path(),
                           QStringLiteral("default"),
                           manifest(QByteArrayLiteral("default")),
                           validQml());
        ASSERT_TRUE(discover(catalog, &warnings, &error))
            << "discover valid user, system, and bundled themes: "
            << error.toStdString();
        EXPECT_TRUE(error.isEmpty()) << "successful discovery has no error";
    }

    QTemporaryDir user;
    QTemporaryDir system;
    QTemporaryDir bundled;
    QString userDefault;
    QString userShared;
    QString systemOnly;
    QString builtinDirectory;
    ThemeCatalog catalog {
        {user.path(), system.path()},
        QUrl::fromLocalFile(
            QDir(bundled.path()).filePath(QStringLiteral("default/theme.toml")))
    };
    QStringList warnings;
    QString error;
};

TEST_F(ThemeDiscoveryTest, UserThemeShadowsSystemTheme) {
    ThemeDescriptor descriptor;
    EXPECT_TRUE(resolve(catalog, QStringLiteral("shared"), &descriptor, &error))
        << "resolve duplicate theme id";
    EXPECT_TRUE(descriptor.entryUrl.toLocalFile().startsWith(userShared))
        << "user theme deterministically shadows system theme";
    EXPECT_TRUE(
        warnings.join(QLatin1Char('\n')).contains(QStringLiteral("shadowed")))
        << "duplicate installation produces a precedence warning";
}

TEST_F(ThemeDiscoveryTest, SystemThemeNeedsNoUserOverride) {
    ThemeDescriptor descriptor;
    EXPECT_TRUE(
        resolve(catalog, QStringLiteral("system-only"), &descriptor, &error)
        && descriptor.entryUrl.toLocalFile().startsWith(systemOnly))
        << "system themes are available when there is no user override";
}

TEST_F(ThemeDiscoveryTest, BundledAliasSurvivesShadowing) {
    ThemeDescriptor descriptor;
    EXPECT_TRUE(resolve(catalog, QStringLiteral("default"), &descriptor, &error)
                && descriptor.entryUrl.toLocalFile().startsWith(userDefault))
        << "user theme may shadow the bundled theme's ordinary id";
    EXPECT_TRUE(
        resolve(catalog, QStringLiteral("builtin:default"), &descriptor, &error)
        && descriptor.bundled
        && descriptor.entryUrl.toLocalFile().startsWith(builtinDirectory))
        << "the explicit bundled alias remains accessible when its ordinary id "
           "is "
           "shadowed";
    EXPECT_TRUE(warnings.join(QLatin1Char('\n'))
                    .contains(QStringLiteral("builtin:default")))
        << "shadowing the default name explains the protected built-in alias";
    EXPECT_TRUE(
        catalog.availableThemeIds().contains(QStringLiteral("builtin:default")))
        << "available themes include the protected bundled alias";
}

TEST_F(ThemeDiscoveryTest, IncompatibleApiVersionIsDiagnosed) {
    ThemeDescriptor descriptor;
    EXPECT_TRUE(warnings.join(QLatin1Char('\n'))
                    .contains(QStringLiteral("API version 99")))
        << "incompatible manifests are diagnosed and ignored";
    EXPECT_TRUE(!resolve(catalog, QStringLiteral("future"), &descriptor, &error)
                && error.contains(QStringLiteral("API version 99")))
        << "selecting an incompatible theme reports the compatibility failure "
           "directly";
}

TEST_F(ThemeDiscoveryTest, BrokenPackagesAreIgnored) {
    ThemeDescriptor descriptor;
    EXPECT_TRUE(
        warnings.join(QLatin1Char('\n')).contains(QStringLiteral("missing")))
        << "missing entry points are diagnosed and ignored";
    EXPECT_TRUE(
        !resolve(catalog, QStringLiteral("badmanifest"), &descriptor, &error)
        && error.contains(QStringLiteral("unknown theme manifest key")))
        << "strict manifest validation rejects unknown fields";
}

// One user theme that declares an integer, an enum and a colour option.
class ThemeOptionsTest : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(user.isValid() && bundled.isValid())
            << "create isolated theme roots";
        const QString userRoot = user.path();
        const QByteArray optionDeclarations = QByteArrayLiteral(
            "[options.gap]\ntype='integer'\ndefault=5\nminimum="
            "0\nmaximum=20\ndescription='Space between items.'\n"
            "[options.mode]\ntype='enum'\ndefault='rows'\nvalues="
            "['rows','compact']\n"
            "[options.tint]\ntype='color'\ndefault='#ffffff'\n");
        themeDirectory(
            userRoot,
            QStringLiteral("options"),
            manifest(QByteArrayLiteral("options"), optionDeclarations),
            validQml());
        themeDirectory(bundled.path(),
                       QStringLiteral("default"),
                       manifest(QByteArrayLiteral("default")),
                       validQml());
        ASSERT_TRUE(discover(catalog, nullptr, &error))
            << "discover a theme with declared options: "
            << error.toStdString();
    }

    QTemporaryDir user;
    QTemporaryDir bundled;
    ThemeCatalog catalog {
        {user.path()},
        QUrl::fromLocalFile(
            QDir(bundled.path()).filePath(QStringLiteral("default/theme.toml")))
    };
    QString error;
    QVariantMap effective;
};

TEST_F(ThemeOptionsTest, OmittedOptionsTakeTheirDefaults) {
    EXPECT_TRUE(validateOptions(catalog,
                                QStringLiteral("options"),
                                {},
                                &effective,
                                &error))
        << "apply declared defaults for omitted theme options";
    EXPECT_TRUE(effective.value(QStringLiteral("gap")).toInt() == 5
                && effective.value(QStringLiteral("mode")).toString()
                    == QStringLiteral("rows"))
        << "theme option defaults are exposed to QML";
}

TEST_F(ThemeOptionsTest, WellTypedValuesOverrideTheDefaults) {
    const QVariantMap valid {
        {QStringLiteral("gap"), 12},
        {QStringLiteral("mode"), QStringLiteral("compact")},
        {QStringLiteral("tint"), QStringLiteral("red")},
    };
    EXPECT_TRUE(validateOptions(catalog,
                                QStringLiteral("options"),
                                valid,
                                &effective,
                                &error))
        << "validate well-typed theme option values";
    EXPECT_TRUE(effective.value(QStringLiteral("gap")).toInt() == 12
                && effective.value(QStringLiteral("mode")).toString()
                    == QStringLiteral("compact"))
        << "accepted option overrides are reflected in the effective map";
}

TEST_F(ThemeOptionsTest, UndeclaredOptionIsRejected) {
    EXPECT_TRUE(!validateOptions(catalog,
                                 QStringLiteral("options"),
                                 {{QStringLiteral("unknown"), 1}},
                                 &effective,
                                 &error)
                && error.contains(QStringLiteral("unknown option")))
        << "reject undeclared theme options";
}

TEST_F(ThemeOptionsTest, MistypedValueIsRejected) {
    EXPECT_FALSE(
        validateOptions(catalog,
                        QStringLiteral("options"),
                        {{QStringLiteral("gap"), QStringLiteral("12")}},
                        &effective,
                        &error))
        << "reject mistyped theme options";
}

TEST_F(ThemeOptionsTest, NumberOutsideItsRangeIsRejected) {
    EXPECT_TRUE(!validateOptions(catalog,
                                 QStringLiteral("options"),
                                 {{QStringLiteral("gap"), 21}},
                                 &effective,
                                 &error)
                && error.contains(QStringLiteral("at most 20")))
        << "enforce declared numeric ranges";
}

TEST_F(ThemeOptionsTest, UnknownEnumValueIsRejected) {
    EXPECT_TRUE(
        !validateOptions(catalog,
                         QStringLiteral("options"),
                         {{QStringLiteral("mode"), QStringLiteral("wide")}},
                         &effective,
                         &error)
        && error.contains(QStringLiteral("rows, compact")))
        << "enforce declared enum values";
}

// A live view showing the bundled default theme, over a catalog of the shipped
// examples and three themes written here: `good` declares one option, the
// root of `badroot` is not an item and `syntax` does not compile.
//
// Every case starts from this state, so none depends on what another left in
// the view, the history or the config.
class ThemeRuntimeTest : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(userDirectory.isValid() && hostDirectory.isValid())
            << "create isolated theme roots";
        const QString userRoot = userDirectory.path();
        const QByteArray gapDeclaration =
            QByteArrayLiteral("[options.gap]\ntype='integer'\ndefault="
                              "5\nminimum=0\nmaximum=20\n");
        themeDirectory(userRoot,
                       QStringLiteral("good"),
                       manifest(QByteArrayLiteral("good"), gapDeclaration),
                       optionQml());
        themeDirectory(userRoot,
                       QStringLiteral("badroot"),
                       manifest(QByteArrayLiteral("badroot")),
                       QByteArrayLiteral("import QtQml\nQtObject {}\n"));
        themeDirectory(userRoot,
                       QStringLiteral("syntax"),
                       manifest(QByteArrayLiteral("syntax")),
                       QByteArrayLiteral("import QtQuick\nItem { broken\n"));
        copyExample(QStringLiteral("text-held"),
                    {
                        QStringLiteral("theme.toml"),
                        QStringLiteral("Main.qml"),
                        QStringLiteral("Keycap.qml"),
                        QStringLiteral("RibbonGroup.qml"),
                        QStringLiteral("RibbonStage.qml"),
                    });
        copyExample(QStringLiteral("keycaps"),
                    {
                        QStringLiteral("theme.toml"),
                        QStringLiteral("Main.qml"),
                        QStringLiteral("KeycapEntry.qml"),
                        QStringLiteral("KeycapPresentation.qml"),
                    });
        ASSERT_TRUE(discover(catalog, nullptr, &error))
            << "discover themes before QML loading test: "
            << error.toStdString();
        ASSERT_TRUE(hostLoaded) << "load a simple offscreen QML host";

        config.width = 600;
        config.height = 120;
        config.dynamicSize = true;
        config.minWidth = 160;
        config.minHeight = 50;
        config.panelVisibility = PanelVisibility::WithContent;
        config.backgroundOpacity = 0.0;
        config.panelBorderWidth = 2;
        config.panelBorderColor = QStringLiteral("#80ffffff");
        ASSERT_TRUE(loadInitial(runtime, config, &error))
            << "load the bundled default as a real QML component: "
            << error.toStdString();
        ASSERT_EQ(runtime.activeThemeId(), QStringLiteral("builtin:default"))
            << "bundled default is active initially";
        view.show();
        QCoreApplication::processEvents();
    }

    // Copies a shipped example into the user root, where it has to work as a
    // package of its own.
    void copyExample(const QString& id, const QStringList& files) const {
        const QString source = QDir(sampleThemes).filePath(id);
        const QString copy = QDir(userDirectory.path()).filePath(id);
        ASSERT_TRUE(QDir().mkpath(copy))
            << "create isolated user theme package";
        for (const QString& file : files) {
            ASSERT_TRUE(QFile::copy(QDir(source).filePath(file),
                                    QDir(copy).filePath(file)))
                << "copy " << file.toStdString() << " of " << id.toStdString();
        }
    }

    bool loadHost() {
        const QString hostPath =
            hostDirectory.filePath(QStringLiteral("Host.qml"));
        if (!writeFile(hostPath,
                       QByteArrayLiteral("import QtQuick\nItem { width: 600; "
                                         "height: 120 }\n"))) {
            return false;
        }
        view.resize(600, 120);
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl::fromLocalFile(hostPath));
        return view.status() == QQuickView::Ready
            && view.rootObject() != nullptr;
    }

    void connectKeyboard(hyprcast::keyboard_id id, const QString& name) {
        presenter.processMessage(KeyboardSnapshotMessage {
            .id = id,
            .name = name,
            .keymap = testKeymap(),
        });
    }

    [[nodiscard]] QQuickItem* findItem(const QString& name) const {
        return view.rootObject()->findChild<QQuickItem*>(
            name,
            Qt::FindChildrenRecursively);
    }

    // Brings bindings, deferred deletes and the layout of the model views to
    // rest, so that a test asserts final geometry and never waits on a timer.
    void settle() const {
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            for (const auto& name : {
                     QStringLiteral("cascadeModelView"),
                     QStringLiteral("ribbonModelView"),
                 }) {
                for (auto* modelView :
                     view.rootObject()->findChildren<QQuickItem*>(name)) {
                    QMetaObject::invokeMethod(modelView, "forceLayout");
                }
            }
        }
    }

    QTemporaryDir userDirectory;
    QTemporaryDir hostDirectory;
    QString sampleThemes = QDir(QStringLiteral(HYPRCAST_OVERLAY_SOURCE_DIR))
                               .filePath(QStringLiteral("examples/themes"));
    ThemeCatalog catalog {
        {userDirectory.path(), sampleThemes},
        QUrl::fromLocalFile(
            QDir(QStringLiteral(HYPRCAST_OVERLAY_SOURCE_DIR))
                .filePath(QStringLiteral("qml/themes/default/theme.toml")))
    };
    QQuickView view;
    // Before the runtime, which attaches to the root object of the view.
    bool hostLoaded = loadHost();
    KeyboardPresenter presenter;
    ThemeRuntime runtime {catalog, view, presenter};
    OverlayConfig config;
    QString error;
    // Markup and an emoji: text a theme must show as typed.
    InterpretedAction unicodeText = textAction(QStringLiteral("<b>🙂</b>"));
    InterpretedAction ctrlC =
        chordAction(QStringLiteral("C"), {QStringLiteral("Ctrl")});
    InterpretedAction backspaceKey = keyAction(QStringLiteral("Backspace"));
};

class DefaultThemeTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        panelFrame = findItem(QStringLiteral("hyprcastPanelFrame"));
        panelFill = findItem(QStringLiteral("hyprcastPanelBackground"));
        panelBorder = findItem(QStringLiteral("hyprcastPanelBorder"));
        ASSERT_TRUE(panelFrame != nullptr && panelFill != nullptr
                    && panelBorder != nullptr)
            << "bundled theme exposes its panel frame, fill, and border to the "
               "offscreen scene";
    }

    QQuickItem* panelFrame = nullptr;
    QQuickItem* panelFill = nullptr;
    QQuickItem* panelBorder = nullptr;
};

TEST_F(DefaultThemeTest, DynamicPanelFollowsItsContent) {
    const qreal emptyWidth = panelFrame->width();
    EXPECT_TRUE(emptyWidth >= config.minWidth && emptyWidth < config.width
                && !panelFill->isVisible())
        << "dynamic empty panel starts at its minimum and with-content "
           "hides only its decoration";
    InterpretedAction longText;
    longText.kind = InterpretedActionKind::Text;
    longText.text = QString(80, QLatin1Char('a'));
    presenter.historyModel().apply(longText);
    QCoreApplication::processEvents();
    auto* tailText = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastTailText"),
        Qt::FindChildrenRecursively);
    EXPECT_TRUE(tailText != nullptr && tailText->x() < 0
                && tailText->x() + tailText->width()
                    <= tailText->parentItem()->width() + 1)
        << "overflowing rich text follows newest characters rather than "
           "clipping the tail";
    const qreal fullWidth = panelFrame->width();
    const QColor fillColor = panelFill->property("color").value<QColor>();
    EXPECT_TRUE(qFuzzyCompare(fullWidth, static_cast<qreal>(config.width))
                && panelFill->isVisible() && panelBorder->isVisible()
                && qFuzzyIsNull(fillColor.alphaF()))
        << "measured history grows to the maximum while a visible border "
           "remains independent of transparent fill opacity";
    InterpretedAction backspace;
    backspace.kind = InterpretedActionKind::Key;
    backspace.key = QStringLiteral("Backspace");
    for (int i = 0; i < 60; ++i) {
        presenter.historyModel().apply(backspace);
    }
    QCoreApplication::processEvents();
    EXPECT_TRUE(panelFrame->width() < fullWidth)
        << "deleting content shrinks the dynamic panel and recovers "
           "viewport space";
    EXPECT_TRUE(tailText != nullptr && qFuzzyIsNull(tailText->x()))
        << "shortened text returns to the start of the viewport";
}

TEST_F(DefaultThemeTest, PanelVisibilityHidesOnlyTheDecoration) {
    presenter.historyModel().apply(textAction(QString(20, QLatin1Char('a'))));
    QCoreApplication::processEvents();
    config.panelVisibility = PanelVisibility::Never;
    runtime.applyAcceptedConfiguration(config);
    QCoreApplication::processEvents();
    EXPECT_TRUE(!panelFill->isVisible()
                && presenter.historyModel().rowCount() == 1)
        << "never suppresses the outer fill while preserving visible "
           "semantic input";
    presenter.historyModel().clear();
    config.panelVisibility = PanelVisibility::WithContent;
    runtime.applyAcceptedConfiguration(config);
    QCoreApplication::processEvents();
    EXPECT_FALSE(panelFill->isVisible())
        << "with-content hides the panel decoration after history is cleared";
}

TEST_F(DefaultThemeTest, HeldKeysAloneChangeNothing) {
    const qreal emptyWidth = panelFrame->width();
    connectKeyboard(1, QStringLiteral("test"));
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 1,
        .keycode = 42,
        .pressed = true,
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(presenter.heldKeyCount() == 1 && !panelFill->isVisible()
                && qFuzzyCompare(panelFrame->width(), emptyWidth)
                && !hasVisualText(view.rootObject(), QStringLiteral("Shift")))
        << "default ignores held-only state for visibility, geometry, and "
           "content";
}

// The keycaps example, copied into the user root, with counted repeats and
// symbols configured.
class KeycapsThemeTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        config.themeId = QStringLiteral("keycaps");
        config.height = 60;
        config.themeOptions = {
            {QStringLiteral("height"), 44},
            {QStringLiteral("border_width"), 0},
        };
        ASSERT_TRUE(prepareSwitch(runtime, config, &error))
            << "load keycaps through ordinary theme discovery: "
            << error.toStdString();
        config.repeatPresentation = RepeatPresentation::Counted;
        config.repeatCountThreshold = 2;
        config.symbolFontFamily = QStringLiteral("Symbols Nerd Font");
        config.keySymbols = {{QStringLiteral("C"), QStringLiteral("COPY")}};
        config.modifierSymbols = {
            {QStringLiteral("Ctrl"), QStringLiteral("CTRL")},
        };
        historyOptions.presentation.countedRepeats = true;
        historyOptions.presentation.repeatThreshold = 2;
        historyOptions.presentation.symbolFontFamily = config.symbolFontFamily;
        historyOptions.presentation.spaceSymbol = QStringLiteral("␣");
        historyOptions.presentation.keySymbols = config.keySymbols;
        historyOptions.presentation.modifierSymbols = config.modifierSymbols;
        presenter.setHistoryOptions(historyOptions);
        runtime.applyAcceptedConfiguration(config);
        capPanel = findItem(QStringLiteral("hyprcastKeycapsPanelFrame"));
        ASSERT_NE(capPanel, nullptr)
            << "keycaps controls its own visible panel";
    }

    // Ctrl+C and one generated repeat of it, which the counted presentation
    // shows as one chord with a badge. Returns the repeat.
    InterpretedAction applyRepeatedChord() {
        InterpretedAction chord;
        chord.kind = InterpretedActionKind::Chord;
        chord.keyboardId = 1;
        chord.keycode = 46;
        chord.key = QStringLiteral("C");
        chord.modifiers = {QStringLiteral("Ctrl")};
        presenter.historyModel().apply(chord);
        chord.repeated = true;
        chord.repeatCount = 1;
        presenter.historyModel().apply(chord);
        QCoreApplication::processEvents();
        return chord;
    }

    void checkCapGeometry(qreal expectedHeight, qreal expectedDepth) {
        // Positioners polish asynchronously; flush nested rows explicitly
        // instead of relying on a render frame or an incidental event
        // delay.
        polishVisualTree(view.rootObject());
        polishVisualTree(view.rootObject());
        QList<QQuickItem*> caps;
        QList<QQuickItem*> pending {view.rootObject()};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->objectName() == QStringLiteral("hyprcastKeycap")) {
                caps.append(item);
            }
            pending.append(item->childItems());
        }
        EXPECT_EQ(caps.size(), 2)
            << "counted chord retains separate modifier and key caps";
        for (auto* cap : caps) {
            auto* face = cap->findChild<QQuickItem*>(
                QStringLiteral("hyprcastKeycapFace"));
            if (capPanel) {
                const auto bounds = cap->mapRectToItem(
                    capPanel,
                    QRectF(0, 0, cap->width(), cap->height()));
                EXPECT_TRUE(bounds.top() >= 0
                            && bounds.bottom() <= capPanel->height()
                            && capPanel->height() <= 60)
                    << "raised caps fit vertically inside a tight 60-pixel "
                       "panel";
            }
            EXPECT_TRUE(
                face && qFuzzyCompare(cap->height(), expectedHeight)
                && qFuzzyCompare(face->height(), expectedHeight - expectedDepth)
                && qFuzzyIsNull(face->y())
                && qFuzzyCompare(face->width(), cap->width()))
                << "raised cap face and lower edge stay inside declared "
                   "geometry";
            if (face) {
                auto* border = face->property("border").value<QObject*>();
                EXPECT_TRUE(
                    face->property("radius").toReal() <= face->height() / 2
                    && border
                    && border->property("width").toReal() <= face->height() / 2)
                    << "cap radius and border are bounded by face geometry";
            }
        }
    }

    InputHistoryOptions historyOptions;
    QQuickItem* capPanel = nullptr;
};

TEST_F(KeycapsThemeTest, PanelIsAtLeastTheMinimumHeight) {
    EXPECT_GE(capPanel->height(), config.minHeight)
        << "keycaps controls its own visible panel";
}

TEST_F(KeycapsThemeTest, ShowsTheSpaceSymbolForALiteralSpace) {
    InterpretedAction space;
    space.kind = InterpretedActionKind::Text;
    space.text = QStringLiteral(" ");
    presenter.historyModel().apply(space);
    QCoreApplication::processEvents();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("␣"))
                && presenter.historyModel().displayText()
                    == QStringLiteral(" "))
        << "keycaps displays the mapped space while retaining a literal "
           "input space";
}

TEST_F(KeycapsThemeTest, RendersChordsWithARepeatBadge) {
    applyRepeatedChord();
    EXPECT_TRUE(presenter.historyModel().presentationModel().displayText()
                    == QStringLiteral("CTRL+COPY…2x ")
                && capPanel != nullptr
                && hasVisualText(view.rootObject(), QStringLiteral("CTRL"))
                && hasVisualText(view.rootObject(), QStringLiteral("COPY"))
                && hasVisualText(view.rootObject(), QStringLiteral("…2x")))
        << "copied keycaps QML renders resolved chords and counted-repeat "
           "badges";
}

TEST_F(KeycapsThemeTest, CapGeometryFollowsItsOptions) {
    applyRepeatedChord();
    checkCapGeometry(44, 3);
    const QString keycapsCapture =
        qEnvironmentVariable("HYPRCAST_KEYCAPS_SCREENSHOT");
    if (!keycapsCapture.isEmpty()) {
        tests::runEventLoopFor(100);
        EXPECT_TRUE(view.grabWindow().save(keycapsCapture))
            << "save optional keycaps rendering artifact";
    }
    config.themeOptions.insert(QStringLiteral("edge_depth"), 0);
    runtime.applyAcceptedConfiguration(config);
    QCoreApplication::processEvents();
    checkCapGeometry(44, 0);
    config.themeOptions = {
        {QStringLiteral("height"), 1},
        {QStringLiteral("edge_depth"), 24},
        {QStringLiteral("radius"), 256},
        {QStringLiteral("border_width"), 64},
    };
    runtime.applyAcceptedConfiguration(config);
    QCoreApplication::processEvents();
    checkCapGeometry(1, 0.5);
    config.themeOptions = {
        {QStringLiteral("height"), 44},
        {QStringLiteral("border_width"), 0},
    };
    runtime.applyAcceptedConfiguration(config);
    QCoreApplication::processEvents();
    checkCapGeometry(44, 3);
}

TEST_F(KeycapsThemeTest, SymbolReloadReachesExistingAndNewCaps) {
    const InterpretedAction chord = applyRepeatedChord();
    presenter.historyModel().clear();
    config.minWidth = config.width;
    runtime.applyAcceptedConfiguration(config);

    // Reproduce live symbol reload, then append fresh actions. Inspect
    // actual QML text items as well as the model: a correct projection
    // alone is insufficient.
    historyOptions.backspaceMode = BackspaceMode::Symbol;
    historyOptions.presentation.countedRepeats = false;
    historyOptions.presentation.keySymbols = {
        {QStringLiteral("Backspace"), QStringLiteral("ERASE")},
        {QStringLiteral("Enter"), QStringLiteral("RETURN")},
    };
    presenter.setHistoryOptions(historyOptions);
    InterpretedAction special;
    special.kind = InterpretedActionKind::Key;
    special.key = QStringLiteral("Backspace");
    presenter.historyModel().apply(special);
    QCoreApplication::processEvents();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("ERASE")))
        << "keycap renders the initial symbol mapping";

    historyOptions.presentation.keySymbols = {
        {QStringLiteral("Backspace"), QStringLiteral("⌫")},
        {QStringLiteral("Enter"), QStringLiteral("↵")},
    };
    historyOptions.presentation.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("⌃")},
        {QStringLiteral("Shift"), QStringLiteral("⇧")},
    };
    presenter.setHistoryOptions(historyOptions);
    QCoreApplication::processEvents();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("⌫")))
        << "symbol reload updates existing keycaps";
    special.key = QStringLiteral("Enter");
    presenter.historyModel().apply(special);
    presenter.historyModel().apply(chord);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("↵")))
        << "new special-key keycaps use the reloaded symbol mapping";
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("⌃")))
        << "new chord keycaps use the reloaded modifier mapping";
    special.key = QStringLiteral("Shift");
    presenter.historyModel().apply(special);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("⇧")))
        << "new standalone modifier keycaps use the reloaded modifier "
           "mapping";
    EXPECT_TRUE(
        presenter.historyModel().presentationModel().displayText().endsWith(
            QStringLiteral("⇧ "))
        && presenter.historyModel().entries().back().action.key
            == QStringLiteral("Shift"))
        << "standalone modifier projection resolves glyphs without changing "
           "canonical identity";
}

TEST_F(KeycapsThemeTest, ExpiredHistoryFadesAsASnapshot) {
    historyOptions.backspaceMode = BackspaceMode::Symbol;
    historyOptions.presentation.countedRepeats = false;
    historyOptions.presentation.keySymbols = {
        {QStringLiteral("Backspace"), QStringLiteral("⌫")},
    };
    presenter.setHistoryOptions(historyOptions);
    presenter.historyModel().apply(backspaceKey);
    QCoreApplication::processEvents();
    const auto expirationStart = KeyboardPresenter::Clock::now();
    presenter.setExpiration(1, 250, expirationStart);
    presenter.advance(expirationStart + std::chrono::milliseconds(2));
    QCoreApplication::processEvents();
    EXPECT_TRUE(presenter.fading() && presenter.historyModel().rowCount() == 0
                && hasVisualText(view.rootObject(), QStringLiteral("⌫")))
        << "keycaps renders the fading snapshot after editable history "
           "expires";
}

// What the shipped examples declare about their own options. Only the catalog
// decides these.
using ExampleThemeOptionsTest = ThemeRuntimeTest;

TEST_F(ExampleThemeOptionsTest, KeycapsBoundTheirHeightAndEdgeDepth) {
    QVariantMap capOptions;
    EXPECT_TRUE(validateOptions(catalog,
                                QStringLiteral("keycaps"),
                                {{QStringLiteral("height"), 45}},
                                &capOptions,
                                &error)
                && capOptions.value(QStringLiteral("height")).toInt() == 45)
        << "keycaps validates declared height option";
    EXPECT_FALSE(validateOptions(catalog,
                                 QStringLiteral("keycaps"),
                                 {{QStringLiteral("height"), -1}},
                                 &capOptions,
                                 &error))
        << "keycaps rejects invalid height option";
    EXPECT_FALSE(validateOptions(catalog,
                                 QStringLiteral("keycaps"),
                                 {{QStringLiteral("edge_depth"), -1}},
                                 &capOptions,
                                 &error))
        << "keycaps rejects negative edge depth";
    EXPECT_FALSE(validateOptions(catalog,
                                 QStringLiteral("keycaps"),
                                 {{QStringLiteral("edge_depth"), 25}},
                                 &capOptions,
                                 &error))
        << "keycaps rejects excessive edge depth";
}

TEST_F(ExampleThemeOptionsTest, TextHeldRejectsInvalidAndRemovedOptions) {
    QVariantMap heldOptions;
    EXPECT_TRUE(validateOptions(catalog,
                                QStringLiteral("text-held"),
                                {{QStringLiteral("height"), 31}},
                                &heldOptions,
                                &error)
                && heldOptions.value(QStringLiteral("height")).toInt() == 31)
        << "held example declares its own style options";
    EXPECT_TRUE(!validateOptions(catalog,
                                 QStringLiteral("text-held"),
                                 {{QStringLiteral("height"), -1}},
                                 &heldOptions,
                                 &error)
                && !validateOptions(catalog,
                                    QStringLiteral("text-held"),
                                    {{QStringLiteral("show_held_keys"),
                                      QStringLiteral("true")}},
                                    &heldOptions,
                                    &error))
        << "held example rejects invalid theme-local options";
    EXPECT_TRUE(
        !validateOptions(
            catalog,
            QStringLiteral("text-held"),
            {{QStringLiteral("layout"), QStringLiteral("floating")}},
            &heldOptions,
            &error)
        && !validateOptions(catalog,
                            QStringLiteral("text-held"),
                            {{QStringLiteral("compact_held_fraction"), 0.9}},
                            &heldOptions,
                            &error))
        << "held layout enums and proportions reject invalid values";
    EXPECT_TRUE(!validateOptions(catalog,
                                 QStringLiteral("text-held"),
                                 {{QStringLiteral("depth_scale"), 0.9}},
                                 &heldOptions,
                                 &error)
                && !validateOptions(catalog,
                                    QStringLiteral("text-held"),
                                    {{QStringLiteral("depth_opacity"), 0.7}},
                                    &heldOptions,
                                    &error))
        << "removed receding controls are rejected rather than ignored";
}

TEST_F(ExampleThemeOptionsTest, LedgerBoundsItsTextScale) {
    QVariantMap ledgerOptions;
    EXPECT_TRUE(validateOptions(catalog,
                                QStringLiteral("ledger"),
                                {
                                    {QStringLiteral("rail_width"), 0},
                                    {QStringLiteral("inner_padding"), 8},
                                    {QStringLiteral("text_scale"), 1.5},
                                },
                                &ledgerOptions,
                                &error))
        << "ledger validates curated layout options";
    EXPECT_FALSE(validateOptions(catalog,
                                 QStringLiteral("ledger"),
                                 {{QStringLiteral("text_scale"), 0.0}},
                                 &ledgerOptions,
                                 &error))
        << "ledger rejects unreadable text scales";
}

// Cascade uses a virtualized model view but repositions its visual groups
// independently, so the fixture turns motion off: the tests assert final
// geometry, not timers.
class CascadeThemeTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        config.themeId = QStringLiteral("cascade");
        config.width = 420;
        config.height = 320;
        config.dynamicSize = false;
        config.themeOptions = {
            {QStringLiteral("motion"), QStringLiteral("reduced")},
        };
        view.resize(420, 320);
        ASSERT_TRUE(prepareSwitch(runtime, config, &error))
            << "load Cascade through the normal catalog: "
            << error.toStdString();
        runtime.applyAcceptedConfiguration(config);
        settle();
        cascade = view.rootObject()->findChild<QQuickItem*>(
            QStringLiteral("cascadeActiveStage"));
        ASSERT_NE(cascade, nullptr) << "Cascade exposes its active stage";
    }

    [[nodiscard]] int groupCount() const {
        int count = 0;
        for (auto* child : cascade->childItems()) {
            if (child->objectName() == QStringLiteral("cascadeGroup")) {
                ++count;
            }
        }
        return count;
    }

    QQuickItem* cascade = nullptr;
};

TEST_F(CascadeThemeTest, StacksGroupsInsideItsStage) {
    EXPECT_EQ(cascade->property("capacity").toInt(), 6)
        << "Cascade fits six groups in the demo surface";
    presenter.historyModel().apply(unicodeText);
    presenter.historyModel().apply(ctrlC);
    settle();
    EXPECT_TRUE(hasVisualText(view.rootObject(), unicodeText.text)
                && hasVisualText(view.rootObject(), QStringLiteral("C")))
        << "Cascade preserves Unicode text and separate chord labels";
    int shown = 0;
    for (auto* child : cascade->childItems()) {
        if (child->objectName() != QStringLiteral("cascadeGroup")
            || !child->property("inStack").toBool()) {
            continue;
        }
        ++shown;
        EXPECT_TRUE(child->y() >= 0
                    && child->y() + child->height() <= cascade->height())
            << "Cascade group stays within stage bounds";
        const QRectF visual =
            child->mapRectToItem(cascade,
                                 QRectF(0, 0, child->width(), child->height()));
        EXPECT_TRUE(visual.left() >= -0.5
                    && visual.right() <= cascade->width() + 0.5)
            << "Cascade transformed chord fits the available width";
        if (child->property("depth").toInt() == 0) {
            EXPECT_TRUE(
                qFuzzyCompare(child->y() + child->height(), cascade->height()))
                << "Cascade newest group is bottom anchored";
        }
    }
    EXPECT_EQ(shown, 2) << "Cascade displays one group per projected action";
    // Optional software-rendered inspection artifact; ordinary assertions
    // above do not wait for animation or depend on a screenshot.
    const QString cascadeCapture =
        qEnvironmentVariable("HYPRCAST_CASCADE_SCREENSHOT");
    if (!cascadeCapture.isEmpty()) {
        for (const QString& key : {
                 QStringLiteral("Enter"),
                 QStringLiteral("Tab"),
                 QStringLiteral("Escape"),
                 QStringLiteral("Space"),
             }) {
            InterpretedAction cap;
            cap.kind = InterpretedActionKind::Key;
            cap.key = key;
            presenter.historyModel().apply(cap);
        }
        settle();
        tests::runEventLoopFor(200);
        EXPECT_TRUE(view.grabWindow().save(cascadeCapture))
            << "save optional Cascade rendering artifact";
    }
}

TEST_F(CascadeThemeTest, RenderingStaysBoundedWithLargeHistory) {
    presenter.historyModel().apply(unicodeText);
    presenter.historyModel().apply(ctrlC);
    for (int i = 0; i < 200; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    settle();
    EXPECT_LE(groupCount(), 10)
        << "Cascade rendering stays bounded with large history";
}

TEST_F(CascadeThemeTest, DepthOptionsAndRetentionKeepTheNewestAnchor) {
    presenter.historyModel().apply(unicodeText);
    for (int i = 0; i < 201; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    config.themeOptions = {
        {QStringLiteral("motion"), QStringLiteral("reduced")},
        {QStringLiteral("visible_groups"), 3},
        {QStringLiteral("group_alignment"), QStringLiteral("left")},
    };
    runtime.applyAcceptedConfiguration(config);
    settle();
    EXPECT_TRUE(cascade != nullptr
                && cascade->property("capacity").toInt() == 3)
        << "Cascade live depth settings update capacity";
    InputHistoryOptions trimmedCascade;
    trimmedCascade.maxRetainedUtf16CodeUnits = 48;
    presenter.setHistoryOptions(trimmedCascade);
    settle();
    for (auto* child : cascade->childItems()) {
        if (child->objectName() == QStringLiteral("cascadeGroup")
            && child->property("depth").toInt() == 0) {
            EXPECT_TRUE(
                qFuzzyCompare(child->y() + child->height(), cascade->height()))
                << "Cascade retention trimming preserves newest anchor";
        }
    }
}

TEST_F(CascadeThemeTest, FullMotionBurstKeepsDelegatesBounded) {
    config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("full")}};
    runtime.applyAcceptedConfiguration(config);
    for (int i = 0; i < 80; ++i) {
        presenter.historyModel().apply(ctrlC);
        settle();
    }
    EXPECT_LE(groupCount(), 12)
        << "Cascade full-motion bursts do not retain an unbounded transition "
           "queue";
}

TEST_F(CascadeThemeTest, CountedRepeatsUpdateOneBadge) {
    InputHistoryOptions cascadeOptions;
    cascadeOptions.presentation.countedRepeats = true;
    cascadeOptions.presentation.repeatThreshold = 3;
    presenter.setHistoryOptions(cascadeOptions);
    for (int i = 0; i < 5; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    settle();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("×5")))
        << "Cascade counted repeats use one updating badge";
    presenter.historyModel().apply(backspaceKey);
    settle();
    EXPECT_TRUE(hasVisualText(view.rootObject(), QStringLiteral("×4")))
        << "Cascade Backspace updates the projected repeat group";
}

TEST_F(CascadeThemeTest, ExpiredHistoryMovesToItsOwnStage) {
    InputHistoryOptions cascadeOptions;
    cascadeOptions.presentation.countedRepeats = true;
    cascadeOptions.presentation.repeatThreshold = 3;
    presenter.setHistoryOptions(cascadeOptions);
    for (int i = 0; i < 5; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    settle();
    presenter.historyModel().apply(backspaceKey);
    settle();
    const auto cascadeExpiration = KeyboardPresenter::Clock::now();
    presenter.setExpiration(1, 250, cascadeExpiration);
    presenter.advance(cascadeExpiration + std::chrono::milliseconds(2));
    settle();
    auto* expiredCascade = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("cascadeExpiredStage"));
    EXPECT_TRUE(cascade != nullptr && !cascade->isVisible()
                && expiredCascade != nullptr && expiredCascade->isVisible()
                && hasVisualText(expiredCascade, QStringLiteral("×4")))
        << "Cascade transfers expiration to the supplied snapshot";
    presenter.historyModel().apply(unicodeText);
    settle();
    EXPECT_TRUE(cascade != nullptr && cascade->isVisible()
                && expiredCascade != nullptr && !expiredCascade->isVisible())
        << "Cascade never layers active and expired stacks on fresh input";
    presenter.clearHistory();
    presenter.setExpiration(1, 0, cascadeExpiration);
    presenter.historyModel().apply(unicodeText);
    presenter.advance(cascadeExpiration + std::chrono::milliseconds(3));
    settle();
    EXPECT_TRUE(!presenter.fading() && expiredCascade != nullptr
                && !expiredCascade->isVisible())
        << "zero-duration Cascade expiration leaves no snapshot";
}

TEST_F(CascadeThemeTest, SmallSurfacesReduceTheCapacity) {
    config.height = 70;
    view.resize(420, 70);
    runtime.applyAcceptedConfiguration(config);
    settle();
    EXPECT_TRUE(cascade != nullptr
                && cascade->property("capacity").toInt() == 1)
        << "short Cascade surface prioritizes one readable group";
    config.height = 10;
    view.resize(420, 10);
    runtime.applyAcceptedConfiguration(config);
    settle();
    EXPECT_TRUE(cascade != nullptr && qFuzzyIsNull(cascade->height())
                && cascade->property("capacity").toInt() == 0)
        << "too-small Cascade surface safely zero-sizes the stage";
}

// The text-held example, copied into the user root, with one keyboard
// connected. It starts in the dynamic layout.
class TextHeldThemeTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        connectKeyboard(1, QStringLiteral("test"));
        config.themeId = QStringLiteral("text-held");
        ASSERT_TRUE(prepareSwitch(runtime, config, &error))
            << "load standalone held-key example through normal theme "
               "discovery: "
            << error.toStdString();
        runtime.applyAcceptedConfiguration(config);
        heldPanel = findItem(QStringLiteral("hyprcastPanelFrame"));
        heldFill = findItem(QStringLiteral("hyprcastPanelBackground"));
        QCoreApplication::processEvents();
        ASSERT_TRUE(heldPanel != nullptr && heldFill != nullptr)
            << "held example exposes its panel frame and fill";
    }

    // Switches to the fixed layout: the history ribbon in its own band above
    // the modifier dock, with motion off.
    void showRibbon() {
        config.dynamicSize = false;
        config.height = 140;
        config.width = 600;
        view.resize(600, 140);
        config.themeOptions = {
            {QStringLiteral("motion"), QStringLiteral("reduced")},
            {QStringLiteral("show_altgr"), true},
        };
        runtime.applyAcceptedConfiguration(config);
        settle();
        ribbon = view.rootObject()->findChild<QQuickItem*>(
            QStringLiteral("textHeldActiveStage"));
        expiredRibbon = view.rootObject()->findChild<QQuickItem*>(
            QStringLiteral("textHeldExpiredStage"));
        ASSERT_TRUE(ribbon != nullptr && expiredRibbon != nullptr)
            << "held example exposes its active and expired ribbon stages";
    }

    // What holds for the ribbon whatever the history is.
    void checkRibbon() const {
        int instantiated = 0;
        int newest = 0;
        QList<QRectF> rectangles;
        for (auto* group : ribbon->childItems()) {
            if (group->objectName() != QStringLiteral("ribbonGroup")) {
                continue;
            }
            ++instantiated;
            const QRectF visual = group->mapRectToItem(
                ribbon,
                QRectF(0, 0, group->width(), group->height()));
            if (group->property("depth").toInt() == 0) {
                ++newest;
                EXPECT_TRUE(qAbs(visual.right() - ribbon->width()) < 0.5)
                    << "newest ribbon group stays right anchored";
                EXPECT_TRUE(visual.left() >= -0.5)
                    << "newest long chord fits bounded viewport";
            }
            if (group->width() <= ribbon->width()) {
                EXPECT_TRUE(qFuzzyCompare(group->scale(), 1.0))
                    << "ribbon age never shrinks a fitting group";
            }
            if (group->opacity() > 0) {
                const qreal fadeWidth =
                    ribbon->property("edgeFadeWidth").toReal();
                if (group->x() >= fadeWidth) {
                    EXPECT_TRUE(qFuzzyCompare(group->opacity(), 1.0))
                        << "ribbon remains fully opaque away from the far left "
                           "edge";
                }
                EXPECT_TRUE(visual.left() >= -0.5
                            && visual.right() <= ribbon->width() + 0.5
                            && visual.top() >= -0.5)
                    << "visible ribbon geometry stays inside the reserved band";
                for (const auto& other : rectangles) {
                    EXPECT_FALSE(visual.intersects(other))
                        << "variable-width fitted groups never collide in "
                           "final "
                           "state";
                }
                rectangles.push_back(visual);
            }
        }
        EXPECT_EQ(newest, 1)
            << "nonempty ribbon realizes exactly one newest projected group";
        EXPECT_TRUE(instantiated <= 16)
            << "ribbon delegates remain bounded independently of retention";
    }

    [[nodiscard]] QQuickItem* findShiftSlot() const {
        return findVisualItem(view.rootObject(),
                              QStringLiteral("modifierSlotShift"));
    }

    QQuickItem* heldPanel = nullptr;
    QQuickItem* heldFill = nullptr;
    QQuickItem* ribbon = nullptr;
    QQuickItem* expiredRibbon = nullptr;
};

TEST_F(TextHeldThemeTest, HeldModifierShowsWithoutAddingHistory) {
    auto* heldHistoryArea = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastHistoryArea"),
        Qt::FindChildrenRecursively);
    EXPECT_NE(heldHistoryArea, nullptr)
        << "held example exposes its reserved history area";
    const qreal historyYBeforeHeld = heldHistoryArea ? heldHistoryArea->y() : 0;
    const qreal historyHeightBeforeHeld =
        heldHistoryArea ? heldHistoryArea->height() : 0;
    const qreal panelHeightBeforeHeld = heldPanel ? heldPanel->height() : 0;
    const int historyBeforeHeld = presenter.historyModel().rowCount();
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 3,
        .keycode = 42,
        .pressed = true,
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(heldPanel != nullptr && heldFill != nullptr
                && heldFill->isVisible()
                && hasVisualText(view.rootObject(),
                                 presenter.heldKeyItems()
                                     .front()
                                     .toMap()
                                     .value(QStringLiteral("label"))
                                     .toString())
                && presenter.historyModel().rowCount() == historyBeforeHeld)
        << "held example reacts to modifier press without adding history";
    EXPECT_TRUE(
        heldHistoryArea != nullptr
        && qFuzzyCompare(heldHistoryArea->y(), historyYBeforeHeld)
        && qFuzzyCompare(heldHistoryArea->height(), historyHeightBeforeHeld)
        && heldPanel != nullptr
        && qFuzzyCompare(heldPanel->height(), panelHeightBeforeHeld))
        << "held press preserves history geometry and panel height";
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 4,
        .keycode = 42,
        .pressed = false,
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(presenter.heldKeyCount() == 0
                && presenter.historyModel().rowCount() == historyBeforeHeld + 1)
        << "modifier release removes held state and records standalone "
           "modifier "
           "history";
    EXPECT_TRUE(
        heldHistoryArea != nullptr
        && qFuzzyCompare(heldHistoryArea->y(), historyYBeforeHeld)
        && qFuzzyCompare(heldHistoryArea->height(), historyHeightBeforeHeld))
        << "held release preserves the reserved history geometry";
    presenter.clearHistory();
    QCoreApplication::processEvents();
    auto* idleDock = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastHeldViewport"));
    auto* idleShift =
        findVisualItem(view.rootObject(), QStringLiteral("modifierSlotShift"));
    EXPECT_TRUE(idleDock != nullptr && !idleDock->isVisible()
                && idleShift != nullptr
                && !idleShift->property("pressed").toBool()
                && heldFill != nullptr && !heldFill->isVisible())
        << "held example hides idle placeholders and decoration after history "
           "clears";
}

TEST_F(TextHeldThemeTest, FixedDockKeepsHistoryInPlace) {
    config.dynamicSize = false;
    config.height = 60;
    for (const auto& alignment : {
             QStringLiteral("left"),
             QStringLiteral("center"),
             QStringLiteral("right"),
         }) {
        config.themeOptions = {
            {QStringLiteral("dock_alignment"), alignment},
            {QStringLiteral("motion"), QStringLiteral("reduced")},
        };
        EXPECT_TRUE(prepareSwitch(runtime, config, &error))
            << "prepare fixed dock alignment";
        runtime.applyAcceptedConfiguration(config);
        settle();
        auto* history = view.rootObject()->findChild<QQuickItem*>(
            QStringLiteral("hyprcastHistoryArea"));
        auto* held = view.rootObject()->findChild<QQuickItem*>(
            QStringLiteral("hyprcastHeldViewport"));
        auto* shift = findVisualItem(view.rootObject(),
                                     QStringLiteral("modifierSlotShift"));
        EXPECT_TRUE(history != nullptr && held != nullptr
                    && history->height() > 0
                    && history->y() + history->height() <= held->y())
            << "short surface retains separate stacked bands";
        if (!history || !held || !shift) {
            continue;
        }
        const QRectF before(history->x(),
                            history->y(),
                            history->width(),
                            history->height());
        const qreal slotX = shift->x();
        presenter.processMessage(KeyMessage {
            .keyboardId = 1,
            .timeMs = 5,
            .keycode = 42,
            .pressed = true,
        });
        settle();
        EXPECT_TRUE(shift->property("pressed").toBool()
                    && qFuzzyCompare(shift->x(), slotX)
                    && before
                        == QRectF(history->x(),
                                  history->y(),
                                  history->width(),
                                  history->height()))
            << "canonical modifier activates fixed slot without moving history";
        presenter.processMessage(KeyMessage {
            .keyboardId = 1,
            .timeMs = 6,
            .keycode = 42,
            .pressed = false,
        });
        settle();
        EXPECT_TRUE(!shift->property("pressed").toBool()
                    && qFuzzyCompare(shift->x(), slotX))
            << "release clears live state while historical modifier remains";
        presenter.clearHistory();
    }
}

TEST_F(TextHeldThemeTest, RibbonKeepsUnicodeAndChordCaps) {
    showRibbon();
    EXPECT_NE(
        findVisualItem(view.rootObject(), QStringLiteral("modifierSlotAltGr")),
        nullptr)
        << "optional canonical AltGr has its own slot";
    presenter.historyModel().apply(unicodeText);
    presenter.historyModel().apply(ctrlC);
    settle();
    EXPECT_TRUE(ribbon != nullptr && hasVisualText(ribbon, unicodeText.text)
                && hasVisualText(ribbon, QStringLiteral("Ctrl"))
                && hasVisualText(ribbon, QStringLiteral("C")))
        << "ribbon preserves Unicode and distinct chord caps";
    checkRibbon();
    const QString ribbonCapture =
        qEnvironmentVariable("HYPRCAST_TEXT_HELD_SCREENSHOT");
    if (!ribbonCapture.isEmpty()) {
        for (const QString& key : {
                 QStringLiteral("Tab"),
                 QStringLiteral("Enter"),
                 QStringLiteral("Escape"),
                 QStringLiteral("Space"),
             }) {
            InterpretedAction cap;
            cap.kind = InterpretedActionKind::Key;
            cap.key = key;
            presenter.historyModel().apply(cap);
        }
        presenter.historyModel().apply(ctrlC);
        presenter.processMessage(KeyMessage {
            .keyboardId = 1,
            .timeMs = 13,
            .keycode = 29,
            .pressed = true,
        });
        settle();
        tests::runEventLoopFor(200);
        EXPECT_TRUE(view.grabWindow().save(ribbonCapture))
            << "save optional text-held rendering artifact";
        presenter.processMessage(KeyMessage {
            .keyboardId = 1,
            .timeMs = 14,
            .keycode = 29,
            .pressed = false,
        });
    }
}

TEST_F(TextHeldThemeTest, OnlyTheFarLeftGroupFades) {
    showRibbon();
    InterpretedAction edgeText;
    edgeText.kind = InterpretedActionKind::Text;
    edgeText.text = QStringLiteral("a");
    for (int i = 0; i < 3; ++i) {
        presenter.historyModel().apply(edgeText);
    }
    settle();
    QQuickItem* edgeGroup = nullptr;
    if (ribbon) {
        for (auto* group : ribbon->childItems()) {
            if (group->objectName() == QStringLiteral("ribbonGroup")
                && group->property("depth").toInt() == 2) {
                edgeGroup = group;
            }
        }
    }
    ASSERT_NE(edgeGroup, nullptr)
        << "edge-fade fixture realizes the oldest of three groups";
    config.width = qRound(config.width - edgeGroup->x() + 5);
    view.resize(config.width, config.height);
    runtime.applyAcceptedConfiguration(config);
    settle();
    const qreal fadeWidth = ribbon->property("edgeFadeWidth").toReal();
    EXPECT_TRUE(
        edgeGroup->x() > 0 && edgeGroup->x() < fadeWidth && fadeWidth <= 12
        && qAbs(edgeGroup->opacity() - (edgeGroup->x() / fadeWidth)) < 0.01
        && qFuzzyCompare(edgeGroup->scale(), 1.0))
        << "only the far-left group fades in a narrow edge zone, without "
           "shrinking";
    checkRibbon();
}

TEST_F(TextHeldThemeTest, RibbonStaysBoundedWithLargeHistory) {
    showRibbon();
    for (int i = 0; i < 200; ++i) {
        presenter.historyModel().apply(i % 2 ? ctrlC : unicodeText);
    }
    settle();
    checkRibbon();
    presenter.historyModel().apply(backspaceKey);
    settle();
    checkRibbon();
}

TEST_F(TextHeldThemeTest, CountedRepeatsUpdateOneBadge) {
    showRibbon();
    InputHistoryOptions ribbonOptions;
    ribbonOptions.presentation.countedRepeats = true;
    ribbonOptions.presentation.repeatThreshold = 3;
    ribbonOptions.presentation.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("same")},
        {QStringLiteral("Shift"), QStringLiteral("same")},
    };
    presenter.clearHistory();
    presenter.setHistoryOptions(ribbonOptions);
    config.modifierSymbols = ribbonOptions.presentation.modifierSymbols;
    runtime.applyAcceptedConfiguration(config);
    for (int i = 0; i < 5; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    settle();
    EXPECT_TRUE(ribbon != nullptr
                && hasVisualText(ribbon, QStringLiteral("×5")))
        << "ribbon updates counted repeat badge in place";
    presenter.historyModel().apply(backspaceKey);
    settle();
    EXPECT_TRUE(ribbon != nullptr
                && hasVisualText(ribbon, QStringLiteral("×4")))
        << "ribbon Backspace decrements counted group";
}

TEST_F(TextHeldThemeTest, SharedSymbolsKeepModifiersDistinct) {
    showRibbon();
    InputHistoryOptions ribbonOptions;
    ribbonOptions.presentation.countedRepeats = true;
    ribbonOptions.presentation.repeatThreshold = 3;
    ribbonOptions.presentation.modifierSymbols = {
        {QStringLiteral("Ctrl"), QStringLiteral("same")},
        {QStringLiteral("Shift"), QStringLiteral("same")},
    };
    presenter.clearHistory();
    presenter.setHistoryOptions(ribbonOptions);
    config.modifierSymbols = ribbonOptions.presentation.modifierSymbols;
    runtime.applyAcceptedConfiguration(config);
    auto* ctrlSlot =
        findVisualItem(view.rootObject(), QStringLiteral("modifierSlotCtrl"));
    auto* shiftSlot = findShiftSlot();
    presenter.processMessage(KeyboardSnapshotMessage {
        .id = 2,
        .name = QStringLiteral("second"),
        .keymap = testKeymap(),
    });
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 10,
        .keycode = 42,
        .pressed = true,
    });
    presenter.processMessage(KeyMessage {
        .keyboardId = 2,
        .timeMs = 10,
        .keycode = 42,
        .pressed = true,
    });
    settle();
    EXPECT_TRUE(shiftSlot != nullptr && shiftSlot->property("pressed").toBool()
                && ctrlSlot != nullptr
                && !ctrlSlot->property("pressed").toBool()
                && shiftSlot->property("label").toString()
                    == ctrlSlot->property("label").toString())
        << "duplicate display symbols do not conflate canonical modifiers";
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 11,
        .keycode = 42,
        .pressed = false,
    });
    settle();
    EXPECT_TRUE(shiftSlot != nullptr && shiftSlot->property("pressed").toBool())
        << "slot stays held while another keyboard observes the modifier";
}

TEST_F(TextHeldThemeTest, ExpirationLeavesTheHeldDockAlone) {
    showRibbon();
    connectKeyboard(2, QStringLiteral("second"));
    presenter.historyModel().apply(ctrlC);
    presenter.processMessage(KeyMessage {
        .keyboardId = 2,
        .timeMs = 10,
        .keycode = 42,
        .pressed = true,
    });
    settle();
    auto* shiftSlot = findShiftSlot();
    const auto ribbonExpiration = KeyboardPresenter::Clock::now();
    presenter.setExpiration(1, 250, ribbonExpiration);
    presenter.advance(ribbonExpiration + std::chrono::milliseconds(2));
    settle();
    EXPECT_TRUE(ribbon != nullptr && !ribbon->isVisible()
                && expiredRibbon != nullptr && expiredRibbon->isVisible()
                && shiftSlot != nullptr
                && shiftSlot->property("pressed").toBool())
        << "expiration transfers ribbon while preserving independently held "
           "dock";
    presenter.historyModel().apply(unicodeText);
    settle();
    EXPECT_TRUE(ribbon != nullptr && ribbon->isVisible()
                && expiredRibbon != nullptr && !expiredRibbon->isVisible())
        << "fresh input never layers both ribbons";
    presenter.processMessage(KeyMessage {
        .keyboardId = 2,
        .timeMs = 12,
        .keycode = 42,
        .pressed = false,
    });
    settle();
    EXPECT_TRUE(shiftSlot != nullptr
                && !shiftSlot->property("pressed").toBool())
        << "final observed release immediately clears slot state";
}

TEST_F(TextHeldThemeTest, PanelAndIdleDockFadeTogether) {
    showRibbon();
    presenter.historyModel().apply(unicodeText);
    settle();
    const auto idleExpiration = KeyboardPresenter::Clock::now();
    presenter.setExpiration(1, 10000, idleExpiration);
    presenter.advance(idleExpiration + std::chrono::milliseconds(2));
    settle();
    auto* fadingDock = findVisualItem(view.rootObject(),
                                      QStringLiteral("hyprcastHeldViewport"));
    for (const PanelVisibility visibility : {
             PanelVisibility::WithContent,
             PanelVisibility::Never,
             PanelVisibility::Always,
         }) {
        config.panelVisibility = visibility;
        runtime.applyAcceptedConfiguration(config);
        settle();
        // Set a deterministic intermediate fade value rather than waiting on
        // animation timing.
        if (expiredRibbon) {
            expiredRibbon->setOpacity(0.4);
        }
        const qreal expectedOpacity =
            visibility == PanelVisibility::Always ? 1.0 : 0.4;
        EXPECT_TRUE(heldFill != nullptr && fadingDock != nullptr
                    && qAbs(heldFill->opacity() - expectedOpacity) < 0.001
                    && qAbs(fadingDock->opacity() - expectedOpacity) < 0.001)
            << "panel and idle dock fade together unless panel visibility is "
               "always";
        EXPECT_TRUE(heldFill != nullptr
                    && heldFill->isVisible()
                        == (visibility != PanelVisibility::Never))
            << "never hides decoration while preserving dock fade behavior";
    }
    config.panelVisibility = PanelVisibility::WithContent;
    runtime.applyAcceptedConfiguration(config);
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 13,
        .keycode = 42,
        .pressed = true,
    });
    settle();
    if (expiredRibbon) {
        expiredRibbon->setOpacity(0.4);
    }
    EXPECT_TRUE(heldFill != nullptr && fadingDock != nullptr
                && qFuzzyCompare(heldFill->opacity(), 1.0)
                && qFuzzyCompare(fadingDock->opacity(), 1.0))
        << "observed held modifier keeps panel and dock opaque during history "
           "fade";
}

TEST_F(TextHeldThemeTest, NarrowSurfaceBoundsLongChordsAndSlots) {
    showRibbon();
    InterpretedAction longChord = ctrlC;
    longChord.key = QString(300, QLatin1Char('W'));
    longChord.modifiers = {
        QStringLiteral("Ctrl"),
        QStringLiteral("Shift"),
        QStringLiteral("Alt"),
        QStringLiteral("Super"),
    };
    presenter.historyModel().apply(longChord);
    config.width = 100;
    config.height = 60;
    view.resize(100, 60);
    runtime.applyAcceptedConfiguration(config);
    settle();
    checkRibbon();
    auto* shiftSlot = findShiftSlot();
    ASSERT_NE(shiftSlot, nullptr) << "held example exposes its Shift slot";
    const QRectF slotRect = shiftSlot->mapRectToItem(
        view.rootObject(),
        QRectF(0, 0, shiftSlot->width(), shiftSlot->height()));
    EXPECT_TRUE(slotRect.left() >= 0 && slotRect.right() <= 100)
        << "narrow dock bounds every fixed slot rather than clipping "
           "arbitrary caps";
}

TEST_F(TextHeldThemeTest, FullMotionBurstKeepsDelegatesBounded) {
    showRibbon();
    presenter.historyModel().apply(unicodeText);
    presenter.historyModel().apply(ctrlC);
    settle();
    config.themeOptions = {{QStringLiteral("motion"), QStringLiteral("full")}};
    runtime.applyAcceptedConfiguration(config);
    for (int i = 0; i < 40; ++i) {
        presenter.historyModel().apply(ctrlC);
        settle();
    }
    int movingGroups = 0;
    if (ribbon) {
        for (auto* group : ribbon->childItems()) {
            if (group->objectName() == QStringLiteral("ribbonGroup")) {
                ++movingGroups;
            }
        }
    }
    EXPECT_TRUE(movingGroups > 0 && movingGroups <= 16)
        << "full-motion input burst keeps delegates bounded without a removal "
           "queue";
}

TEST_F(TextHeldThemeTest, RetentionTrimKeepsTheRibbonConsistent) {
    showRibbon();
    presenter.historyModel().apply(unicodeText);
    for (int i = 0; i < 41; ++i) {
        presenter.historyModel().apply(ctrlC);
    }
    config.themeOptions = {
        {QStringLiteral("motion"), QStringLiteral("reduced")},
        {QStringLiteral("edge_depth"), 0},
    };
    runtime.applyAcceptedConfiguration(config);
    InputHistoryOptions ribbonTrim;
    ribbonTrim.maxRetainedUtf16CodeUnits = 48;
    presenter.setHistoryOptions(ribbonTrim);
    settle();
    checkRibbon();
}

TEST_F(TextHeldThemeTest, ConnectionResetClearsHeldSlots) {
    showRibbon();
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 15,
        .keycode = 42,
        .pressed = true,
    });
    presenter.resetConnection();
    settle();
    auto* shiftSlot = findShiftSlot();
    EXPECT_TRUE(shiftSlot != nullptr
                && !shiftSlot->property("pressed").toBool())
        << "held-list reset immediately clears pressed visuals";
}

TEST_F(TextHeldThemeTest, ZeroFadeExpiresTheRibbonAtOnce) {
    showRibbon();
    const auto immediateRibbonExpiration = KeyboardPresenter::Clock::now();
    presenter.historyModel().apply(unicodeText);
    presenter.setExpiration(1, 0, immediateRibbonExpiration);
    presenter.advance(immediateRibbonExpiration + std::chrono::milliseconds(3));
    settle();
    EXPECT_TRUE(presenter.historyModel().rowCount() == 0 && !presenter.fading()
                && expiredRibbon != nullptr && !expiredRibbon->isVisible())
        << "zero-duration ribbon expiration is immediate";
}

TEST_F(TextHeldThemeTest, TooSmallSurfaceZeroSizesTheBands) {
    showRibbon();
    config.height = 10;
    view.resize(600, 10);
    runtime.applyAcceptedConfiguration(config);
    settle();
    EXPECT_TRUE(ribbon != nullptr && qFuzzyIsNull(ribbon->height())
                && ribbon->property("capacity").toInt() == 0)
        << "impossible short surface safely zero-sizes both bands";
}

// The ledger example, loaded from the examples directory itself.
class LedgerThemeTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        config.themeId = QStringLiteral("ledger");
        config.themeOptions = {
            {QStringLiteral("item_spacing"), 8},
            {QStringLiteral("accent"), QStringLiteral("#40ccaa")},
        };
        ASSERT_TRUE(prepareSwitch(runtime, config, &error))
            << "load the shipped, distinctly composed QML example: "
            << error.toStdString();
        runtime.applyAcceptedConfiguration(config);
    }
};

TEST_F(LedgerThemeTest, ReceivesItsDeclaredOptions) {
    EXPECT_TRUE(
        view.rootObject()->findChild<QQuickItem*>(QStringLiteral("ledger-8"),
                                                  Qt::FindChildrenRecursively)
        != nullptr)
        << "the shipped example receives its declared options through the "
           "public API";
}

TEST_F(LedgerThemeTest, PanelFollowsItsTimeline) {
    auto* ledgerPanel = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastLedgerPanelFrame"),
        Qt::FindChildrenRecursively);
    ASSERT_NE(ledgerPanel, nullptr) << "ledger exposes its dynamic panel frame";
    InterpretedAction line;
    line.kind = InterpretedActionKind::Text;
    for (int i = 0; i < 2; ++i) {
        line.text = QString::number(i);
        presenter.historyModel().apply(line);
    }
    QCoreApplication::processEvents();
    const qreal growingHeight = ledgerPanel->height();
    EXPECT_TRUE(growingHeight > config.minHeight
                && growingHeight < config.height)
        << "ledger grows vertically with its timeline content";
    for (int i = 2; i < 10; ++i) {
        line.text = QString::number(i);
        presenter.historyModel().apply(line);
    }
    QCoreApplication::processEvents();
    const qreal fullHeight = ledgerPanel->height();
    EXPECT_TRUE(qFuzzyCompare(fullHeight, static_cast<qreal>(config.height)))
        << "ledger caps its dynamic height at the configured maximum";
    InterpretedAction backspace;
    backspace.kind = InterpretedActionKind::Key;
    backspace.key = QStringLiteral("Backspace");
    for (int i = 0; i < 8; ++i) {
        presenter.historyModel().apply(backspace);
    }
    QCoreApplication::processEvents();
    EXPECT_TRUE(ledgerPanel->height() < fullHeight)
        << "ledger panel shrinks as Backspace shortens the timeline";
}

// The config manager driving the runtime the way the overlay wires them: a
// reload is checked against the catalog, and a theme switch is prepared
// before the config that asks for it is accepted. The history holds "kept".
class LiveThemeConfigTest : public ThemeRuntimeTest {
protected:
    void SetUp() override {
        ThemeRuntimeTest::SetUp();
        connectKeyboard(1, QStringLiteral("test"));
        presenter.historyModel().apply(textAction(QStringLiteral("kept")));
        ASSERT_TRUE(
            writeFile(configPath,
                      QByteArrayLiteral("[theme]\nid='builtin:default'\n")))
            << "write initial theme config";
        manager.setRuntimeValidator(
            [this](const std::optional<OverlayConfig>& previous,
                   const OverlayConfig& candidate)
                -> std::expected<void, QString> {
                if (auto options =
                        catalog.validateOptions(candidate.themeId,
                                                candidate.themeOptions);
                    !options) {
                    return std::unexpected(options.error());
                }
                if (!previous || previous->themeId == candidate.themeId) {
                    return {};
                }
                return runtime.prepareSwitch(candidate);
            });
        ASSERT_TRUE(manager.initialize().has_value())
            << "initialize config manager with the active bundled theme";
        QObject::connect(
            &manager,
            &OverlayConfigManager::configurationChanged,
            &view,
            [this] { runtime.applyAcceptedConfiguration(manager.config()); });
        QObject::connect(&manager,
                         &OverlayConfigManager::reloadRejected,
                         &view,
                         [this] { ++rejections; });
    }

    // Replaces the config file and has the manager read it.
    void load(const QByteArray& toml) {
        ASSERT_TRUE(writeFile(configPath, toml))
            << "write " << toml.toStdString();
        manager.reloadNow();
    }

    QString configPath = hostDirectory.filePath(QStringLiteral("overlay.toml"));
    OverlayConfigManager manager {configPath, true};
    int rejections = 0;
};

TEST_F(LiveThemeConfigTest, TextHeldOptionsApplyWithoutASwitch) {
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='text-held'\n[theme.options]\nheight=30\n")))
        << "select standalone held feedback via live config";
    manager.reloadNow();
    EXPECT_TRUE(runtime.activeThemeId() == QStringLiteral("text-held")
                && manager.config()
                        .themeOptions.value(QStringLiteral("height"))
                        .toLongLong()
                    == 30)
        << "live config accepts declared cap style";
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='text-held'\n[theme.options]\nheight=32\n")))
        << "update cap style without switching themes";
    manager.reloadNow();
    auto* heldRow = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastHeldViewport"),
        Qt::FindChildrenRecursively);
    presenter.processMessage(KeyMessage {
        .keyboardId = 1,
        .timeMs = 5,
        .keycode = 42,
        .pressed = true,
    });
    QCoreApplication::processEvents();
    EXPECT_TRUE(manager.config()
                        .themeOptions.value(QStringLiteral("height"))
                        .toLongLong()
                    == 32
                && heldRow != nullptr && heldRow->isVisible()
                && heldRow->height() > 0 && heldRow->height() <= 32)
        << "accepted cap style reaches bounded two-band geometry";
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]"
                                    "\nheight=32\nshow_held_keys=false\n")))
        << "hide example held feedback via live theme option";
    manager.reloadNow();
    QCoreApplication::processEvents();
    EXPECT_TRUE(heldRow != nullptr && !heldRow->isVisible()
                && presenter.heldKeyCount() == 1)
        << "example option hides the dock without changing backend held state";
}

TEST_F(LiveThemeConfigTest, InvalidOptionKeepsTheAcceptedTheme) {
    load(QByteArrayLiteral("[theme]\nid='text-held'\n[theme.options]\nheight="
                           "32\nshow_held_keys=false\n"));
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='text-held'\n[theme.options]\nheight=0\n")))
        << "write invalid cap style for live reload";
    manager.reloadNow();
    EXPECT_TRUE(manager.config()
                        .themeOptions.value(QStringLiteral("height"))
                        .toLongLong()
                    == 32
                && !manager.config()
                        .themeOptions.value(QStringLiteral("show_held_keys"))
                        .toBool()
                && runtime.activeThemeId() == QStringLiteral("text-held"))
        << "invalid cap style keeps last accepted theme and options";
}

TEST_F(LiveThemeConfigTest, SwitchKeepsHistoryAndDeliversOptions) {
    EXPECT_TRUE(writeFile(
        configPath,
        QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=12\n")))
        << "write config selecting a valid external theme and option";
    manager.reloadNow();
    EXPECT_TRUE(manager.config().themeId == QStringLiteral("good")
                && runtime.activeThemeId() == QStringLiteral("good"))
        << "a validated live config atomically accepts a prepared theme";
    EXPECT_TRUE(
        view.rootObject()->findChild<QQuickItem*>(QStringLiteral("gap-12"),
                                                  Qt::FindChildrenRecursively)
        != nullptr)
        << "QML receives declared option values and defaults through the "
           "versioned API";
    EXPECT_EQ(presenter.outputText(), QStringLiteral("kept"))
        << "theme switching preserves editable input history";

    EXPECT_TRUE(writeFile(
        configPath,
        QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n")))
        << "write a live update to a declared option";
    manager.reloadNow();
    EXPECT_TRUE(
        manager.config().themeOptions.value(QStringLiteral("gap")).toLongLong()
            == 14
        && view.rootObject()->findChild<QQuickItem*>(
               QStringLiteral("gap-14"),
               Qt::FindChildrenRecursively)
            != nullptr)
        << "accepted option changes notify the active QML theme without "
           "recreating history";
}

TEST_F(LiveThemeConfigTest, MistypedOptionIsRejected) {
    load(QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n"));
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='good'\n[theme.options]\ngap='wrong'\n")))
        << "write a config with a mistyped theme option";
    manager.reloadNow();
    EXPECT_TRUE(rejections == 1
                && manager.config()
                        .themeOptions.value(QStringLiteral("gap"))
                        .toLongLong()
                    == 14
                && view.rootObject()->findChild<QQuickItem*>(
                       QStringLiteral("gap-14"),
                       Qt::FindChildrenRecursively)
                    != nullptr)
        << "invalid options leave accepted settings and the active visual "
           "unchanged";
}

TEST_F(LiveThemeConfigTest, BrokenRootKeepsTheWholeConfig) {
    load(QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n"));
    EXPECT_TRUE(writeFile(
        configPath,
        QByteArrayLiteral("[window]\nwidth=800\n[theme]\nid='badroot'\n")))
        << "write config with a broken root theme and other changes";
    manager.reloadNow();
    EXPECT_TRUE(rejections == 1
                && manager.config().themeId == QStringLiteral("good")
                && manager.config().width == 600
                && manager.config()
                        .themeOptions.value(QStringLiteral("gap"))
                        .toLongLong()
                    == 14)
        << "a rejected theme switch leaves the entire accepted config "
           "unchanged";
    EXPECT_TRUE(runtime.activeThemeId() == QStringLiteral("good")
                && presenter.outputText() == QStringLiteral("kept"))
        << "a rejected root component keeps the old visual theme and history "
           "live";
}

TEST_F(LiveThemeConfigTest, QmlSyntaxErrorKeepsTheAcceptedTheme) {
    load(QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n"));
    EXPECT_TRUE(
        writeFile(configPath, QByteArrayLiteral("[theme]\nid='syntax'\n")))
        << "write config selecting a syntactically broken theme";
    manager.reloadNow();
    EXPECT_TRUE(rejections == 1
                && manager.config().themeId == QStringLiteral("good")
                && runtime.activeThemeId() == QStringLiteral("good"))
        << "QML compilation failure rejects the switch without replacing the "
           "accepted theme";
}

TEST_F(LiveThemeConfigTest, RecoversAfterRejectedSwitches) {
    load(QByteArrayLiteral("[theme]\nid='good'\n[theme.options]\ngap=14\n"));
    load(QByteArrayLiteral("[window]\nwidth=800\n[theme]\nid='badroot'\n"));
    load(QByteArrayLiteral("[theme]\nid='syntax'\n"));
    ASSERT_EQ(rejections, 2) << "both broken themes are rejected first";
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral("[theme]\nid='ledger'\n[theme.options]"
                                    "\nitem_spacing=8\naccent='#40ccaa'\n")))
        << "write a valid config after rejected theme attempts";
    manager.reloadNow();
    EXPECT_TRUE(manager.config().themeId == QStringLiteral("ledger")
                && runtime.activeThemeId() == QStringLiteral("ledger")
                && view.rootObject()->findChild<QQuickItem*>(
                       QStringLiteral("ledger-8"),
                       Qt::FindChildrenRecursively)
                    != nullptr)
        << "a later valid theme config recovers normally after rejected "
           "attempts";
    EXPECT_EQ(presenter.outputText(), QStringLiteral("kept"))
        << "theme switching and recovery preserve input history";
}

TEST_F(LiveThemeConfigTest, KeycapsOptionReachesTheGeometry) {
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='keycaps'\n[theme.options]\nheight=44\n")))
        << "select copied keycaps package through live config";
    manager.reloadNow();
    EXPECT_TRUE(runtime.activeThemeId() == QStringLiteral("keycaps")
                && presenter.outputText() == QStringLiteral("kept"))
        << "switching to standalone keycaps keeps history";
    EXPECT_TRUE(
        writeFile(configPath,
                  QByteArrayLiteral(
                      "[theme]\nid='keycaps'\n[theme.options]\nheight=60\n")))
        << "change live keycaps height";
    manager.reloadNow();
    auto* capFrame = view.rootObject()->findChild<QQuickItem*>(
        QStringLiteral("hyprcastKeycapsPanelFrame"),
        Qt::FindChildrenRecursively);
    EXPECT_TRUE(capFrame != nullptr && capFrame->height() >= 68
                && presenter.outputText() == QStringLiteral("kept"))
        << "live keycaps option affects panel geometry without clearing "
           "history";
}

} // namespace

int main(int argc, char** argv) {
    return tests::run<QGuiApplication>(argc, argv);
}
