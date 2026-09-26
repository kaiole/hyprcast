#pragma once

#include "../config/OverlayConfig.hpp"
#include "../input/KeyboardPresenter.hpp"

#include <QObject>
#include <QHash>
#include <QQuickItem>
#include <QQuickView>
#include <QUrl>
#include <QVariant>
#include <QVariantMap>

#include <memory>
#include <optional>

class QQmlContext;

namespace Hyprcast::Overlay {
    struct ThemeOption {
        QString                 type;
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

    class ThemeCatalog final {
      public:
        ThemeCatalog(QStringList themeRoots, QUrl bundledManifest);

        [[nodiscard]] bool        discover(QStringList* warnings = nullptr, QString* error = nullptr);
        [[nodiscard]] bool        resolve(const QString& id, ThemeDescriptor* descriptor, QString* error = nullptr) const;
        [[nodiscard]] bool        validateOptions(const QString& id, const QVariantMap& supplied, QVariantMap* effective, QString* error = nullptr) const;
        [[nodiscard]] QStringList availableThemeIds() const;

      private:
        [[nodiscard]] bool              loadDescriptor(const QUrl& manifestUrl, bool bundled, const QString& expectedId, ThemeDescriptor* descriptor, QString* error) const;

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
        Q_PROPERTY(QStringList heldKeys READ heldKeys NOTIFY heldKeysChanged)
        Q_PROPERTY(int heldKeyCount READ heldKeyCount NOTIFY heldKeysChanged)
        Q_PROPERTY(bool fading READ fading NOTIFY fadingChanged)
        Q_PROPERTY(int fadeDurationMs READ fadeDurationMs NOTIFY fadeDurationMsChanged)
        Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
        Q_PROPERTY(QVariantMap options READ options NOTIFY optionsChanged)

      public:
        ThemeApi(KeyboardPresenter& presenter, const OverlayConfig& config, QString themeId, QVariantMap options, QObject* parent = nullptr);

        [[nodiscard]] int version() const noexcept {
            return 1;
        }
        [[nodiscard]] QString themeId() const {
            return m_themeId;
        }
        [[nodiscard]] HistoryListModel* history() const;
        [[nodiscard]] HistoryListModel* expiredHistory() const;
        [[nodiscard]] QStringList       heldKeys() const;
        [[nodiscard]] int               heldKeyCount() const;
        [[nodiscard]] bool              fading() const;
        [[nodiscard]] int               fadeDurationMs() const;
        [[nodiscard]] QVariantMap       settings() const {
            return m_settings;
        }
        [[nodiscard]] QVariantMap options() const {
            return m_options;
        }

        void updateConfiguration(const OverlayConfig& config, QVariantMap options);

      signals:
        void heldKeysChanged();
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
        ~ThemeRuntime() override;

        [[nodiscard]] bool    loadInitial(const OverlayConfig& config, QString* error = nullptr);
        [[nodiscard]] bool    prepareSwitch(const OverlayConfig& config, QString* error = nullptr);
        void                  applyAcceptedConfiguration(const OverlayConfig& config);
        [[nodiscard]] QString activeThemeId() const;

      private:
        struct Instance {
            QString      id;
            QQuickItem*  slot    = nullptr;
            QQuickItem*  root    = nullptr;
            QQmlContext* context = nullptr;
            ThemeApi*    api     = nullptr;
        };

        [[nodiscard]] std::unique_ptr<Instance> createInstance(const OverlayConfig& config, QString* error);
        void                                    destroyInstance(std::unique_ptr<Instance> instance);
        void                                    resizeSlots();

        const ThemeCatalog&                     m_catalog;
        QQuickView&                             m_view;
        KeyboardPresenter&                      m_presenter;
        QQuickItem*                             m_host = nullptr;
        std::unique_ptr<Instance>               m_active;
        std::unique_ptr<Instance>               m_staged;
    };
} // namespace Hyprcast::Overlay
