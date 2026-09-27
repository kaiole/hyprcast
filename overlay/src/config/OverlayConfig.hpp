#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QMargins>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace Hyprcast::Overlay {
    struct OverlayConfig {
        QString     monitor;
        QString     anchor = QStringLiteral("bottom-right");
        QMargins    margins{24, 24, 24, 24};
        int         width        = 600;
        int         height       = 88;
        bool        dynamicSize  = true;
        int         minWidth     = 240;
        int         minHeight    = 64;
        bool        clickThrough = true;

        QString     backgroundColor   = QStringLiteral("#0e1116");
        double      backgroundOpacity = 0.78;
        int         cornerRadius      = 12;
        int         panelBorderWidth  = 1;
        QString     panelBorderColor  = QStringLiteral("#ffffff");
        QString     foregroundColor   = QStringLiteral("#ffffff");
        QString     fontFamily        = QStringLiteral("Sans Serif");
        int         fontSize          = 30;
        int         fontWeight        = 500;
        int         historyPaddingX   = 16;
        int         textExtraPaddingX = 4;

        int         keycapFontSize       = 19;
        int         keycapHeight         = 40;
        int         keycapPaddingX       = 9;
        int         keycapRadius         = 7;
        int         keycapSpacing        = 6;
        int         keycapInnerSpacing   = 3;
        QString     keycapTextBackground = QStringLiteral("#2b3546");
        QString     keycapKeyBackground  = QStringLiteral("#39475c");
        QString     keycapBorderColor    = QStringLiteral("#66758c");
        int         keycapBorderWidth    = 1;
        QString     keycapTextColor      = QStringLiteral("#ffffff");

        int         heldFontSize         = 14;
        int         heldKeyHeight        = 22;
        int         heldKeyPaddingX      = 7;
        int         heldKeyRadius        = 5;
        int         heldKeySpacing       = 5;
        int         heldRowPaddingX      = 16;
        int         heldRowPaddingBottom = 10;
        QString     heldKeyBackground    = QStringLiteral("#46536a");
        QString     heldKeyTextColor     = QStringLiteral("#ffffff");
        int         heldKeyBorderWidth   = 0;
        QString     heldKeyBorderColor   = QStringLiteral("#66758c");

        QString     symbolFontFamily;
        QVariantMap keySymbols;
        QVariantMap modifierSymbols;

        QString     presentation = QStringLiteral("text");
        QString     themeId      = QStringLiteral("builtin:default");
        QVariantMap themeOptions;
        bool        showHeldKeys              = false;
        QString     panelVisibility           = QStringLiteral("with-content");
        QString     backspaceMode             = QStringLiteral("delete");
        qsizetype   maxRetainedUtf16CodeUnits = 4096;
        bool        repeatsEnabled            = true;
        QString     repeatPresentation        = QStringLiteral("counted");
        int         repeatCountThreshold      = 4;
        int         expireAfterMs             = 3000;
        int         fadeDurationMs            = 250;

        friend bool operator==(const OverlayConfig&, const OverlayConfig&) = default;
    };

    struct ConfigOverrides {
        std::optional<QString>   monitor;
        std::optional<QString>   anchor;
        std::optional<QMargins>  margins;
        std::optional<int>       width;
        std::optional<int>       height;
        std::optional<double>    backgroundOpacity;
        std::optional<QString>   presentation;
        std::optional<bool>      showHeldKeys;
        std::optional<QString>   backspaceMode;
        std::optional<qsizetype> maxRetainedUtf16CodeUnits;
        std::optional<bool>      repeatsEnabled;
        std::optional<int>       expireAfterMs;
        std::optional<int>       fadeDurationMs;
    };

    [[nodiscard]] QString       defaultConfigPath(QString* warning = nullptr);
    [[nodiscard]] QVariantMap   overlayConfigToQmlValues(const OverlayConfig& config);
    [[nodiscard]] bool          parseOverlayConfig(const QString& path, OverlayConfig* config, QString* error);
    [[nodiscard]] bool          validateOverlayConfig(OverlayConfig* config, QString* error);
    [[nodiscard]] OverlayConfig applyOverrides(OverlayConfig config, const ConfigOverrides& overrides);

    class OverlayConfigManager final : public QObject {
        Q_OBJECT
      public:
        using RuntimeValidator = std::function<bool(const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate, QString* error)>;

        OverlayConfigManager(QString path, bool requireExisting, ConfigOverrides overrides = {}, QObject* parent = nullptr);

        [[nodiscard]] bool                 initialize(QString* error = nullptr);
        void                               startWatching();
        void                               setRuntimeValidator(RuntimeValidator validator);
        void                               reloadNow();

        [[nodiscard]] const OverlayConfig& config() const noexcept {
            return m_config;
        }
        [[nodiscard]] QString configPath() const {
            return m_path;
        }
      signals:
        void configurationChanged();
        void reloadRejected(const QString& reason);

      private:
        [[nodiscard]] bool loadCandidate(OverlayConfig* candidate, QString* error) const;
        [[nodiscard]] bool accept(OverlayConfig candidate, QString* error);
        void               refreshWatchTargets();
        void               onFileSystemChange();

        QString            m_path;
        bool               m_requireExisting = false;
        bool               m_initialized     = false;
        ConfigOverrides    m_overrides;
        OverlayConfig      m_config;
        RuntimeValidator   m_runtimeValidator;
        QFileSystemWatcher m_watcher;
        QTimer             m_reloadTimer;
    };
} // namespace Hyprcast::Overlay
