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
    check(defaults.themeId == QStringLiteral("builtin:default") && defaults.spaceSymbol == QStringLiteral(" "),
          "select the bundled text theme and preserve literal spaces by default");
    check(defaults.dynamicSize && defaults.minWidth == 240 && defaults.minHeight == 64 && defaults.width == 600 && defaults.height == 88,
          "default panel grows up to the unchanged maximum surface size");
    check(defaults.panelVisibility == QStringLiteral("with-content") && defaults.expireAfterMs == 3000 && defaults.fadeDurationMs == 250,
          "default panel hides when history expires after inactivity");
    check(defaults.panelBorderWidth == 1 && defaults.panelBorderColor == QStringLiteral("#ffffff"), "default panel has a white one-pixel border");
    check(defaults.repeatPresentation == QStringLiteral("counted") && defaults.repeatCountThreshold == 4,
          "default repeats collapse at four occurrences");

    OverlayConfig config;
    QString       error;
    check(parse("[appearance]\nfont_size = 36\nbackground_color = '#112233'\n", &config, &error), "load a valid partial TOML file");
    check(config.fontSize == 36 && config.backgroundColor == QStringLiteral("#112233"), "read appearance overrides");
    check(config.width == 600 && config.themeId == QStringLiteral("builtin:default"),
          "partial files inherit the bundled text theme");
    check(parse("[theme]\nid='ledger'\n[theme.options]\nitem_spacing=7\naccent='#abcdef'\n", &config, &error), "parse theme selection and scalar options");
    check(config.themeId == QStringLiteral("ledger") && config.themeOptions.value(QStringLiteral("item_spacing")).toLongLong() == 7 &&
              config.themeOptions.value(QStringLiteral("accent")).toString() == QStringLiteral("#abcdef"),
          "preserve dynamic theme option values for descriptor validation");
    check(parse("[window]\nwidth=720\nheight=140\nmin_width=180\nmin_height=70\ndynamic_size=true\n[appearance]\npanel_border_width=2\npanel_border_color='#80112233'\n[display]\npanel_visibility='with-content'\n[repeat]\npresentation='counted'\ncount_threshold=4\n[symbols]\nfont_family='"
                "Symbols Nerd Font'\n[symbols.keys]\nBackspace='⌫'\n[symbols.modifiers]\nCtrl='⌃'\n",
                &config, &error),
          "parse dynamic sizing, border, panel visibility, counted repeat, and symbol settings");
    check(config.dynamicSize && config.width == 720 && config.minWidth == 180 && config.minHeight == 70 && config.panelBorderWidth == 2 &&
              config.panelVisibility == QStringLiteral("with-content") && config.repeatPresentation == QStringLiteral("counted") &&
              config.repeatCountThreshold == 4,
          "retain extension settings after validation");
    check(config.keySymbols.value(QStringLiteral("Backspace")).toString() == QStringLiteral("⌫") &&
              config.modifierSymbols.value(QStringLiteral("Ctrl")).toString() == QStringLiteral("⌃") && config.symbolFontFamily == QStringLiteral("Symbols Nerd Font"),
          "parse presentation-only key and modifier symbols");
    check(overlayConfigToQmlValues(config).value(QStringLiteral("dynamicSize")).toBool() && overlayConfigToQmlValues(config).value(QStringLiteral("panelBorderWidth")).toInt() == 2,
          "publish the new shared settings to theme QML");
    check(!parse("[theme.options]\nbad=[1,2]\n", &config, &error), "reject non-scalar theme option values");

    ConfigOverrides overrides;
    overrides.width          = 600;
    overrides.repeatsEnabled = false;
    const auto effective     = applyOverrides(config, overrides);
    check(effective.width == 600 && !effective.repeatsEnabled,
          "explicit CLI values override file settings even when equal to defaults");

    check(!parse("[display\npresentation='text'\n", &config, &error), "reject malformed TOML");
    check(!parse("[display]\npresentation='keycaps'\n", &config, &error) && error.contains(QStringLiteral("unknown configuration key")), "reject removed display mode");
    check(!parse("[appearance]\nkeycap_height=40\n", &config, &error) && error.contains(QStringLiteral("unknown configuration key")), "reject removed global cap settings");
    check(!parse("[appearance]\nheld_key_height=22\n", &config, &error) && error.contains(QStringLiteral("unknown configuration key")), "reject global held style settings");
    check(!parse("[display]\nshow_held_keys=true\n", &config, &error) && error.contains(QStringLiteral("unknown configuration key")), "reject global held visibility");
    check(!overlayConfigToQmlValues(config).contains(QStringLiteral("presentation")) && !overlayConfigToQmlValues(config).contains(QStringLiteral("keycapHeight")) &&
              !overlayConfigToQmlValues(config).contains(QStringLiteral("showHeldKeys")) && !overlayConfigToQmlValues(config).contains(QStringLiteral("heldKeyHeight")),
          "do not publish removed mode and cap settings to QML");
    check(!parse("[window]\nwidth='600'\n", &config, &error) && error.contains(QStringLiteral("must be an integer")), "reject wrong TOML types");
    check(!parse("[window]\nwidth=600.0\n", &config, &error), "reject numeric conversions between TOML integer and float types");
    check(!parse("[window]\nmargins=[1,2,3]\n", &config, &error), "require exactly four margins");
    check(!parse("[appearance]\nbackground_opacity=2.0\n", &config, &error), "reject values outside documented ranges");
    check(!parse("[appearance]\nbackground_color='not-a-color'\n", &config, &error), "reject invalid colors");
    check(!parse("[window]\ndynamic_size=true\nwidth=100\nmin_width=101\n", &config, &error), "reject dynamic minimum dimensions above the maximum surface");
    check(parse("[window]\ndynamic_size=false\nwidth=100\n", &config, &error), "fixed-size surfaces may retain dimensions below the unused dynamic minimum");
    check(!parse("[appearance]\npanel_border_width=-1\n", &config, &error), "reject negative border widths");
    check(!parse("[display]\npanel_visibility='sometimes'\n", &config, &error), "reject unknown panel visibility policies");
    check(!parse("[repeat]\npresentation='counted'\ncount_threshold=1\n", &config, &error), "reject counted repeat thresholds below two occurrences");
    check(parse("[symbols]\nspace='␣'\n", &config, &error) && config.spaceSymbol == QStringLiteral("␣") &&
              overlayConfigToQmlValues(config).value(QStringLiteral("spaceSymbol")).toString() == QStringLiteral("␣"),
          "read and publish the display-only space label");
    check(!parse("[symbols]\nspace=''\n", &config, &error), "reject an empty space label");
    check(!parse("[symbols]\nspace=123\n", &config, &error), "reject a non-string space label");
    check(!parse("[symbols.keys]\nBackspace=12\n", &config, &error), "reject non-string symbol mappings");
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
