#pragma once

#include "hyprcast/os/UniqueFd.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <expected>
#include <functional>
#include <optional>

class QLocalSocket;

namespace Hyprcast::Overlay {
    // steady_clock is CLOCK_MONOTONIC on Linux, so deadlines survive exec() unchanged.
    using Clock    = std::chrono::steady_clock;
    using Deadline = Clock::time_point;

    inline constexpr std::chrono::milliseconds RestartBudget{10'000};
    inline constexpr std::chrono::milliseconds RestartReplyTimeout{12'000};

    // Milliseconds left until `deadline` as a Qt timer interval, clamped to [0, INT_MAX].
    [[nodiscard]] int remainingMs(Deadline deadline);

    // Startup settings carried across exec. Plain data: the descriptors that travel
    // with it are owned separately, so copying a context never duplicates ownership.
    struct RestartContext {
        QString             socketPath;
        QString             configPath;
        bool                explicitConfig = false;
        QStringList         arguments;
        std::optional<bool> target;
        Deadline            deadline{};
    };

    // Receiving side of a restart: owns the bootstrap descriptors handed over by the
    // previous process and reports the final outcome to the original requester.
    class RestartHandoff {
      public:
        RestartHandoff() = default;
        // Replies with an error if the replacement never finished initializing.
        ~RestartHandoff();

        // The destructor replies on m_replyFd, so a moved-from handoff would need a
        // valid "empty" state; none is needed, so moving is disabled outright.
        RestartHandoff(const RestartHandoff&)            = delete;
        RestartHandoff& operator=(const RestartHandoff&) = delete;
        RestartHandoff(RestartHandoff&&)                 = delete;
        RestartHandoff& operator=(RestartHandoff&&)      = delete;

        // Adopts the private bootstrap left in the environment, if there is one.
        [[nodiscard]] std::expected<void, QString> adopt();
        void                                       finish(const QString& error = {});

        [[nodiscard]] bool                         pending() const noexcept {
            return m_context.has_value();
        }
        [[nodiscard]] const std::optional<RestartContext>& context() const noexcept {
            return m_context;
        }
        // Transfers the inherited ownership lock to the new OverlayInstance.
        [[nodiscard]] CUniqueFd takeLock() noexcept {
            return std::move(m_lockFd);
        }

      private:
        std::optional<RestartContext> m_context;
        CUniqueFd                     m_lockFd;
        CUniqueFd                     m_replyFd;
    };

    // Replaces the current process image with `executable`, handing over the borrowed
    // ownership lock and requester socket. Returns only on failure, with the reason.
    [[nodiscard]] QString execReplacement(const RestartContext& context, int lockFd, int requesterFd, const QString& executable);

    class OverlayInstance;
    class CaptureController;

    // Resident lifecycle gate; preflight is supplied by the GUI/config layer.
    class RestartCoordinator final : public QObject {
      public:
        using Preflight = std::function<std::expected<void, QString>()>;
        RestartCoordinator(OverlayInstance& instance, CaptureController& capture, RestartHandoff& handoff, RestartContext startup, QString executable, Preflight preflight,
                           QObject* parent = nullptr);
        void initializeReplacement(const RestartContext& bootstrap);

      private:
        void               request(QLocalSocket* requester);

        OverlayInstance&   m_instance;
        CaptureController& m_capture;
        RestartHandoff&    m_handoff;
        RestartContext     m_startup;
        QString            m_executable;
        Preflight          m_preflight;
    };
} // namespace Hyprcast::Overlay
