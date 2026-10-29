#pragma once

#include "../history/BackspaceMode.hpp"

#include <QFileSystemWatcher>
#include <QMargins>
#include <QObject>
#include <QString>
#include <QStringView>
#include <QTimer>
#include <QVariantMap>

#include <expected>
#include <functional>
#include <optional>

namespace Hyprcast::Overlay {
    // Layer-shell placement: one edge, or a corner when both axes are set.
    struct Anchor {
        enum class Vertical {
            None,
            Top,
            Bottom,
        };
        enum class Horizontal {
            None,
            Left,
            Right,
        };

        Vertical    vertical   = Vertical::Bottom;
        Horizontal  horizontal = Horizontal::Right;

        friend bool operator==(const Anchor&, const Anchor&) = default;
    };

    enum class PanelVisibility {
        Always,
        WithContent,
        Never,
    };

    enum class RepeatPresentation {
        Expanded,
        Counted,
    };

    // Text forms used in TOML, on the command line and in theme QML.
    [[nodiscard]] std::expected<Anchor, QString>             parseAnchor(QStringView text);
    [[nodiscard]] std::expected<BackspaceMode, QString>      parseBackspaceMode(QStringView text);
    [[nodiscard]] std::expected<PanelVisibility, QString>    parsePanelVisibility(QStringView text);
    [[nodiscard]] std::expected<RepeatPresentation, QString> parseRepeatPresentation(QStringView text);
    [[nodiscard]] QString                                    toString(Anchor anchor);
    [[nodiscard]] QString                                    toString(PanelVisibility visibility);
    [[nodiscard]] QString                                    toString(RepeatPresentation presentation);

    template <typename T>
    struct Range {
        T                            minimum;
        T                            maximum;

        [[nodiscard]] constexpr bool contains(T value) const noexcept {
            return value >= minimum && value <= maximum;
        }
    };

    // Accepted ranges, shared by config validation and command-line parsing.
    namespace Limits {
        inline constexpr Range<int>       SurfaceSize{1, 8192};
        inline constexpr Range<int>       Margin{0, 8192};
        inline constexpr Range<int>       ExpireAfterMs{0, 86'400'000};
        inline constexpr Range<int>       FadeDurationMs{0, 60'000};
        inline constexpr Range<qsizetype> RetainedUtf16CodeUnits{1, 1'048'576};
    }

    struct OverlayConfig {
        QString            monitor;
        Anchor             anchor;
        QMargins           margins{24, 24, 24, 24};
        int                width        = 600;
        int                height       = 88;
        bool               dynamicSize  = true;
        int                minWidth     = 240;
        int                minHeight    = 64;
        bool               clickThrough = true;

        QString            backgroundColor   = QStringLiteral("#0e1116");
        double             backgroundOpacity = 0.78;
        int                cornerRadius      = 12;
        int                panelBorderWidth  = 1;
        QString            panelBorderColor  = QStringLiteral("#ffffff");
        QString            foregroundColor   = QStringLiteral("#ffffff");
        QString            fontFamily        = QStringLiteral("Sans Serif");
        int                fontSize          = 30;
        int                fontWeight        = 500;
        int                historyPaddingX   = 16;
        int                textExtraPaddingX = 4;

        QString            symbolFontFamily;
        QString            spaceSymbol = QStringLiteral(" ");
        QVariantMap        keySymbols;
        QVariantMap        modifierSymbols;

        QString            themeId = QStringLiteral("builtin:default");
        QVariantMap        themeOptions;
        PanelVisibility    panelVisibility           = PanelVisibility::WithContent;
        BackspaceMode      backspaceMode             = BackspaceMode::Delete;
        qsizetype          maxRetainedUtf16CodeUnits = 4096;
        bool               repeatsEnabled            = true;
        RepeatPresentation repeatPresentation        = RepeatPresentation::Counted;
        int                repeatCountThreshold      = 4;
        int                expireAfterMs             = 3000;
        int                fadeDurationMs            = 250;

        friend bool        operator==(const OverlayConfig&, const OverlayConfig&) = default;
    };

    struct ConfigOverrides {
        std::optional<QString>       monitor;
        std::optional<Anchor>        anchor;
        std::optional<QMargins>      margins;
        std::optional<int>           width;
        std::optional<int>           height;
        std::optional<double>        backgroundOpacity;
        std::optional<BackspaceMode> backspaceMode;
        std::optional<qsizetype>     maxRetainedUtf16CodeUnits;
        std::optional<bool>          repeatsEnabled;
        std::optional<int>           expireAfterMs;
        std::optional<int>           fadeDurationMs;
    };

    [[nodiscard]] QString                               defaultConfigPath(QString* warning = nullptr);
    [[nodiscard]] QVariantMap                           overlayConfigToQmlValues(const OverlayConfig& config);
    [[nodiscard]] std::expected<OverlayConfig, QString> parseOverlayConfig(const QString& path);
    [[nodiscard]] std::expected<void, QString>          validateOverlayConfig(const OverlayConfig& config);
    [[nodiscard]] OverlayConfig                         applyOverrides(OverlayConfig config, const ConfigOverrides& overrides);

    class OverlayConfigManager final : public QObject {
        Q_OBJECT
      public:
        using RuntimeValidator = std::function<std::expected<void, QString>(const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate)>;

        OverlayConfigManager(QString path, bool requireExisting, ConfigOverrides overrides = {}, QObject* parent = nullptr);

        [[nodiscard]] std::expected<void, QString> initialize();
        void                                       startWatching();
        void                                       setRuntimeValidator(RuntimeValidator validator);
        void                                       reloadNow();

        [[nodiscard]] const OverlayConfig&         config() const noexcept {
            return m_config;
        }
        [[nodiscard]] const QString& configPath() const noexcept {
            return m_path;
        }

      signals:
        void configurationChanged();
        void reloadRejected(const QString& reason);

      private:
        [[nodiscard]] std::expected<OverlayConfig, QString> loadCandidate() const;
        [[nodiscard]] std::expected<void, QString>          accept(OverlayConfig candidate);
        void                                                refreshWatchTargets();
        void                                                onFileSystemChange();

        QString                                             m_path;
        bool                                                m_requireExisting = false;
        bool                                                m_initialized     = false;
        ConfigOverrides                                     m_overrides;
        OverlayConfig                                       m_config;
        RuntimeValidator                                    m_runtimeValidator;
        QFileSystemWatcher                                  m_watcher;
        QTimer                                              m_reloadTimer;
    };
} // namespace Hyprcast::Overlay
