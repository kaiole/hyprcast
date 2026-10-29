#pragma once

#include "../config/OverlayConfig.hpp"
#include "../input/KeyboardPresenter.hpp"
#include "../util/QObjectOwner.hpp"

#include <QObject>
#include <QQmlContext>
#include <QHash>
#include <QQuickItem>
#include <QQuickView>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <expected>
#include <memory>
#include <optional>

namespace Hyprcast::Overlay {
    enum class ThemeOptionType {
        Boolean,
        Integer,
        Number,
        String,
        Enum,
        Color,
    };

    struct ThemeOption {
        ThemeOptionType         type = ThemeOptionType::String;
        QVariant                defaultValue;
        std::optional<QVariant> minimum;
        std::optional<QVariant> maximum;
        QStringList             choices;
        QString                 description;
    };

    struct ThemeDescriptor {
        QString                     id;
        QString                     name;
        int                         apiVersion = 0;
        QUrl                        entryUrl;
        bool                        bundled = false;
        QHash<QString, ThemeOption> options;
    };

    // The only presentation API version this overlay implements.
    inline constexpr int ThemeApiVersion = 1;

    class ThemeCatalog final {
      public:
        ThemeCatalog(QStringList themeRoots, QUrl bundledManifest);

        // Scans the theme roots. Returns warnings about ignored themes; fails only if the
        // bundled default is unusable.
        [[nodiscard]] std::expected<QStringList, QString> discover();
        // The returned descriptor stays valid until the next discover().
        [[nodiscard]] std::expected<const ThemeDescriptor*, QString> resolve(const QString& id) const;
        // Supplied options merged over the declared defaults, after type and range checks.
        [[nodiscard]] std::expected<QVariantMap, QString> validateOptions(const QString& id, const QVariantMap& supplied) const;
        [[nodiscard]] QStringList                         availableThemeIds() const;

      private:
        QStringList                     m_themeRoots;
        QUrl                            m_bundledManifest;
        std::optional<ThemeDescriptor>  m_bundledDefault;
        QHash<QString, ThemeDescriptor> m_themes;
        QHash<QString, QString>         m_rejected;
    };

    class ThemeApi final : public QObject {
        Q_OBJECT
        Q_PROPERTY(int version READ version CONSTANT)
        Q_PROPERTY(QString themeId READ themeId CONSTANT)
        Q_PROPERTY(HistoryListModel* history READ history CONSTANT)
        Q_PROPERTY(HistoryListModel* expiredHistory READ expiredHistory CONSTANT)
        Q_PROPERTY(HistoryProjectionModel* displayHistory READ displayHistory CONSTANT)
        Q_PROPERTY(HistoryProjectionModel* expiredDisplayHistory READ expiredDisplayHistory CONSTANT)
        Q_PROPERTY(int historyCount READ historyCount NOTIFY historyCountChanged)
        Q_PROPERTY(int expiredHistoryCount READ expiredHistoryCount NOTIFY expiredHistoryCountChanged)
        Q_PROPERTY(QStringList heldKeys READ heldKeys NOTIFY heldKeysChanged)
        Q_PROPERTY(QVariantList heldKeyItems READ heldKeyItems NOTIFY heldKeysChanged)
        Q_PROPERTY(int heldKeyCount READ heldKeyCount NOTIFY heldKeysChanged)
        Q_PROPERTY(bool fading READ fading NOTIFY fadingChanged)
        Q_PROPERTY(int fadeDurationMs READ fadeDurationMs NOTIFY fadeDurationMsChanged)
        Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
        Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)

      public:
        ThemeApi(KeyboardPresenter& presenter, const OverlayConfig& config, QString themeId, QVariantMap options, QObject* parent = nullptr);

        [[nodiscard]] int version() const noexcept {
            return ThemeApiVersion;
        }
        [[nodiscard]] QString themeId() const {
            return m_themeId;
        }
        [[nodiscard]] HistoryListModel*       history() const;
        [[nodiscard]] HistoryListModel*       expiredHistory() const;
        [[nodiscard]] HistoryProjectionModel* displayHistory() const;
        [[nodiscard]] HistoryProjectionModel* expiredDisplayHistory() const;
        [[nodiscard]] int                     historyCount() const;
        [[nodiscard]] int                     expiredHistoryCount() const;
        [[nodiscard]] QStringList             heldKeys() const;
        [[nodiscard]] QVariantList            heldKeyItems() const;
        [[nodiscard]] int                     heldKeyCount() const;
        [[nodiscard]] bool                    fading() const;
        [[nodiscard]] int                     fadeDurationMs() const;
        [[nodiscard]] QVariantMap             settings() const {
            return m_settings;
        }
        [[nodiscard]] QVariantMap options() const {
            return m_options;
        }

        void updateConfiguration(const OverlayConfig& config, QVariantMap options);

      signals:
        void heldKeysChanged();
        void historyCountChanged();
        void expiredHistoryCountChanged();
        void fadingChanged();
        void fadeDurationMsChanged();
        void settingsChanged();
        void optionsChanged();

      private:
        KeyboardPresenter& m_presenter;
        QString            m_themeId;
        QVariantMap        m_settings;
        QVariantMap        m_options;
    };

    class ThemeRuntime final : public QObject {
        Q_OBJECT

      public:
        ThemeRuntime(const ThemeCatalog& catalog, QQuickView& view, KeyboardPresenter& presenter, QObject* parent = nullptr);

        [[nodiscard]] std::expected<void, QString> loadInitial(const OverlayConfig& config);
        // Builds the configured theme off-screen so a config reload can be rejected
        // before it is accepted. Does nothing if that theme is already active.
        [[nodiscard]] std::expected<void, QString> prepareSwitch(const OverlayConfig& config);
        void                                       applyAcceptedConfiguration(const OverlayConfig& config);
        [[nodiscard]] QString                      activeThemeId() const;

      private:
        // One loaded theme. Members are destroyed in reverse order, so the visual tree
        // (slot and the theme root parented to it) goes before the context its bindings use.
        // The slot is also a QObject child of the host so the scene stays discoverable.
        struct Instance {
            QString                      id;
            std::unique_ptr<QQmlContext> context;
            QObjectOwner<QQuickItem>     slot;
            QQuickItem*                  root = nullptr; // Owned by slot.
            ThemeApi*                    api  = nullptr; // Owned by context.
        };

        [[nodiscard]] std::expected<std::unique_ptr<Instance>, QString> createInstance(const OverlayConfig& config);
        void                                                            resizeSlots();

        const ThemeCatalog&                                             m_catalog;
        QQuickView&                                                     m_view;
        KeyboardPresenter&                                              m_presenter;
        QQuickItem*                                                     m_host = nullptr;
        std::unique_ptr<Instance>                                       m_active;
        std::unique_ptr<Instance>                                       m_staged;
    };
} // namespace Hyprcast::Overlay
