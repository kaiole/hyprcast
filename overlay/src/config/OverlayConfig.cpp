#include "OverlayConfig.hpp"

#include <toml++/toml.hpp>

#include <QColor>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

namespace Hyprcast::Overlay {
    namespace {
        using Table = toml::table;

        bool fail(QString* error, const QString& message) {
            if (error) {
                *error = message;
            }
            return false;
        }

        void annotatePath(const QString& path, QString* error) {
            if (error && !error->startsWith(path + QStringLiteral(":"))) {
                *error = QStringLiteral("%1: %2").arg(path, *error);
            }
        }

        bool hasOnlyKeys(const Table& table, std::initializer_list<std::string_view> allowed, const QString& prefix, QString* error) {
            for (const auto& [key, value] : table) {
                Q_UNUSED(value);
                const std::string_view candidate = key.str();
                if (std::find(allowed.begin(), allowed.end(), candidate) == allowed.end()) {
                    const QString name = prefix.isEmpty() ? QString::fromUtf8(candidate.data(), static_cast<qsizetype>(candidate.size())) :
                                                            prefix + QLatin1Char('.') + QString::fromUtf8(candidate.data(), static_cast<qsizetype>(candidate.size()));
                    return fail(error, QStringLiteral("unknown configuration key '%1'").arg(name));
                }
            }
            return true;
        }

        const Table* readTable(const Table& root, std::string_view name, QString* error) {
            const auto* node = root.get(name);
            if (!node) {
                return nullptr;
            }
            const auto* table = node->as_table();
            if (!table) {
                fail(error, QStringLiteral("'%1' must be a TOML table").arg(QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))));
                return nullptr;
            }
            return table;
        }

        bool readString(const Table& table, std::string_view key, QString* output, QString* error) {
            const auto* node = table.get(key);
            if (!node) {
                return true;
            }
            const auto value = node->value_exact<std::string>();
            if (!value) {
                return fail(error, QStringLiteral("'%1' must be a string").arg(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()))));
            }
            *output = QString::fromUtf8(value->data(), static_cast<qsizetype>(value->size()));
            return true;
        }

        bool readInteger(const Table& table, std::string_view key, int* output, QString* error) {
            const auto* node = table.get(key);
            if (!node) {
                return true;
            }
            const auto value = node->value_exact<std::int64_t>();
            if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
                return fail(error, QStringLiteral("'%1' must be an integer").arg(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()))));
            }
            *output = static_cast<int>(*value);
            return true;
        }

        bool readBoolean(const Table& table, std::string_view key, bool* output, QString* error) {
            const auto* node = table.get(key);
            if (!node) {
                return true;
            }
            const auto value = node->value_exact<bool>();
            if (!value) {
                return fail(error, QStringLiteral("'%1' must be a boolean").arg(QString::fromUtf8(key.data(), static_cast<qsizetype>(key.size()))));
            }
            *output = *value;
            return true;
        }

        bool readStringMap(const Table& table, QVariantMap* output, QString* error, const QString& prefix) {
            for (const auto& [key, node] : table) {
                const QString name  = QString::fromUtf8(key.str().data(), static_cast<qsizetype>(key.str().size()));
                const auto    value = node.value_exact<std::string>();
                if (!value) {
                    return fail(error, QStringLiteral("'%1.%2' must be a string").arg(prefix, name));
                }
                output->insert(name, QString::fromUtf8(value->data(), static_cast<qsizetype>(value->size())));
            }
            return true;
        }

        bool readOpacity(const Table& table, QString* error, double* output) {
            const auto* node = table.get("background_opacity");
            if (!node) {
                return true;
            }
            const auto value = node->value<double>();
            if (!value) {
                return fail(error, QStringLiteral("'appearance.background_opacity' must be a number"));
            }
            *output = *value;
            return true;
        }

        bool readMargins(const Table& table, QMargins* output, QString* error) {
            const auto* node = table.get("margins");
            if (!node) {
                return true;
            }
            const auto* array = node->as_array();
            if (!array || array->size() != 4) {
                return fail(error, QStringLiteral("'window.margins' must be an array of four integers: left, top, right, bottom"));
            }
            int values[4]{};
            for (qsizetype i = 0; i < 4; ++i) {
                const auto value = (*array)[static_cast<std::size_t>(i)].value_exact<std::int64_t>();
                if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
                    return fail(error, QStringLiteral("'window.margins' must contain four integers: left, top, right, bottom"));
                }
                values[i] = static_cast<int>(*value);
            }
            *output = QMargins(values[0], values[1], values[2], values[3]);
            return true;
        }

        bool readSymbols(const Table& root, OverlayConfig* config, QString* error) {
            const Table* symbols = readTable(root, "symbols", error);
            if (!symbols) {
                return !error || error->isEmpty();
            }
            if (!hasOnlyKeys(*symbols, {"font_family", "keys", "modifiers"}, QStringLiteral("symbols"), error) ||
                !readString(*symbols, "font_family", &config->symbolFontFamily, error)) {
                return false;
            }
            if (const Table* keys = readTable(*symbols, "keys", error)) {
                if (!readStringMap(*keys, &config->keySymbols, error, QStringLiteral("symbols.keys"))) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }
            if (const Table* modifiers = readTable(*symbols, "modifiers", error)) {
                if (!readStringMap(*modifiers, &config->modifierSymbols, error, QStringLiteral("symbols.modifiers"))) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }
            return true;
        }

        bool readTheme(const Table& root, OverlayConfig* config, QString* error) {
            const Table* table = readTable(root, "theme", error);
            if (!table) {
                return !error || error->isEmpty();
            }
            if (!hasOnlyKeys(*table, {"id", "options"}, QStringLiteral("theme"), error) || !readString(*table, "id", &config->themeId, error)) {
                return false;
            }
            const Table* options = readTable(*table, "options", error);
            if (!options) {
                return !error || error->isEmpty();
            }
            for (const auto& [key, node] : *options) {
                const QString name = QString::fromUtf8(key.str().data(), static_cast<qsizetype>(key.str().size()));
                QVariant      value;
                if (const auto item = node.value_exact<std::string>()) {
                    value = QString::fromUtf8(item->data(), static_cast<qsizetype>(item->size()));
                } else if (const auto item = node.value_exact<bool>()) {
                    value = *item;
                } else if (const auto item = node.value_exact<std::int64_t>()) {
                    value = QVariant::fromValue<qlonglong>(*item);
                } else if (const auto item = node.value_exact<double>()) {
                    value = *item;
                } else {
                    return fail(error, QStringLiteral("'theme.options.%1' must be a string, boolean, integer, or number").arg(name));
                }
                config->themeOptions.insert(name, std::move(value));
            }
            return true;
        }

        bool readFontWeight(const Table& table, int* output, QString* error) {
            QString weight;
            if (!readString(table, "font_weight", &weight, error)) {
                return false;
            }
            if (!table.get("font_weight")) {
                return true;
            }
            weight = weight.toLower();
            if (weight == QStringLiteral("normal")) {
                *output = 400;
            } else if (weight == QStringLiteral("medium")) {
                *output = 500;
            } else if (weight == QStringLiteral("demibold")) {
                *output = 600;
            } else if (weight == QStringLiteral("bold")) {
                *output = 700;
            } else {
                return fail(error, QStringLiteral("'appearance.font_weight' must be normal, medium, demibold, or bold"));
            }
            return true;
        }

        bool validateAnchor(QString* anchor, QString* error) {
            *anchor                 = anchor->toLower();
            const QStringList parts = anchor->split(QLatin1Char('-'), Qt::SkipEmptyParts);
            if (parts.isEmpty() || parts.size() > 2) {
                return fail(error, QStringLiteral("'window.anchor' must be one edge or a corner (for example 'bottom-right')"));
            }
            bool horizontal = false;
            bool vertical   = false;
            for (const auto& part : parts) {
                if (part == QStringLiteral("top") || part == QStringLiteral("bottom")) {
                    if (vertical) {
                        return fail(error, QStringLiteral("'window.anchor' cannot contain two vertical edges"));
                    }
                    vertical = true;
                } else if (part == QStringLiteral("left") || part == QStringLiteral("right")) {
                    if (horizontal) {
                        return fail(error, QStringLiteral("'window.anchor' cannot contain two horizontal edges"));
                    }
                    horizontal = true;
                } else {
                    return fail(error, QStringLiteral("invalid 'window.anchor'; use top, bottom, left, right, or a corner such as bottom-right"));
                }
            }
            return true;
        }

        bool validateColor(const QString& value, const QString& name, QString* error) {
            if (value.isEmpty() || !QColor(value).isValid()) {
                return fail(error, QStringLiteral("'%1' must be a valid Qt color (for example '#ffffff')").arg(name));
            }
            return true;
        }

        bool inRange(int value, int minimum, int maximum, const QString& name, QString* error) {
            if (value < minimum || value > maximum) {
                return fail(error, QStringLiteral("'%1' must be in the range %2..%3").arg(name).arg(minimum).arg(maximum));
            }
            return true;
        }

        bool readConfigTables(const Table& root, OverlayConfig* config, QString* error) {
            if (!hasOnlyKeys(root, {"window", "appearance", "display", "history", "repeat", "expiration", "theme", "symbols"}, {}, error)) {
                return false;
            }

            if (const Table* table = readTable(root, "window", error)) {
                if (!hasOnlyKeys(*table, {"monitor", "anchor", "margins", "width", "height", "min_width", "min_height", "dynamic_size", "click_through"}, QStringLiteral("window"),
                                 error) ||
                    !readString(*table, "monitor", &config->monitor, error) || !readString(*table, "anchor", &config->anchor, error) ||
                    !readMargins(*table, &config->margins, error) || !readInteger(*table, "width", &config->width, error) ||
                    !readInteger(*table, "height", &config->height, error) || !readInteger(*table, "min_width", &config->minWidth, error) ||
                    !readInteger(*table, "min_height", &config->minHeight, error) || !readBoolean(*table, "dynamic_size", &config->dynamicSize, error) ||
                    !readBoolean(*table, "click_through", &config->clickThrough, error)) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }

            if (const Table* table = readTable(root, "appearance", error)) {
                if (!hasOnlyKeys(*table,
                                 {"background_color",
                                  "background_opacity",
                                  "corner_radius",
                                  "panel_border_width",
                                  "panel_border_color",
                                  "foreground_color",
                                  "font_family",
                                  "font_size",
                                  "font_weight",
                                  "history_padding_x",
                                  "text_extra_padding_x",
                                  "keycap_font_size",
                                  "keycap_height",
                                  "keycap_padding_x",
                                  "keycap_radius",
                                  "keycap_spacing",
                                  "keycap_inner_spacing",
                                  "keycap_text_background",
                                  "keycap_key_background",
                                  "keycap_border_color",
                                  "keycap_border_width",
                                  "keycap_text_color",
                                  "held_font_size",
                                  "held_key_height",
                                  "held_key_padding_x",
                                  "held_key_radius",
                                  "held_key_spacing",
                                  "held_row_padding_x",
                                  "held_row_padding_bottom",
                                  "held_key_background",
                                  "held_key_text_color",
                                  "held_key_border_width",
                                  "held_key_border_color"},
                                 QStringLiteral("appearance"), error) ||
                    !readString(*table, "background_color", &config->backgroundColor, error) || !readOpacity(*table, error, &config->backgroundOpacity) ||
                    !readInteger(*table, "corner_radius", &config->cornerRadius, error) || !readInteger(*table, "panel_border_width", &config->panelBorderWidth, error) ||
                    !readString(*table, "panel_border_color", &config->panelBorderColor, error) || !readString(*table, "foreground_color", &config->foregroundColor, error) ||
                    !readString(*table, "font_family", &config->fontFamily, error) || !readInteger(*table, "font_size", &config->fontSize, error) ||
                    !readFontWeight(*table, &config->fontWeight, error) || !readInteger(*table, "history_padding_x", &config->historyPaddingX, error) ||
                    !readInteger(*table, "text_extra_padding_x", &config->textExtraPaddingX, error) || !readInteger(*table, "keycap_font_size", &config->keycapFontSize, error) ||
                    !readInteger(*table, "keycap_height", &config->keycapHeight, error) || !readInteger(*table, "keycap_padding_x", &config->keycapPaddingX, error) ||
                    !readInteger(*table, "keycap_radius", &config->keycapRadius, error) || !readInteger(*table, "keycap_spacing", &config->keycapSpacing, error) ||
                    !readInteger(*table, "keycap_inner_spacing", &config->keycapInnerSpacing, error) ||
                    !readString(*table, "keycap_text_background", &config->keycapTextBackground, error) ||
                    !readString(*table, "keycap_key_background", &config->keycapKeyBackground, error) ||
                    !readString(*table, "keycap_border_color", &config->keycapBorderColor, error) ||
                    !readInteger(*table, "keycap_border_width", &config->keycapBorderWidth, error) || !readString(*table, "keycap_text_color", &config->keycapTextColor, error) ||
                    !readInteger(*table, "held_font_size", &config->heldFontSize, error) || !readInteger(*table, "held_key_height", &config->heldKeyHeight, error) ||
                    !readInteger(*table, "held_key_padding_x", &config->heldKeyPaddingX, error) || !readInteger(*table, "held_key_radius", &config->heldKeyRadius, error) ||
                    !readInteger(*table, "held_key_spacing", &config->heldKeySpacing, error) || !readInteger(*table, "held_row_padding_x", &config->heldRowPaddingX, error) ||
                    !readInteger(*table, "held_row_padding_bottom", &config->heldRowPaddingBottom, error) ||
                    !readString(*table, "held_key_background", &config->heldKeyBackground, error) || !readString(*table, "held_key_text_color", &config->heldKeyTextColor, error) ||
                    !readInteger(*table, "held_key_border_width", &config->heldKeyBorderWidth, error) ||
                    !readString(*table, "held_key_border_color", &config->heldKeyBorderColor, error)) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }

            if (!readTheme(root, config, error) || !readSymbols(root, config, error)) {
                return false;
            }

            if (const Table* table = readTable(root, "display", error)) {
                if (!hasOnlyKeys(*table, {"presentation", "show_held_keys", "panel_visibility"}, QStringLiteral("display"), error) ||
                    !readString(*table, "presentation", &config->presentation, error) || !readBoolean(*table, "show_held_keys", &config->showHeldKeys, error) ||
                    !readString(*table, "panel_visibility", &config->panelVisibility, error)) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }

            if (const Table* table = readTable(root, "history", error)) {
                if (!hasOnlyKeys(*table, {"backspace", "max_retained_utf16_code_units"}, QStringLiteral("history"), error) ||
                    !readString(*table, "backspace", &config->backspaceMode, error)) {
                    return false;
                }
                if (const auto* node = table->get("max_retained_utf16_code_units")) {
                    const auto value = node->value_exact<std::int64_t>();
                    if (!value || *value < std::numeric_limits<qsizetype>::min() || *value > std::numeric_limits<qsizetype>::max()) {
                        return fail(error, QStringLiteral("'history.max_retained_utf16_code_units' must be an integer"));
                    }
                    config->maxRetainedUtf16CodeUnits = static_cast<qsizetype>(*value);
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }

            if (const Table* table = readTable(root, "repeat", error)) {
                if (!hasOnlyKeys(*table, {"enabled", "presentation", "count_threshold"}, QStringLiteral("repeat"), error) ||
                    !readBoolean(*table, "enabled", &config->repeatsEnabled, error) || !readString(*table, "presentation", &config->repeatPresentation, error) ||
                    !readInteger(*table, "count_threshold", &config->repeatCountThreshold, error)) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }

            if (const Table* table = readTable(root, "expiration", error)) {
                if (!hasOnlyKeys(*table, {"after_ms", "fade_duration_ms"}, QStringLiteral("expiration"), error) ||
                    !readInteger(*table, "after_ms", &config->expireAfterMs, error) || !readInteger(*table, "fade_duration_ms", &config->fadeDurationMs, error)) {
                    return false;
                }
            } else if (error && !error->isEmpty()) {
                return false;
            }
            return true;
        }

        QVariantMap toQmlValues(const OverlayConfig& c) {
            return {{QStringLiteral("width"), c.width},
                    {QStringLiteral("height"), c.height},
                    {QStringLiteral("anchor"), c.anchor},
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
                    {QStringLiteral("keycapFontSize"), c.keycapFontSize},
                    {QStringLiteral("keycapHeight"), c.keycapHeight},
                    {QStringLiteral("keycapPaddingX"), c.keycapPaddingX},
                    {QStringLiteral("keycapRadius"), c.keycapRadius},
                    {QStringLiteral("keycapSpacing"), c.keycapSpacing},
                    {QStringLiteral("keycapInnerSpacing"), c.keycapInnerSpacing},
                    {QStringLiteral("keycapTextBackground"), c.keycapTextBackground},
                    {QStringLiteral("keycapKeyBackground"), c.keycapKeyBackground},
                    {QStringLiteral("keycapBorderColor"), c.keycapBorderColor},
                    {QStringLiteral("keycapBorderWidth"), c.keycapBorderWidth},
                    {QStringLiteral("keycapTextColor"), c.keycapTextColor},
                    {QStringLiteral("heldFontSize"), c.heldFontSize},
                    {QStringLiteral("heldKeyHeight"), c.heldKeyHeight},
                    {QStringLiteral("heldKeyPaddingX"), c.heldKeyPaddingX},
                    {QStringLiteral("heldKeyRadius"), c.heldKeyRadius},
                    {QStringLiteral("heldKeySpacing"), c.heldKeySpacing},
                    {QStringLiteral("heldRowPaddingX"), c.heldRowPaddingX},
                    {QStringLiteral("heldRowPaddingBottom"), c.heldRowPaddingBottom},
                    {QStringLiteral("heldKeyBackground"), c.heldKeyBackground},
                    {QStringLiteral("heldKeyTextColor"), c.heldKeyTextColor},
                    {QStringLiteral("heldKeyBorderWidth"), c.heldKeyBorderWidth},
                    {QStringLiteral("heldKeyBorderColor"), c.heldKeyBorderColor},
                    {QStringLiteral("symbolFontFamily"), c.symbolFontFamily},
                    {QStringLiteral("keySymbols"), c.keySymbols},
                    {QStringLiteral("modifierSymbols"), c.modifierSymbols},
                    {QStringLiteral("presentation"), c.presentation},
                    {QStringLiteral("showHeldKeys"), c.showHeldKeys},
                    {QStringLiteral("panelVisibility"), c.panelVisibility},
                    {QStringLiteral("repeatPresentation"), c.repeatPresentation},
                    {QStringLiteral("repeatCountThreshold"), c.repeatCountThreshold}};
        }
    } // namespace

    QVariantMap overlayConfigToQmlValues(const OverlayConfig& config) {
        return toQmlValues(config);
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

    bool parseOverlayConfig(const QString& path, OverlayConfig* config, QString* error) {
        QString localError;
        if (!error) {
            error = &localError;
        }
        if (!config) {
            return fail(error, QStringLiteral("internal error: null configuration destination"));
        }
        error->clear();
        try {
            const Table   parsed = toml::parse_file(path.toStdString());
            OverlayConfig candidate;
            if (!readConfigTables(parsed, &candidate, error) || !validateOverlayConfig(&candidate, error)) {
                return false;
            }
            *config = std::move(candidate);
            return true;
        } catch (const toml::parse_error& exception) { return fail(error, QString::fromUtf8(exception.what())); } catch (const std::exception& exception) {
            return fail(error, QString::fromUtf8(exception.what()));
        }
    }

    bool validateOverlayConfig(OverlayConfig* config, QString* error) {
        QString localError;
        if (!error) {
            error = &localError;
        }
        if (!config) {
            return fail(error, QStringLiteral("internal error: null configuration"));
        }
        error->clear();
        if (config->monitor.size() > 256 || config->monitor.contains(QChar::Null)) {
            return fail(error, QStringLiteral("'window.monitor' must be at most 256 characters and contain no NUL"));
        }
        if (!validateAnchor(&config->anchor, error)) {
            return false;
        }
        if (!inRange(config->width, 1, 8192, QStringLiteral("window.width"), error) || !inRange(config->height, 1, 8192, QStringLiteral("window.height"), error) ||
            !inRange(config->minWidth, 1, 8192, QStringLiteral("window.min_width"), error) || !inRange(config->minHeight, 1, 8192, QStringLiteral("window.min_height"), error)) {
            return false;
        }
        if (config->dynamicSize && (config->minWidth > config->width || config->minHeight > config->height)) {
            return fail(error, QStringLiteral("when window.dynamic_size is enabled, min_width and min_height must not exceed width and height"));
        }
        const int margins[] = {config->margins.left(), config->margins.top(), config->margins.right(), config->margins.bottom()};
        for (const int margin : margins) {
            if (!inRange(margin, 0, 8192, QStringLiteral("window.margins"), error)) {
                return false;
            }
        }
        if (!std::isfinite(config->backgroundOpacity) || config->backgroundOpacity < 0 || config->backgroundOpacity > 1) {
            return fail(error, QStringLiteral("'appearance.background_opacity' must be a number from 0 to 1"));
        }
        if (!inRange(config->cornerRadius, 0, 256, QStringLiteral("appearance.corner_radius"), error) ||
            !inRange(config->panelBorderWidth, 0, 64, QStringLiteral("appearance.panel_border_width"), error) ||
            !inRange(config->keycapBorderWidth, 0, 64, QStringLiteral("appearance.keycap_border_width"), error) ||
            !inRange(config->heldKeyBorderWidth, 0, 64, QStringLiteral("appearance.held_key_border_width"), error) ||
            !inRange(config->fontSize, 1, 256, QStringLiteral("appearance.font_size"), error) ||
            !inRange(config->fontWeight, 100, 900, QStringLiteral("appearance.font_weight"), error) ||
            !inRange(config->historyPaddingX, 0, 512, QStringLiteral("appearance.history_padding_x"), error) ||
            !inRange(config->textExtraPaddingX, 0, 512, QStringLiteral("appearance.text_extra_padding_x"), error) ||
            !inRange(config->keycapFontSize, 1, 256, QStringLiteral("appearance.keycap_font_size"), error) ||
            !inRange(config->keycapHeight, 1, 512, QStringLiteral("appearance.keycap_height"), error) ||
            !inRange(config->keycapPaddingX, 0, 512, QStringLiteral("appearance.keycap_padding_x"), error) ||
            !inRange(config->keycapRadius, 0, 256, QStringLiteral("appearance.keycap_radius"), error) ||
            !inRange(config->keycapSpacing, 0, 128, QStringLiteral("appearance.keycap_spacing"), error) ||
            !inRange(config->keycapInnerSpacing, 0, 128, QStringLiteral("appearance.keycap_inner_spacing"), error) ||
            !inRange(config->heldFontSize, 1, 256, QStringLiteral("appearance.held_font_size"), error) ||
            !inRange(config->heldKeyHeight, 1, 512, QStringLiteral("appearance.held_key_height"), error) ||
            !inRange(config->heldKeyPaddingX, 0, 512, QStringLiteral("appearance.held_key_padding_x"), error) ||
            !inRange(config->heldKeyRadius, 0, 256, QStringLiteral("appearance.held_key_radius"), error) ||
            !inRange(config->heldKeySpacing, 0, 128, QStringLiteral("appearance.held_key_spacing"), error) ||
            !inRange(config->heldRowPaddingX, 0, 512, QStringLiteral("appearance.held_row_padding_x"), error) ||
            !inRange(config->heldRowPaddingBottom, 0, 512, QStringLiteral("appearance.held_row_padding_bottom"), error)) {
            return false;
        }
        if (config->fontFamily.trimmed().isEmpty() || config->fontFamily.size() > 128 || config->fontFamily.contains(QChar::Null)) {
            return fail(error, QStringLiteral("'appearance.font_family' must be a non-empty font family name (max 128 characters)"));
        }
        if (!validateColor(config->backgroundColor, QStringLiteral("appearance.background_color"), error) ||
            !validateColor(config->panelBorderColor, QStringLiteral("appearance.panel_border_color"), error) ||
            !validateColor(config->foregroundColor, QStringLiteral("appearance.foreground_color"), error) ||
            !validateColor(config->keycapTextBackground, QStringLiteral("appearance.keycap_text_background"), error) ||
            !validateColor(config->keycapKeyBackground, QStringLiteral("appearance.keycap_key_background"), error) ||
            !validateColor(config->keycapBorderColor, QStringLiteral("appearance.keycap_border_color"), error) ||
            !validateColor(config->keycapTextColor, QStringLiteral("appearance.keycap_text_color"), error) ||
            !validateColor(config->heldKeyBackground, QStringLiteral("appearance.held_key_background"), error) ||
            !validateColor(config->heldKeyTextColor, QStringLiteral("appearance.held_key_text_color"), error) ||
            !validateColor(config->heldKeyBorderColor, QStringLiteral("appearance.held_key_border_color"), error)) {
            return false;
        }
        if (!config->symbolFontFamily.isEmpty() &&
            (config->symbolFontFamily.trimmed().isEmpty() || config->symbolFontFamily.size() > 128 || config->symbolFontFamily.contains(QChar::Null))) {
            return fail(error, QStringLiteral("'symbols.font_family' must be empty or a font family name no longer than 128 characters"));
        }
        const auto validateSymbolMap = [error](const QVariantMap& mappings, const QString& path) {
            if (mappings.size() > 256) {
                return fail(error, QStringLiteral("'%1' may contain at most 256 mappings").arg(path));
            }
            for (auto it = mappings.cbegin(); it != mappings.cend(); ++it) {
                const QString value = it.value().toString();
                if (it.key().isEmpty() || it.key().size() > 64 || it.key().contains(QChar::Null) || value.isEmpty() || value.size() > 128 || value.contains(QChar::Null)) {
                    return fail(error, QStringLiteral("'%1.%2' needs a non-empty identity (max 64 characters) and display label (max 128 characters)").arg(path, it.key()));
                }
            }
            return true;
        };
        if (!validateSymbolMap(config->keySymbols, QStringLiteral("symbols.keys")) || !validateSymbolMap(config->modifierSymbols, QStringLiteral("symbols.modifiers"))) {
            return false;
        }
        if (config->themeId.trimmed().isEmpty() || config->themeId.size() > 128 || config->themeId.contains(QChar::Null)) {
            return fail(error, QStringLiteral("'theme.id' must be a non-empty theme identifier (max 128 characters)"));
        }
        config->presentation = config->presentation.toLower();
        if (config->presentation != QStringLiteral("text") && config->presentation != QStringLiteral("keycaps")) {
            return fail(error, QStringLiteral("'display.presentation' must be 'text' or 'keycaps'"));
        }
        config->panelVisibility = config->panelVisibility.toLower();
        if (config->panelVisibility != QStringLiteral("always") && config->panelVisibility != QStringLiteral("with-content") &&
            config->panelVisibility != QStringLiteral("never")) {
            return fail(error, QStringLiteral("'display.panel_visibility' must be 'always', 'with-content', or 'never'"));
        }
        config->backspaceMode = config->backspaceMode.toLower();
        if (config->backspaceMode != QStringLiteral("delete") && config->backspaceMode != QStringLiteral("symbol")) {
            return fail(error, QStringLiteral("'history.backspace' must be 'delete' or 'symbol'"));
        }
        config->repeatPresentation = config->repeatPresentation.toLower();
        if (config->repeatPresentation != QStringLiteral("expanded") && config->repeatPresentation != QStringLiteral("counted")) {
            return fail(error, QStringLiteral("'repeat.presentation' must be 'expanded' or 'counted'"));
        }
        if (!inRange(config->repeatCountThreshold, 2, 10000, QStringLiteral("repeat.count_threshold"), error)) {
            return false;
        }
        if (config->maxRetainedUtf16CodeUnits < 1 || config->maxRetainedUtf16CodeUnits > 1'048'576) {
            return fail(error, QStringLiteral("'history.max_retained_utf16_code_units' must be in the range 1..1048576"));
        }
        if (!inRange(config->expireAfterMs, 0, 86'400'000, QStringLiteral("expiration.after_ms"), error) ||
            !inRange(config->fadeDurationMs, 0, 60'000, QStringLiteral("expiration.fade_duration_ms"), error)) {
            return false;
        }
        return true;
    }

    OverlayConfig applyOverrides(OverlayConfig config, const ConfigOverrides& overrides) {
        if (overrides.monitor)
            config.monitor = *overrides.monitor;
        if (overrides.anchor)
            config.anchor = *overrides.anchor;
        if (overrides.margins)
            config.margins = *overrides.margins;
        if (overrides.width)
            config.width = *overrides.width;
        if (overrides.height)
            config.height = *overrides.height;
        if (overrides.backgroundOpacity)
            config.backgroundOpacity = *overrides.backgroundOpacity;
        if (overrides.presentation)
            config.presentation = *overrides.presentation;
        if (overrides.showHeldKeys)
            config.showHeldKeys = *overrides.showHeldKeys;
        if (overrides.backspaceMode)
            config.backspaceMode = *overrides.backspaceMode;
        if (overrides.maxRetainedUtf16CodeUnits)
            config.maxRetainedUtf16CodeUnits = *overrides.maxRetainedUtf16CodeUnits;
        if (overrides.repeatsEnabled)
            config.repeatsEnabled = *overrides.repeatsEnabled;
        if (overrides.expireAfterMs)
            config.expireAfterMs = *overrides.expireAfterMs;
        if (overrides.fadeDurationMs)
            config.fadeDurationMs = *overrides.fadeDurationMs;
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

    bool OverlayConfigManager::initialize(QString* error) {
        if (error) {
            error->clear();
        }
        if (m_path.isEmpty()) {
            return fail(error, QStringLiteral("configuration path must not be empty"));
        }
        if (!m_requireExisting && !QDir().mkpath(QFileInfo(m_path).absolutePath())) {
            return fail(error, QStringLiteral("cannot create configuration directory '%1'").arg(QFileInfo(m_path).absolutePath()));
        }

        OverlayConfig candidate;
        if (!loadCandidate(&candidate, error)) {
            return false;
        }
        if (!accept(std::move(candidate), error)) {
            annotatePath(m_path, error);
            return false;
        }
        return true;
    }

    void OverlayConfigManager::startWatching() {
        refreshWatchTargets();
    }

    void OverlayConfigManager::setRuntimeValidator(RuntimeValidator validator) {
        m_runtimeValidator = std::move(validator);
    }

    void OverlayConfigManager::reloadNow() {
        OverlayConfig candidate;
        QString       error;
        const bool    loaded = loadCandidate(&candidate, &error);
        if (!loaded || !accept(std::move(candidate), &error)) {
            annotatePath(m_path, &error);
            emit reloadRejected(error);
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: keeping the last valid configuration: %1").arg(error);
        }
        refreshWatchTargets();
    }

    bool OverlayConfigManager::loadCandidate(OverlayConfig* candidate, QString* error) const {
        QString localError;
        if (!error) {
            error = &localError;
        }
        error->clear();
        const QFileInfo configFile(m_path);
        OverlayConfig   loaded;
        if (configFile.exists()) {
            if (!parseOverlayConfig(m_path, &loaded, error)) {
                if (error) {
                    *error = QStringLiteral("%1: %2").arg(m_path, *error);
                }
                return false;
            }
        } else if (m_requireExisting) {
            return fail(error, QStringLiteral("configuration file '%1' does not exist").arg(m_path));
        }
        loaded = applyOverrides(std::move(loaded), m_overrides);
        if (!validateOverlayConfig(&loaded, error)) {
            if (error) {
                *error = QStringLiteral("%1: %2").arg(m_path, *error);
            }
            return false;
        }
        *candidate = std::move(loaded);
        return true;
    }

    bool OverlayConfigManager::accept(OverlayConfig candidate, QString* error) {
        if (m_runtimeValidator) {
            const std::optional<OverlayConfig> previous = m_initialized ? std::optional<OverlayConfig>{m_config} : std::nullopt;
            if (!m_runtimeValidator(previous, candidate, error)) {
                return false;
            }
        }
        if (!m_initialized || candidate != m_config) {
            m_config      = std::move(candidate);
            m_initialized = true;
            emit configurationChanged();
        }
        return true;
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
