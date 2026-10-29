#include "OverlayConfig.hpp"
#include "TomlReader.hpp"

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace Hyprcast::Overlay {
    namespace {
        using Toml::TableReader;

        std::unexpected<QString> outOfRange(const QString& name, int minimum, int maximum) {
            return std::unexpected(QStringLiteral("'%1' must be in the range %2..%3").arg(name).arg(minimum).arg(maximum));
        }

        std::expected<void, QString> checkRange(int value, Range<int> range, const QString& name) {
            if (!range.contains(value)) {
                return outOfRange(name, range.minimum, range.maximum);
            }
            return {};
        }

        std::expected<int, QString> parseFontWeight(QStringView text) {
            const QString weight = text.toString().toLower();
            if (weight == u"normal") {
                return 400;
            }
            if (weight == u"medium") {
                return 500;
            }
            if (weight == u"demibold") {
                return 600;
            }
            if (weight == u"bold") {
                return 700;
            }
            return std::unexpected(QStringLiteral("'appearance.font_weight' must be normal, medium, demibold, or bold"));
        }

        std::expected<void, QString> readMargins(const toml::node& node, QMargins& out) {
            const auto* array = node.as_array();
            if (!array || array->size() != 4) {
                return std::unexpected(QStringLiteral("'window.margins' must be an array of four integers: left, top, right, bottom"));
            }
            std::array<int, 4> values{};
            for (std::size_t i = 0; i < values.size(); ++i) {
                const auto value = (*array)[i].value_exact<std::int64_t>();
                if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
                    return std::unexpected(QStringLiteral("'window.margins' must contain four integers: left, top, right, bottom"));
                }
                values[i] = static_cast<int>(*value);
            }
            out = QMargins(values[0], values[1], values[2], values[3]);
            return {};
        }

        std::expected<void, QString> readThemeOptions(const toml::node& node, QVariantMap& out) {
            const auto* options = node.as_table();
            if (!options) {
                return std::unexpected(QStringLiteral("'theme.options' must be a TOML table"));
            }
            for (const auto& [key, value] : *options) {
                const QString name = Toml::toQString(key.str());
                if (const auto text = value.value_exact<std::string>()) {
                    out.insert(name, Toml::toQString(*text));
                } else if (const auto flag = value.value_exact<bool>()) {
                    out.insert(name, *flag);
                } else if (const auto integer = value.value_exact<std::int64_t>()) {
                    out.insert(name, QVariant::fromValue<qlonglong>(*integer));
                } else if (const auto number = value.value_exact<double>()) {
                    out.insert(name, *number);
                } else {
                    return std::unexpected(QStringLiteral("'theme.options.%1' must be a string, boolean, integer, or number").arg(name));
                }
            }
            return {};
        }

        // Reads `name` with `read` if the table is present; absent tables keep defaults.
        template <typename Read>
        std::expected<void, QString> readSection(const toml::table& root, std::string_view name, Read&& read) {
            const auto table = Toml::optionalTable(root, name);
            if (!table) {
                return std::unexpected(table.error());
            }
            if (!*table) {
                return {};
            }
            TableReader reader(**table, Toml::toQString(name));
            std::forward<Read>(read)(reader);
            return reader.result();
        }

        std::expected<OverlayConfig, QString> readConfig(const toml::table& root) {
            OverlayConfig config;
            const auto    sections = {
                TableReader(root, {}).onlyKeys({"window", "appearance", "display", "history", "repeat", "expiration", "theme", "symbols"}).result(),
                readSection(root, "window",
                            [&](TableReader& read) {
                                read.onlyKeys({"monitor", "anchor", "margins", "width", "height", "min_width", "min_height", "dynamic_size", "click_through"})
                                    .string("monitor", config.monitor)
                                    .choice("anchor", config.anchor, parseAnchor)
                                    .node("margins", [&](const toml::node& node) { return readMargins(node, config.margins); })
                                    .integer("width", config.width)
                                    .integer("height", config.height)
                                    .integer("min_width", config.minWidth)
                                    .integer("min_height", config.minHeight)
                                    .boolean("dynamic_size", config.dynamicSize)
                                    .boolean("click_through", config.clickThrough);
                            }),
                readSection(root, "appearance",
                            [&](TableReader& read) {
                                read.onlyKeys({"background_color", "background_opacity", "corner_radius", "panel_border_width", "panel_border_color", "foreground_color",
                                               "font_family", "font_size", "font_weight", "history_padding_x", "text_extra_padding_x"})
                                    .string("background_color", config.backgroundColor)
                                    .number("background_opacity", config.backgroundOpacity)
                                    .integer("corner_radius", config.cornerRadius)
                                    .integer("panel_border_width", config.panelBorderWidth)
                                    .string("panel_border_color", config.panelBorderColor)
                                    .string("foreground_color", config.foregroundColor)
                                    .string("font_family", config.fontFamily)
                                    .integer("font_size", config.fontSize)
                                    .choice("font_weight", config.fontWeight, parseFontWeight)
                                    .integer("history_padding_x", config.historyPaddingX)
                                    .integer("text_extra_padding_x", config.textExtraPaddingX);
                            }),
                readSection(root, "theme",
                            [&](TableReader& read) {
                                read.onlyKeys({"id", "options"}).string("id", config.themeId).node("options", [&](const toml::node& node) {
                                    return readThemeOptions(node, config.themeOptions);
                                });
                            }),
                readSection(root, "symbols",
                            [&](TableReader& read) {
                                read.onlyKeys({"font_family", "space", "keys", "modifiers"})
                                    .string("font_family", config.symbolFontFamily)
                                    .string("space", config.spaceSymbol)
                                    .stringMap("keys", config.keySymbols)
                                    .stringMap("modifiers", config.modifierSymbols);
                            }),
                readSection(root, "display",
                            [&](TableReader& read) { read.onlyKeys({"panel_visibility"}).choice("panel_visibility", config.panelVisibility, parsePanelVisibility); }),
                readSection(root, "history",
                            [&](TableReader& read) {
                                read.onlyKeys({"backspace", "max_retained_utf16_code_units"})
                                    .choice("backspace", config.backspaceMode, parseBackspaceMode)
                                    .integer("max_retained_utf16_code_units", config.maxRetainedUtf16CodeUnits);
                            }),
                readSection(root, "repeat",
                            [&](TableReader& read) {
                                read.onlyKeys({"enabled", "presentation", "count_threshold"})
                                    .boolean("enabled", config.repeatsEnabled)
                                    .choice("presentation", config.repeatPresentation, parseRepeatPresentation)
                                    .integer("count_threshold", config.repeatCountThreshold);
                            }),
                readSection(root, "expiration",
                            [&](TableReader& read) {
                                read.onlyKeys({"after_ms", "fade_duration_ms"}).integer("after_ms", config.expireAfterMs).integer("fade_duration_ms", config.fadeDurationMs);
                            }),
            };
            for (const auto& section : sections) {
                if (!section) {
                    return std::unexpected(section.error());
                }
            }
            return config;
        }

        std::expected<void, QString> validateColor(const QString& value, const QString& name) {
            if (value.isEmpty() || !QColor(value).isValid()) {
                return std::unexpected(QStringLiteral("'%1' must be a valid Qt color (for example '#ffffff')").arg(name));
            }
            return {};
        }

        bool isLabel(const QString& text, qsizetype maxLength) {
            return !text.isEmpty() && text.size() <= maxLength && !text.contains(QChar::Null);
        }

        std::expected<void, QString> validateSymbolMap(const QVariantMap& mappings, const QString& path) {
            if (mappings.size() > 256) {
                return std::unexpected(QStringLiteral("'%1' may contain at most 256 mappings").arg(path));
            }
            for (auto it = mappings.cbegin(); it != mappings.cend(); ++it) {
                if (!isLabel(it.key(), 64) || !isLabel(it.value().toString(), 128)) {
                    return std::unexpected(QStringLiteral("'%1.%2' needs a non-empty identity (max 64 characters) and display label (max 128 characters)").arg(path, it.key()));
                }
            }
            return {};
        }
    } // namespace

    std::expected<Anchor, QString> parseAnchor(QStringView text) {
        const QStringList parts = text.toString().toLower().split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.isEmpty() || parts.size() > 2) {
            return std::unexpected(QStringLiteral("'window.anchor' must be one edge or a corner (for example 'bottom-right')"));
        }
        Anchor anchor{.vertical = Anchor::Vertical::None, .horizontal = Anchor::Horizontal::None};
        for (const auto& part : parts) {
            const bool vertical = part == u"top" || part == u"bottom";
            if (vertical && anchor.vertical != Anchor::Vertical::None) {
                return std::unexpected(QStringLiteral("'window.anchor' cannot contain two vertical edges"));
            }
            const bool horizontal = part == u"left" || part == u"right";
            if (horizontal && anchor.horizontal != Anchor::Horizontal::None) {
                return std::unexpected(QStringLiteral("'window.anchor' cannot contain two horizontal edges"));
            }
            if (part == u"top") {
                anchor.vertical = Anchor::Vertical::Top;
            } else if (part == u"bottom") {
                anchor.vertical = Anchor::Vertical::Bottom;
            } else if (part == u"left") {
                anchor.horizontal = Anchor::Horizontal::Left;
            } else if (part == u"right") {
                anchor.horizontal = Anchor::Horizontal::Right;
            } else {
                return std::unexpected(QStringLiteral("invalid 'window.anchor'; use top, bottom, left, right, or a corner such as bottom-right"));
            }
        }
        return anchor;
    }

    std::expected<BackspaceMode, QString> parseBackspaceMode(QStringView text) {
        const QString mode = text.toString().toLower();
        if (mode == u"delete") {
            return BackspaceMode::Delete;
        }
        if (mode == u"symbol") {
            return BackspaceMode::Symbol;
        }
        return std::unexpected(QStringLiteral("'history.backspace' must be 'delete' or 'symbol'"));
    }

    std::expected<PanelVisibility, QString> parsePanelVisibility(QStringView text) {
        const QString visibility = text.toString().toLower();
        if (visibility == u"always") {
            return PanelVisibility::Always;
        }
        if (visibility == u"with-content") {
            return PanelVisibility::WithContent;
        }
        if (visibility == u"never") {
            return PanelVisibility::Never;
        }
        return std::unexpected(QStringLiteral("'display.panel_visibility' must be 'always', 'with-content', or 'never'"));
    }

    std::expected<RepeatPresentation, QString> parseRepeatPresentation(QStringView text) {
        const QString presentation = text.toString().toLower();
        if (presentation == u"expanded") {
            return RepeatPresentation::Expanded;
        }
        if (presentation == u"counted") {
            return RepeatPresentation::Counted;
        }
        return std::unexpected(QStringLiteral("'repeat.presentation' must be 'expanded' or 'counted'"));
    }

    QString toString(Anchor anchor) {
        QStringList parts;
        if (anchor.vertical != Anchor::Vertical::None) {
            parts.push_back(anchor.vertical == Anchor::Vertical::Top ? QStringLiteral("top") : QStringLiteral("bottom"));
        }
        if (anchor.horizontal != Anchor::Horizontal::None) {
            parts.push_back(anchor.horizontal == Anchor::Horizontal::Left ? QStringLiteral("left") : QStringLiteral("right"));
        }
        return parts.join(QLatin1Char('-'));
    }

    QString toString(PanelVisibility visibility) {
        switch (visibility) {
            case PanelVisibility::Always: return QStringLiteral("always");
            case PanelVisibility::WithContent: return QStringLiteral("with-content");
            case PanelVisibility::Never: return QStringLiteral("never");
        }
        return {};
    }

    QString toString(RepeatPresentation presentation) {
        switch (presentation) {
            case RepeatPresentation::Expanded: return QStringLiteral("expanded");
            case RepeatPresentation::Counted: return QStringLiteral("counted");
        }
        return {};
    }

    QVariantMap overlayConfigToQmlValues(const OverlayConfig& c) {
        return {{QStringLiteral("width"), c.width},
                {QStringLiteral("height"), c.height},
                {QStringLiteral("anchor"), toString(c.anchor)},
                {QStringLiteral("dynamicSize"), c.dynamicSize},
                {QStringLiteral("minWidth"), c.minWidth},
                {QStringLiteral("minHeight"), c.minHeight},
                {QStringLiteral("backgroundColor"), c.backgroundColor},
                {QStringLiteral("backgroundOpacity"), c.backgroundOpacity},
                {QStringLiteral("cornerRadius"), c.cornerRadius},
                {QStringLiteral("panelBorderWidth"), c.panelBorderWidth},
                {QStringLiteral("panelBorderColor"), c.panelBorderColor},
                {QStringLiteral("foregroundColor"), c.foregroundColor},
                {QStringLiteral("fontFamily"), c.fontFamily},
                {QStringLiteral("fontSize"), c.fontSize},
                {QStringLiteral("fontWeight"), c.fontWeight},
                {QStringLiteral("historyPaddingX"), c.historyPaddingX},
                {QStringLiteral("textExtraPaddingX"), c.textExtraPaddingX},
                {QStringLiteral("symbolFontFamily"), c.symbolFontFamily},
                {QStringLiteral("spaceSymbol"), c.spaceSymbol},
                {QStringLiteral("keySymbols"), c.keySymbols},
                {QStringLiteral("modifierSymbols"), c.modifierSymbols},
                {QStringLiteral("panelVisibility"), toString(c.panelVisibility)},
                {QStringLiteral("repeatPresentation"), toString(c.repeatPresentation)},
                {QStringLiteral("repeatCountThreshold"), c.repeatCountThreshold}};
    }

    QString defaultConfigPath(QString* warning) {
        if (warning) {
            warning->clear();
        }
        QString configHome = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (configHome.isEmpty() || !QDir::isAbsolutePath(configHome)) {
            if (!configHome.isEmpty() && warning) {
                *warning = QStringLiteral("XDG_CONFIG_HOME is not absolute; using ~/.config instead");
            }
            configHome = QDir::home().filePath(QStringLiteral(".config"));
        }
        return QDir(configHome).filePath(QStringLiteral("hyprcast/overlay.toml"));
    }

    std::expected<OverlayConfig, QString> parseOverlayConfig(const QString& path) {
        try {
            const toml::table parsed = toml::parse_file(path.toStdString());
            auto              config = readConfig(parsed);
            if (!config) {
                return config;
            }
            if (auto valid = validateOverlayConfig(*config); !valid) {
                return std::unexpected(valid.error());
            }
            return config;
        } catch (const std::exception& exception) { // toml::parse_error derives from std::runtime_error.
            return std::unexpected(QString::fromUtf8(exception.what()));
        }
    }

    std::expected<void, QString> validateOverlayConfig(const OverlayConfig& config) {
        if (config.monitor.size() > 256 || config.monitor.contains(QChar::Null)) {
            return std::unexpected(QStringLiteral("'window.monitor' must be at most 256 characters and contain no NUL"));
        }
        const std::pair<int, const char*> sizes[] = {
            {config.width, "window.width"}, {config.height, "window.height"}, {config.minWidth, "window.min_width"}, {config.minHeight, "window.min_height"}};
        for (const auto& [value, name] : sizes) {
            if (auto ok = checkRange(value, Limits::SurfaceSize, QString::fromLatin1(name)); !ok) {
                return ok;
            }
        }
        if (config.dynamicSize && (config.minWidth > config.width || config.minHeight > config.height)) {
            return std::unexpected(QStringLiteral("when window.dynamic_size is enabled, min_width and min_height must not exceed width and height"));
        }
        for (const int margin : {config.margins.left(), config.margins.top(), config.margins.right(), config.margins.bottom()}) {
            if (auto ok = checkRange(margin, Limits::Margin, QStringLiteral("window.margins")); !ok) {
                return ok;
            }
        }
        if (!std::isfinite(config.backgroundOpacity) || config.backgroundOpacity < 0 || config.backgroundOpacity > 1) {
            return std::unexpected(QStringLiteral("'appearance.background_opacity' must be a number from 0 to 1"));
        }
        const std::tuple<int, Range<int>, const char*> appearance[] = {
            {config.cornerRadius, {0, 256}, "appearance.corner_radius"},
            {config.panelBorderWidth, {0, 64}, "appearance.panel_border_width"},
            {config.fontSize, {1, 256}, "appearance.font_size"},
            {config.fontWeight, {100, 900}, "appearance.font_weight"},
            {config.historyPaddingX, {0, 512}, "appearance.history_padding_x"},
            {config.textExtraPaddingX, {0, 512}, "appearance.text_extra_padding_x"},
            {config.repeatCountThreshold, {2, 10000}, "repeat.count_threshold"},
            {config.expireAfterMs, Limits::ExpireAfterMs, "expiration.after_ms"},
            {config.fadeDurationMs, Limits::FadeDurationMs, "expiration.fade_duration_ms"},
        };
        for (const auto& [value, range, name] : appearance) {
            if (auto ok = checkRange(value, range, QString::fromLatin1(name)); !ok) {
                return ok;
            }
        }
        if (config.fontFamily.trimmed().isEmpty() || !isLabel(config.fontFamily, 128)) {
            return std::unexpected(QStringLiteral("'appearance.font_family' must be a non-empty font family name (max 128 characters)"));
        }
        for (const auto& [color, name] : {std::pair{&config.backgroundColor, "appearance.background_color"}, std::pair{&config.panelBorderColor, "appearance.panel_border_color"},
                                          std::pair{&config.foregroundColor, "appearance.foreground_color"}}) {
            if (auto ok = validateColor(*color, QString::fromLatin1(name)); !ok) {
                return ok;
            }
        }
        if (!config.symbolFontFamily.isEmpty() && (config.symbolFontFamily.trimmed().isEmpty() || !isLabel(config.symbolFontFamily, 128))) {
            return std::unexpected(QStringLiteral("'symbols.font_family' must be empty or a font family name no longer than 128 characters"));
        }
        if (!isLabel(config.spaceSymbol, 128)) {
            return std::unexpected(QStringLiteral("'symbols.space' must be a non-empty display label (max 128 characters)"));
        }
        if (auto ok = validateSymbolMap(config.keySymbols, QStringLiteral("symbols.keys")); !ok) {
            return ok;
        }
        if (auto ok = validateSymbolMap(config.modifierSymbols, QStringLiteral("symbols.modifiers")); !ok) {
            return ok;
        }
        if (config.themeId.trimmed().isEmpty() || !isLabel(config.themeId, 128)) {
            return std::unexpected(QStringLiteral("'theme.id' must be a non-empty theme identifier (max 128 characters)"));
        }
        if (!Limits::RetainedUtf16CodeUnits.contains(config.maxRetainedUtf16CodeUnits)) {
            return std::unexpected(QStringLiteral("'history.max_retained_utf16_code_units' must be in the range %1..%2")
                                       .arg(Limits::RetainedUtf16CodeUnits.minimum)
                                       .arg(Limits::RetainedUtf16CodeUnits.maximum));
        }
        return {};
    }

    OverlayConfig applyOverrides(OverlayConfig config, const ConfigOverrides& overrides) {
        const auto apply = [](auto& field, const auto& override) {
            if (override) {
                field = *override;
            }
        };
        apply(config.monitor, overrides.monitor);
        apply(config.anchor, overrides.anchor);
        apply(config.margins, overrides.margins);
        apply(config.width, overrides.width);
        apply(config.height, overrides.height);
        apply(config.backgroundOpacity, overrides.backgroundOpacity);
        apply(config.backspaceMode, overrides.backspaceMode);
        apply(config.maxRetainedUtf16CodeUnits, overrides.maxRetainedUtf16CodeUnits);
        apply(config.repeatsEnabled, overrides.repeatsEnabled);
        apply(config.expireAfterMs, overrides.expireAfterMs);
        apply(config.fadeDurationMs, overrides.fadeDurationMs);
        return config;
    }

    OverlayConfigManager::OverlayConfigManager(QString path, bool requireExisting, ConfigOverrides overrides, QObject* parent) :
        QObject(parent), m_path(QFileInfo(path).absoluteFilePath()), m_requireExisting(requireExisting), m_overrides(std::move(overrides)) {
        m_reloadTimer.setSingleShot(true);
        m_reloadTimer.setInterval(160);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { onFileSystemChange(); });
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { onFileSystemChange(); });
        connect(&m_reloadTimer, &QTimer::timeout, this, &OverlayConfigManager::reloadNow);
    }

    std::expected<void, QString> OverlayConfigManager::initialize() {
        if (m_path.isEmpty()) {
            return std::unexpected(QStringLiteral("configuration path must not be empty"));
        }
        if (!m_requireExisting && !QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            return std::unexpected(QStringLiteral("cannot create configuration directory '%1'").arg(QFileInfo(m_path).absolutePath()));
        }
        auto candidate = loadCandidate();
        if (!candidate) {
            return std::unexpected(candidate.error());
        }
        if (auto accepted = accept(std::move(*candidate)); !accepted) {
            return std::unexpected(QStringLiteral("%1: %2").arg(m_path, accepted.error()));
        }
        return {};
    }

    void OverlayConfigManager::startWatching() {
        refreshWatchTargets();
    }

    void OverlayConfigManager::setRuntimeValidator(RuntimeValidator validator) {
        m_runtimeValidator = std::move(validator);
    }

    void OverlayConfigManager::reloadNow() {
        auto outcome = loadCandidate().and_then([this](OverlayConfig candidate) {
            return accept(std::move(candidate)).transform_error([this](const QString& error) { return QStringLiteral("%1: %2").arg(m_path, error); });
        });
        if (!outcome) {
            emit reloadRejected(outcome.error());
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: keeping the last valid configuration: %1").arg(outcome.error());
        }
        refreshWatchTargets();
    }

    std::expected<OverlayConfig, QString> OverlayConfigManager::loadCandidate() const {
        OverlayConfig loaded;
        if (QFileInfo::exists(m_path)) {
            auto parsed = parseOverlayConfig(m_path);
            if (!parsed) {
                return std::unexpected(QStringLiteral("%1: %2").arg(m_path, parsed.error()));
            }
            loaded = std::move(*parsed);
        } else if (m_requireExisting) {
            return std::unexpected(QStringLiteral("configuration file '%1' does not exist").arg(m_path));
        }
        loaded = applyOverrides(std::move(loaded), m_overrides);
        if (auto valid = validateOverlayConfig(loaded); !valid) {
            return std::unexpected(QStringLiteral("%1: %2").arg(m_path, valid.error()));
        }
        return loaded;
    }

    std::expected<void, QString> OverlayConfigManager::accept(OverlayConfig candidate) {
        if (m_runtimeValidator) {
            const std::optional<OverlayConfig> previous = m_initialized ? std::optional<OverlayConfig>{m_config} : std::nullopt;
            if (auto valid = m_runtimeValidator(previous, candidate); !valid) {
                return valid;
            }
        }
        if (!m_initialized || candidate != m_config) {
            m_config      = std::move(candidate);
            m_initialized = true;
            emit configurationChanged();
        }
        return {};
    }

    void OverlayConfigManager::refreshWatchTargets() {
        const QFileInfo file(m_path);
        const QString   directory = file.absolutePath();
        if (QDir(directory).exists() && !m_watcher.directories().contains(directory)) {
            m_watcher.addPath(directory);
        }
        if (file.exists() && !m_watcher.files().contains(m_path)) {
            m_watcher.addPath(m_path);
        }
    }

    void OverlayConfigManager::onFileSystemChange() {
        refreshWatchTargets();
        m_reloadTimer.start();
    }
} // namespace Hyprcast::Overlay
