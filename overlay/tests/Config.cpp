#include "config/OverlayConfig.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>

namespace {
    int  failures = 0;

    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
        }
    }

    bool writeAtomically(const QString& path, const QByteArray& contents) {
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
    }

    bool waitFor(const std::function<bool()>& condition, int timeoutMs = 2500) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            if (condition()) {
                return true;
            }
            QThread::msleep(10);
        }
        return condition();
    }

    bool parse(const QByteArray& source, Hyprcast::Overlay::OverlayConfig* config, QString* error) {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("overlay.toml"));
        QFile         file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size()) {
            return false;
        }
        file.close();
        return Hyprcast::Overlay::parseOverlayConfig(path, config, error);
    }
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    using namespace Hyprcast::Overlay;

    OverlayConfig defaults;
    check(defaults.width == 600 && defaults.height == 88 && defaults.anchor == QStringLiteral("bottom-right"), "use built-in window defaults");
    check(defaults.maxRetainedUtf16CodeUnits == 4096 && defaults.backspaceMode == QStringLiteral("delete"), "preserve documented history defaults");

    OverlayConfig config;
    QString       error;
    check(parse("[appearance]\nfont_size = 36\nbackground_color = '#112233'\n[display]\npresentation = 'keycaps'\n", &config, &error), "load a valid partial TOML file");
    check(config.fontSize == 36 && config.backgroundColor == QStringLiteral("#112233"), "read appearance overrides");
    check(config.presentation == QStringLiteral("keycaps") && config.width == 600, "partial file inherits defaults");

    ConfigOverrides overrides;
    overrides.width          = 600;
    overrides.showHeldKeys   = false;
    overrides.presentation   = QStringLiteral("text");
    overrides.repeatsEnabled = false;
    const auto effective     = applyOverrides(config, overrides);
    check(effective.width == 600 && effective.presentation == QStringLiteral("text") && !effective.showHeldKeys && !effective.repeatsEnabled,
          "explicit CLI values override file settings even when equal to defaults");

    check(!parse("[display\npresentation='text'\n", &config, &error), "reject malformed TOML");
    check(!parse("[display]\npresentaton='text'\n", &config, &error) && error.contains(QStringLiteral("unknown configuration key")), "reject unknown keys with diagnostics");
    check(!parse("[window]\nwidth='600'\n", &config, &error) && error.contains(QStringLiteral("must be an integer")), "reject wrong TOML types");
    check(!parse("[window]\nwidth=600.0\n", &config, &error), "reject numeric conversions between TOML integer and float types");
    check(!parse("[window]\nmargins=[1,2,3]\n", &config, &error), "require exactly four margins");
    check(!parse("[appearance]\nbackground_opacity=2.0\n", &config, &error), "reject values outside documented ranges");
    check(!parse("[appearance]\nbackground_color='not-a-color'\n", &config, &error), "reject invalid colors");
    check(!parse("[history]\nbackspace='sometimes'\n", &config, &error), "reject unknown behavior enums");
    check(!parse("[other]\nvalue=1\n", &config, &error), "reject unknown top-level tables");

    const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const bool       hadConfigHome      = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QByteArrayLiteral("/tmp/hyprcast-test-config"));
    check(defaultConfigPath() == QStringLiteral("/tmp/hyprcast-test-config/hyprcast/overlay.toml"), "honor absolute XDG_CONFIG_HOME");
    if (hadConfigHome) {
        qputenv("XDG_CONFIG_HOME", previousConfigHome);
    } else {
        qunsetenv("XDG_CONFIG_HOME");
    }

    QTemporaryDir        absentDirectory;
    const QString        absentPath = absentDirectory.filePath(QStringLiteral("new/settings/overlay.toml"));
    OverlayConfigManager optionalDefault(absentPath, false);
    check(optionalDefault.initialize(&error), "an absent implicit config starts from built-in defaults");
    check(optionalDefault.config().width == 600 && QFileInfo::exists(QFileInfo(absentPath).absolutePath()), "create the default config directory for watching");
    OverlayConfigManager missingExplicit(absentPath, true);
    check(!missingExplicit.initialize(&error) && error.contains(QStringLiteral("does not exist")), "an explicitly requested missing config is an error");
    check(writeAtomically(absentPath, QByteArrayLiteral("[display\npresentation='text'\n")), "create a malformed initial config");
    OverlayConfigManager malformedInitial(absentPath, true);
    check(!malformedInitial.initialize(&error) && error.contains(absentPath), "an invalid initial config fails with its path");

    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("overlay.toml"));
    check(writeAtomically(path, QByteArrayLiteral("[appearance]\nfont_size=30\n")), "create initial live-reload config");

    OverlayConfigManager manager(path, true);
    int                  rejections = 0;
    QString              lastRejection;
    QObject::connect(&manager, &OverlayConfigManager::reloadRejected, &application, [&rejections, &lastRejection](const QString& reason) {
        ++rejections;
        lastRejection = reason;
    });
    manager.setRuntimeValidator([](const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate, QString* validatorError) {
        if (previous && candidate.width != previous->width) {
            if (validatorError) {
                *validatorError = QStringLiteral("test runtime rejection");
            }
            return false;
        }
        return true;
    });
    check(manager.initialize(&error), "load initial config before runtime start");
    check(manager.config().fontSize == 30, "initial config accepted");
    manager.startWatching();

    check(writeAtomically(path, QByteArrayLiteral("[appearance]\nfont_size=33\n")), "atomically replace config with a valid candidate");
    check(waitFor([&] { return manager.config().fontSize == 33; }), "watch directory and reload after atomic replacement");

    check(writeAtomically(path, QByteArrayLiteral("[appearance\nfont_size=40\n")), "atomically replace config with malformed TOML");
    check(waitFor([&] { return rejections == 1; }), "watcher reports the malformed reload");
    check(lastRejection.contains(path), "reload rejection names the config file and includes a diagnostic");
    check(manager.config().fontSize == 33, "malformed reload keeps the last accepted settings");

    check(writeAtomically(path, QByteArrayLiteral("[appearance]\nfont_size=37\n")), "save a valid config after a malformed one");
    check(waitFor([&] { return manager.config().fontSize == 37; }), "watcher recovers and accepts a later valid save");

    check(writeAtomically(path, QByteArrayLiteral("[window]\nwidth=800\n[appearance]\nfont_size=42\n")), "write a candidate rejected by runtime validation");
    check(waitFor([&] { return rejections == 2; }), "watcher reports the runtime-invalid reload");
    check(manager.config().fontSize == 37 && manager.config().width == 600, "runtime-rejected candidate applies none of its settings");

    if (failures == 0) {
        std::cout << "Overlay config tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
