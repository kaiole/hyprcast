#include "Support.hpp"
#include "config/OverlayConfig.hpp"
#include "history/BackspaceMode.hpp"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QObject>
#include <QSaveFile>
#include <QString>
#include <QTemporaryDir>
#include <expected>
#include <optional>

using namespace hyprcast::overlay;

namespace {

using tests::waitFor;

bool writeAtomically(const QString& path, const QByteArray& contents) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly)
        && file.write(contents) == contents.size() && file.commit();
}

// Parses `source` as the contents of a config file.
std::expected<OverlayConfig, QString> parse(const QByteArray& source) {
    const QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("overlay.toml"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(source) != source.size()) {
        return std::unexpected(QStringLiteral("cannot write the test config"));
    }
    file.close();
    return parseOverlayConfig(path);
}

// Why `source` is rejected, or nothing when it is accepted.
QString rejection(const QByteArray& source) {
    const auto parsed = parse(source);
    return parsed ? QString() : parsed.error();
}

TEST(OverlayConfigDefaults, MatchTheDocumentedValues) {
    const OverlayConfig defaults;
    EXPECT_TRUE(defaults.width == 600 && defaults.height == 88
                && toString(defaults.anchor) == QStringLiteral("bottom-right"))
        << "use built-in window defaults";
    EXPECT_TRUE(defaults.maxRetainedUtf16CodeUnits == 4096
                && defaults.backspaceMode == BackspaceMode::Delete)
        << "preserve documented history defaults";
    EXPECT_TRUE(defaults.themeId == QStringLiteral("builtin:default")
                && defaults.spaceSymbol == QStringLiteral(" "))
        << "select the bundled text theme and preserve literal spaces by "
           "default";
    EXPECT_TRUE(defaults.dynamicSize && defaults.minWidth == 240
                && defaults.minHeight == 64 && defaults.width == 600
                && defaults.height == 88)
        << "default panel grows up to the unchanged maximum surface size";
    EXPECT_TRUE(defaults.panelVisibility == PanelVisibility::WithContent
                && defaults.expireAfterMs == 3000
                && defaults.fadeDurationMs == 250)
        << "default panel hides when history expires after inactivity";
    EXPECT_TRUE(defaults.panelBorderWidth == 1
                && defaults.panelBorderColor == QStringLiteral("#ffffff"))
        << "default panel has a white one-pixel border";
    EXPECT_TRUE(defaults.repeatPresentation == RepeatPresentation::Counted
                && defaults.repeatCountThreshold == 4)
        << "default repeats collapse at four occurrences";
}

TEST(OverlayConfigParsing, PartialFileKeepsTheOtherDefaults) {
    const auto config =
        parse("[appearance]\nfont_size = 36\nbackground_color = '#112233'\n");
    ASSERT_TRUE(config) << "load a valid partial TOML file";
    EXPECT_TRUE(config->fontSize == 36
                && config->backgroundColor == QStringLiteral("#112233"))
        << "read appearance overrides";
    EXPECT_TRUE(config->width == 600
                && config->themeId == QStringLiteral("builtin:default"))
        << "partial files inherit the bundled text theme";
}

TEST(OverlayConfigParsing, ReadsThemeSelectionAndOptions) {
    const auto config =
        parse("[theme]\nid='ledger'\n[theme.options]\nitem_spacing=7\naccent="
              "'#abcdef'\n");
    ASSERT_TRUE(config) << "parse theme selection and scalar options";
    EXPECT_TRUE(
        config->themeId == QStringLiteral("ledger")
        && config->themeOptions.value(QStringLiteral("item_spacing"))
                .toLongLong()
            == 7
        && config->themeOptions.value(QStringLiteral("accent")).toString()
            == QStringLiteral("#abcdef"))
        << "preserve dynamic theme option values for descriptor validation";
    EXPECT_FALSE(parse("[theme.options]\nbad=[1,2]\n"))
        << "reject non-scalar theme option values";
}

// A file that sets something in most tables.
class OverlayConfigExtensions : public testing::Test {
protected:
    void SetUp() override {
        auto parsed =
            parse("[window]\nwidth=720\nheight=140\nmin_width=180\nmin_height="
                  "70\ndynamic_size=true\n[appearance]\npanel_border_width="
                  "2\npanel_border_color='#80112233'\n[display]"
                  "\npanel_visibility='with-content'\n[repeat]\npresentation='"
                  "counted'\ncount_threshold=4\n[symbols]\nfont_family='"
                  "Symbols Nerd "
                  "Font'\n[symbols.keys]\nBackspace='⌫'\n[symbols.modifiers]"
                  "\nCtrl='⌃'\n");
        ASSERT_TRUE(parsed)
            << "parse dynamic sizing, border, panel visibility, counted "
               "repeat, and symbol settings";
        config = *parsed;
    }

    OverlayConfig config;
};

TEST_F(OverlayConfigExtensions, AreRetainedAfterValidation) {
    EXPECT_TRUE(config.dynamicSize && config.width == 720
                && config.minWidth == 180 && config.minHeight == 70
                && config.panelBorderWidth == 2
                && config.panelVisibility == PanelVisibility::WithContent
                && config.repeatPresentation == RepeatPresentation::Counted
                && config.repeatCountThreshold == 4)
        << "retain extension settings after validation";
    EXPECT_TRUE(
        config.keySymbols.value(QStringLiteral("Backspace")).toString()
            == QStringLiteral("⌫")
        && config.modifierSymbols.value(QStringLiteral("Ctrl")).toString()
            == QStringLiteral("⌃")
        && config.symbolFontFamily == QStringLiteral("Symbols Nerd Font"))
        << "parse presentation-only key and modifier symbols";
}

TEST_F(OverlayConfigExtensions, ArePublishedToThemeQml) {
    const auto values = overlayConfigToQmlValues(config);
    EXPECT_TRUE(values.value(QStringLiteral("dynamicSize")).toBool()
                && values.value(QStringLiteral("panelBorderWidth")).toInt()
                    == 2)
        << "publish the new shared settings to theme QML";
    EXPECT_TRUE(!values.contains(QStringLiteral("presentation"))
                && !values.contains(QStringLiteral("keycapHeight"))
                && !values.contains(QStringLiteral("showHeldKeys"))
                && !values.contains(QStringLiteral("heldKeyHeight")))
        << "do not publish removed mode and cap settings to QML";
}

TEST_F(OverlayConfigExtensions, CommandLineValuesWinOverTheFile) {
    ConfigOverrides overrides;
    overrides.width = 600;
    overrides.repeatsEnabled = false;
    const auto effective = applyOverrides(config, overrides);
    EXPECT_TRUE(effective.width == 600 && !effective.repeatsEnabled)
        << "explicit CLI values override file settings even when equal to "
           "defaults";
}

TEST(OverlayConfigParsing, RejectsMalformedToml) {
    EXPECT_FALSE(parse("[display\npresentation='text'\n"))
        << "reject malformed TOML";
}

TEST(OverlayConfigParsing, RejectsRemovedAndUnknownKeys) {
    const QString unknown = QStringLiteral("unknown configuration key");
    EXPECT_TRUE(
        rejection("[display]\npresentation='keycaps'\n").contains(unknown))
        << "reject removed display mode";
    EXPECT_TRUE(rejection("[appearance]\nkeycap_height=40\n").contains(unknown))
        << "reject removed global cap settings";
    EXPECT_TRUE(
        rejection("[appearance]\nheld_key_height=22\n").contains(unknown))
        << "reject global held style settings";
    EXPECT_TRUE(rejection("[display]\nshow_held_keys=true\n").contains(unknown))
        << "reject global held visibility";
    EXPECT_FALSE(parse("[other]\nvalue=1\n"))
        << "reject unknown top-level tables";
}

TEST(OverlayConfigParsing, RejectsWrongTypes) {
    EXPECT_TRUE(rejection("[window]\nwidth='600'\n")
                    .contains(QStringLiteral("must be an integer")))
        << "reject wrong TOML types";
    EXPECT_FALSE(parse("[window]\nwidth=600.0\n"))
        << "reject numeric conversions between TOML integer and float types";
    EXPECT_FALSE(parse("[window]\nmargins=[1,2,3]\n"))
        << "require exactly four margins";
    EXPECT_FALSE(parse("[symbols]\nspace=123\n"))
        << "reject a non-string space label";
    EXPECT_FALSE(parse("[symbols.keys]\nBackspace=12\n"))
        << "reject non-string symbol mappings";
}

TEST(OverlayConfigParsing, RejectsValuesOutsideTheirRange) {
    EXPECT_FALSE(parse("[appearance]\nbackground_opacity=2.0\n"))
        << "reject values outside documented ranges";
    EXPECT_FALSE(parse("[appearance]\nbackground_color='not-a-color'\n"))
        << "reject invalid colors";
    EXPECT_FALSE(parse("[appearance]\npanel_border_width=-1\n"))
        << "reject negative border widths";
    EXPECT_FALSE(parse("[display]\npanel_visibility='sometimes'\n"))
        << "reject unknown panel visibility policies";
    EXPECT_FALSE(parse("[repeat]\npresentation='counted'\ncount_threshold=1\n"))
        << "reject counted repeat thresholds below two occurrences";
    EXPECT_FALSE(parse("[symbols]\nspace=''\n"))
        << "reject an empty space label";
    EXPECT_FALSE(parse("[history]\nbackspace='sometimes'\n"))
        << "reject unknown behavior enums";
}

TEST(OverlayConfigParsing, BoundsRetainedHistory) {
    const auto smallest = parse("[history]\nmax_retained_utf16_code_units=1\n");
    ASSERT_TRUE(smallest) << smallest.error().toStdString();
    EXPECT_EQ(smallest->maxRetainedUtf16CodeUnits, 1);
    const auto largest =
        parse("[history]\nmax_retained_utf16_code_units=16384\n");
    ASSERT_TRUE(largest) << largest.error().toStdString();
    EXPECT_EQ(largest->maxRetainedUtf16CodeUnits, 16384);

    const QString range = QStringLiteral(
        "'history.max_retained_utf16_code_units' must be in the range "
        "1..16384");
    EXPECT_EQ(rejection("[history]\nmax_retained_utf16_code_units=0\n"), range);
    EXPECT_EQ(rejection("[history]\nmax_retained_utf16_code_units=16385\n"),
              range);
}

TEST(OverlayConfigParsing, DynamicMinimumOnlyBindsADynamicSurface) {
    EXPECT_FALSE(
        parse("[window]\ndynamic_size=true\nwidth=100\nmin_width=101\n"))
        << "reject dynamic minimum dimensions above the maximum surface";
    EXPECT_TRUE(parse("[window]\ndynamic_size=false\nwidth=100\n"))
        << "fixed-size surfaces may retain dimensions below the unused dynamic "
           "minimum";
}

TEST(OverlayConfigParsing, ReadsAndPublishesTheSpaceLabel) {
    const auto config = parse("[symbols]\nspace='␣'\n");
    ASSERT_TRUE(config) << "read the display-only space label";
    EXPECT_TRUE(config->spaceSymbol == QStringLiteral("␣")
                && overlayConfigToQmlValues(*config)
                        .value(QStringLiteral("spaceSymbol"))
                        .toString()
                    == QStringLiteral("␣"))
        << "read and publish the display-only space label";
}

TEST(OverlayConfigPath, HonorsAnAbsoluteConfigHome) {
    const QByteArray previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    const bool hadConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", QByteArrayLiteral("/tmp/hyprcast-test-config"));
    EXPECT_EQ(defaultConfigPath(),
              QStringLiteral("/tmp/hyprcast-test-config/hyprcast/overlay.toml"))
        << "honor absolute XDG_CONFIG_HOME";
    if (hadConfigHome) {
        qputenv("XDG_CONFIG_HOME", previousConfigHome);
    } else {
        qunsetenv("XDG_CONFIG_HOME");
    }
}

// A config path below a directory that does not exist yet.
class OverlayConfigInitialLoad : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(directory.isValid()) << "temporary directory";
    }

    QTemporaryDir directory;
    QString path =
        directory.filePath(QStringLiteral("new/settings/overlay.toml"));
};

TEST_F(OverlayConfigInitialLoad, AbsentImplicitConfigStartsFromDefaults) {
    OverlayConfigManager optionalDefault(path, false);
    EXPECT_TRUE(optionalDefault.initialize().has_value())
        << "an absent implicit config starts from built-in defaults";
    EXPECT_TRUE(optionalDefault.config().width == 600
                && QFileInfo::exists(QFileInfo(path).absolutePath()))
        << "create the default config directory for watching";
}

TEST_F(OverlayConfigInitialLoad, AbsentExplicitConfigIsAnError) {
    OverlayConfigManager missingExplicit(path, true);
    const auto missing = missingExplicit.initialize();
    EXPECT_TRUE(!missing
                && missing.error().contains(QStringLiteral("does not exist")))
        << "an explicitly requested missing config is an error";
}

TEST_F(OverlayConfigInitialLoad, InvalidConfigFailsWithItsPath) {
    // Creates the directory.
    OverlayConfigManager optionalDefault(path, false);
    ASSERT_TRUE(optionalDefault.initialize().has_value())
        << "an absent implicit config starts from built-in defaults";
    ASSERT_TRUE(
        writeAtomically(path,
                        QByteArrayLiteral("[display\npresentation='text'\n")))
        << "create a malformed initial config";
    OverlayConfigManager malformedInitial(path, true);
    const auto malformed = malformedInitial.initialize();
    EXPECT_TRUE(!malformed && malformed.error().contains(path))
        << "an invalid initial config fails with its path";
}

TEST_F(OverlayConfigInitialLoad, CandidateIsReadWithoutBeingApplied) {
    ConfigOverrides overrides;
    overrides.width = 640;
    OverlayConfigManager manager(path, false, overrides);
    int changes = 0;
    int validations = 0;
    QObject::connect(&manager,
                     &OverlayConfigManager::configurationChanged,
                     QCoreApplication::instance(),
                     [&] { ++changes; });
    manager.setRuntimeValidator(
        [&](const std::optional<OverlayConfig>&,
            const OverlayConfig&) -> std::expected<void, QString> {
            ++validations;
            return {};
        });
    ASSERT_TRUE(manager.initialize().has_value()) << "start from defaults";
    ASSERT_TRUE(changes == 1 && validations == 1) << "the initial load";

    ASSERT_TRUE(writeAtomically(path, "[appearance]\nfont_size=41\n"))
        << "save a config behind the manager's back";
    const auto candidate = manager.loadCandidate();
    ASSERT_TRUE(candidate.has_value()) << "the saved config is valid";
    EXPECT_TRUE(candidate->fontSize == 41 && candidate->width == 640)
        << "a candidate is the file on disk with the overrides applied";
    EXPECT_NE(manager.config().fontSize, 41)
        << "reading a candidate leaves the current configuration alone";

    manager.adopt(*candidate);
    EXPECT_EQ(manager.config().fontSize, 41)
        << "an adopted candidate is the current configuration";
    EXPECT_TRUE(changes == 1 && validations == 1)
        << "adopting asks no validator and announces no change";

    ASSERT_TRUE(writeAtomically(path, "[appearance\n"))
        << "save a malformed config";
    const auto malformed = manager.loadCandidate();
    EXPECT_TRUE(!malformed && malformed.error().contains(path))
        << "a malformed candidate fails with its path";
    EXPECT_EQ(manager.config().fontSize, 41)
        << "a failed candidate leaves the current configuration alone";
}

// A manager that watches a config file with font size 30 and refuses, as the
// running overlay would, any reload that changes the width.
class OverlayConfigLiveReload : public testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(directory.isValid()) << "temporary directory";
        ASSERT_TRUE(save("[appearance]\nfont_size=30\n"))
            << "create initial live-reload config";
        QObject::connect(&manager,
                         &OverlayConfigManager::reloadRejected,
                         QCoreApplication::instance(),
                         [this](const QString& reason) {
                             ++rejections;
                             lastRejection = reason;
                         });
        manager.setRuntimeValidator(
            [](const std::optional<OverlayConfig>& previous,
               const OverlayConfig& candidate) -> std::expected<void, QString> {
                if (previous && candidate.width != previous->width) {
                    return std::unexpected(
                        QStringLiteral("test runtime rejection"));
                }
                return {};
            });
        ASSERT_TRUE(manager.initialize().has_value())
            << "load initial config before runtime start";
        ASSERT_EQ(manager.config().fontSize, 30) << "initial config accepted";
        manager.startWatching();
    }

    // Replaces the config file the way an editor saves it.
    [[nodiscard]] bool save(const QByteArray& contents) const {
        return writeAtomically(path, contents);
    }

    QTemporaryDir directory;
    QString path = directory.filePath(QStringLiteral("overlay.toml"));
    OverlayConfigManager manager {path, true};
    int rejections = 0;
    QString lastRejection;
};

TEST_F(OverlayConfigLiveReload, ReloadsAfterAnAtomicReplacement) {
    ASSERT_TRUE(save("[appearance]\nfont_size=33\n"))
        << "atomically replace config with a valid candidate";
    EXPECT_TRUE(waitFor([&] { return manager.config().fontSize == 33; }))
        << "watch directory and reload after atomic replacement";
}

TEST_F(OverlayConfigLiveReload, MalformedReloadKeepsTheAcceptedSettings) {
    ASSERT_TRUE(save("[appearance\nfont_size=40\n"))
        << "atomically replace config with malformed TOML";
    ASSERT_TRUE(waitFor([&] { return rejections == 1; }))
        << "watcher reports the malformed reload";
    EXPECT_TRUE(lastRejection.contains(path))
        << "reload rejection names the config file and includes a diagnostic";
    EXPECT_EQ(manager.config().fontSize, 30)
        << "malformed reload keeps the last accepted settings";

    ASSERT_TRUE(save("[appearance]\nfont_size=37\n"))
        << "save a valid config after a malformed one";
    EXPECT_TRUE(waitFor([&] { return manager.config().fontSize == 37; }))
        << "watcher recovers and accepts a later valid save";
}

TEST_F(OverlayConfigLiveReload, RuntimeRejectedReloadAppliesNothing) {
    ASSERT_TRUE(save("[window]\nwidth=800\n[appearance]\nfont_size=42\n"))
        << "write a candidate rejected by runtime validation";
    ASSERT_TRUE(waitFor([&] { return rejections == 1; }))
        << "watcher reports the runtime-invalid reload";
    EXPECT_TRUE(manager.config().fontSize == 30
                && manager.config().width == 600)
        << "runtime-rejected candidate applies none of its settings";
}

} // namespace

int main(int argc, char** argv) {
    return tests::run<QCoreApplication>(argc, argv);
}
