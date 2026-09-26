#include "theme/Theme.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickView>
#include <QTemporaryDir>
#include <QUrl>

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

    void testRuntimeLoadingAndSwitchRecovery(const QString& userRoot, const QString& bundledRoot) {
        const QByteArray gapDeclaration = QByteArrayLiteral("[options.gap]\ntype='integer'\ndefault=5\nminimum=0\nmaximum=20\n");
        const QString    goodDirectory  = themeDirectory(userRoot, QStringLiteral("good"), manifest(QByteArrayLiteral("good"), gapDeclaration), optionQml());
        Q_UNUSED(goodDirectory);
        themeDirectory(userRoot, QStringLiteral("badroot"), manifest(QByteArrayLiteral("badroot")), QByteArrayLiteral("import QtQml\nQtObject {}\n"));
        themeDirectory(userRoot, QStringLiteral("syntax"), manifest(QByteArrayLiteral("syntax")), QByteArrayLiteral("import QtQuick\nItem { broken\n"));
        const QString builtinDirectory = themeDirectory(bundledRoot, QStringLiteral("default"), manifest(QByteArrayLiteral("default")), validQml());

        const QString sampleThemes = QDir(QStringLiteral(HYPRCAST_OVERLAY_SOURCE_DIR)).filePath(QStringLiteral("examples/themes"));
        ThemeCatalog  catalog({userRoot, sampleThemes}, QUrl::fromLocalFile(QDir(builtinDirectory).filePath(QStringLiteral("theme.toml"))));
        QString       error;
        check(catalog.discover(nullptr, &error), "discover themes before QML loading test");

        QTemporaryDir hostDirectory;
        const QString hostPath = hostDirectory.filePath(QStringLiteral("Host.qml"));
        check(writeFile(hostPath, QByteArrayLiteral("import QtQuick\nItem { width: 600; height: 88 }\n")), "write QML host fixture");
        QQuickView view;
        view.setSource(QUrl::fromLocalFile(hostPath));
        check(view.status() == QQuickView::Ready && view.rootObject(), "load a simple offscreen QML host");

        KeyboardPresenter presenter;
        ThemeRuntime      runtime(catalog, view, presenter);
        OverlayConfig     config;
        check(runtime.loadInitial(config, &error), "load the bundled default as a real QML component");
        check(runtime.activeThemeId() == QStringLiteral("builtin:default"), "bundled default is active initially");

        config.themeId      = QStringLiteral("ledger");
        config.themeOptions = {{QStringLiteral("item_spacing"), 8}, {QStringLiteral("accent"), QStringLiteral("#40ccaa")}};
        check(runtime.prepareSwitch(config, &error), "load the shipped, distinctly composed QML example");
        runtime.applyAcceptedConfiguration(config);
        check(view.rootObject()->findChild<QQuickItem*>(QStringLiteral("ledger-8"), Qt::FindChildrenRecursively) != nullptr,
              "the shipped example receives its declared options through the public API");
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
    QTemporaryDir runtimeBundled;
    testRuntimeLoadingAndSwitchRecovery(runtimeUser.path(), runtimeBundled.path());

    if (failures == 0) {
        std::cout << "Overlay theme tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
