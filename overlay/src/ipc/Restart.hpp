#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

class QLocalSocket;

namespace Hyprcast::Overlay {
    inline constexpr int RestartBudgetMs = 10000;
    inline constexpr int RestartReplyMs = 12000;
    // Private, bounded exec bootstrap. No input history or effective configuration.
    struct RestartContext {
        QString socketPath;
        QString configPath;
        bool explicitConfig = false;
        QStringList arguments;
        std::optional<bool> target;
        qint64 deadline = 0;
        int lockFd = -1;
        int replyFd = -1;
    };
    qint64 monotonicMilliseconds();
    class OverlayInstance;
    class CaptureController;
    class RestartHandoff {
      public:
        ~RestartHandoff();
        bool adopt(QString *error);
        bool execute(const RestartContext &context, int lockFd, int replyFd, const QString &executable, QString *error);
        void finish(const QString &error = {});
        bool pending() const { return m_context.has_value(); }
        const RestartContext &context() const { return *m_context; }

      private:
        std::optional<RestartContext> m_context;
    };
    // Resident lifecycle gate; preflight is supplied by the GUI/config layer.
    class RestartCoordinator final : public QObject {
      public:
        using Preflight = std::function<bool(QString *)>;
        RestartCoordinator(OverlayInstance &instance, CaptureController &capture, RestartHandoff &handoff, RestartContext startup, QString executable,
                           Preflight preflight, QObject *parent = nullptr);
        void initializeReplacement(const RestartContext &bootstrap);

      private:
        void request(QLocalSocket *requester);
        OverlayInstance &m_instance;
        CaptureController &m_capture;
        RestartHandoff &m_handoff;
        RestartContext m_startup;
        QString m_executable;
        Preflight m_preflight;
    };
} // namespace Hyprcast::Overlay
