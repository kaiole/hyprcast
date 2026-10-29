#include "Restart.hpp"
#include "CaptureController.hpp"
#include "OverlayInstance.hpp"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QPointer>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Hyprcast::Overlay {
    namespace {
        constexpr qsizetype MaxContextBytes   = 65536;
        constexpr auto      BootstrapVariable = "HYPRCAST_PRIVATE_REEXEC_FD";
        constexpr int       BootstrapSeals    = F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE;
        // Descriptors 0-2 are stdio and must never be adopted or closed.
        constexpr int FirstPrivateFd = 3;

        bool          inheritable(int fd) {
            return ::fcntl(fd, F_SETFD, 0) == 0;
        }
        void cloexec(int fd) {
            ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        }

        qint64 toWire(Deadline deadline) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(deadline.time_since_epoch()).count();
        }
        Deadline fromWire(qint64 milliseconds) {
            return Deadline{std::chrono::duration_cast<Clock::duration>(std::chrono::milliseconds{milliseconds})};
        }
    } // namespace

    int remainingMs(Deadline deadline) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
        return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(remaining, 0, std::numeric_limits<int>::max()));
    }

    RestartHandoff::~RestartHandoff() {
        finish(QStringLiteral("Replacement failed during initialization; capture remains paused"));
    }

    std::expected<void, QString> RestartHandoff::adopt() {
        const QByteArray value = qgetenv(BootstrapVariable);
        if (value.isEmpty()) {
            return {};
        }
        qunsetenv(BootstrapVariable);

        bool      ok    = false;
        const int rawFd = value.toInt(&ok);
        // Own the bootstrap descriptor immediately so every early return closes it.
        CUniqueFd   stateFd{ok && rawFd >= FirstPrivateFd ? rawFd : -1};
        struct stat st{};
        if (!stateFd || ::fstat(stateFd.get(), &st) || !S_ISREG(st.st_mode) || st.st_uid != ::getuid() || st.st_size <= 0 || st.st_size > MaxContextBytes ||
            ::fcntl(stateFd.get(), F_GET_SEALS) != BootstrapSeals) {
            return std::unexpected(QStringLiteral("Invalid private restart bootstrap"));
        }
        QByteArray bytes(static_cast<qsizetype>(st.st_size), '\0');
        const auto count = ::pread(stateFd.get(), bytes.data(), static_cast<std::size_t>(bytes.size()), 0);
        stateFd.reset();

        const QJsonObject object = QJsonDocument::fromJson(bytes).object();
        RestartContext    context;
        context.socketPath     = object.value("socket").toString();
        context.configPath     = object.value("config").toString();
        context.explicitConfig = object.value("explicit").toBool();
        for (const auto& arg : object.value("args").toArray()) {
            context.arguments.push_back(arg.toString());
        }
        if (object.value("target").isBool()) {
            context.target = object.value("target").toBool();
        }
        context.deadline = fromWire(object.value("deadline").toInteger());

        const int lockFd  = object.value("lock").toInt(-1);
        const int replyFd = object.value("reply").toInt(-1);
        if (count != bytes.size() || !context.socketPath.startsWith('/') || !context.configPath.startsWith('/') || lockFd < FirstPrivateFd || replyFd < FirstPrivateFd ||
            lockFd == replyFd) {
            return std::unexpected(QStringLiteral("Invalid private restart descriptors/context"));
        }
        // From here on the descriptors are ours, so validation failures close them.
        CUniqueFd lock{lockFd};
        CUniqueFd reply{replyFd};
        int       type   = 0;
        socklen_t length = sizeof(type);
        if (::getsockopt(reply.get(), SOL_SOCKET, SO_TYPE, &type, &length) || type != SOCK_STREAM) {
            return std::unexpected(QStringLiteral("Invalid private restart descriptors/context"));
        }
        cloexec(lock.get());
        cloexec(reply.get());

        m_context = std::move(context);
        m_lockFd  = std::move(lock);
        m_replyFd = std::move(reply);
        if (Clock::now() >= m_context->deadline) {
            const QString error = QStringLiteral("Restart deadline expired before replacement initialization");
            finish(error);
            return std::unexpected(error);
        }
        return {};
    }

    void RestartHandoff::finish(const QString& error) {
        if (!pending()) {
            return;
        }
        const QByteArray response = OverlayInstance::replyLine(error);
        ::send(m_replyFd.get(), response.constData(), static_cast<std::size_t>(response.size()), MSG_NOSIGNAL | MSG_DONTWAIT);
        m_replyFd.reset();
        m_context.reset();
    }

    QString execReplacement(const RestartContext& context, int lockFd, int requesterFd, const QString& executable) {
        const CUniqueFd reply{::fcntl(requesterFd, F_DUPFD_CLOEXEC, FirstPrivateFd)};
        const CUniqueFd state{::memfd_create("hyprcast-restart", MFD_CLOEXEC | MFD_ALLOW_SEALING)};
        if (!reply || !state) {
            return QStringLiteral("Cannot create restart handoff descriptors");
        }

        QJsonObject object{{"socket", context.socketPath},
                           {"config", context.configPath},
                           {"explicit", context.explicitConfig},
                           {"args", QJsonArray::fromStringList(context.arguments)},
                           {"deadline", toWire(context.deadline)},
                           {"lock", lockFd},
                           {"reply", reply.get()}};
        if (context.target) {
            object.insert("target", *context.target);
        }
        const QByteArray bytes    = QJsonDocument(object).toJson(QJsonDocument::Compact);
        bool             prepared = bytes.size() <= MaxContextBytes && ::write(state.get(), bytes.constData(), static_cast<std::size_t>(bytes.size())) == bytes.size() &&
            ::fcntl(state.get(), F_ADD_SEALS, BootstrapSeals) == 0 && inheritable(state.get()) && inheritable(reply.get()) && inheritable(lockFd);

        std::vector<QByteArray> storage{QFile::encodeName(executable)};
        for (const auto& arg : context.arguments) {
            storage.push_back(arg.toLocal8Bit());
        }
        std::vector<char*> argv;
        for (auto& arg : storage) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        // Do not rely on Qt or theme code setting CLOEXEC on every descriptor.
        // Restore the previous flags if exec fails, keeping the old runtime usable.
        std::vector<std::pair<int, int>> changedFlags;
        std::error_code                  iterationError;
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd", iterationError)) {
            bool      valid = false;
            const int fd    = QByteArray::fromStdString(entry.path().filename().string()).toInt(&valid);
            if (!valid || fd < FirstPrivateFd || fd == state.get() || fd == reply.get() || fd == lockFd) {
                continue;
            }
            const int flags = ::fcntl(fd, F_GETFD);
            if (flags >= 0 && !(flags & FD_CLOEXEC)) {
                if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC)) {
                    prepared = false;
                } else {
                    changedFlags.emplace_back(fd, flags);
                }
            }
        }
        if (iterationError) {
            prepared = false;
        }

        QString error;
        if (prepared && Clock::now() < context.deadline) {
            qputenv(BootstrapVariable, QByteArray::number(state.get()));
            ::execv(storage.front().constData(), argv.data());
            error = QStringLiteral("Executable replacement failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        } else {
            error = QStringLiteral("Cannot prepare restart handoff, or operation deadline expired");
        }
        qunsetenv(BootstrapVariable);
        for (const auto& [fd, flags] : changedFlags) {
            ::fcntl(fd, F_SETFD, flags);
        }
        cloexec(lockFd);
        return error; // `state` and `reply` close here.
    }

    RestartCoordinator::RestartCoordinator(OverlayInstance& instance, CaptureController& capture, RestartHandoff& handoff, RestartContext startup, QString executable,
                                           Preflight preflight, QObject* parent) :
        QObject(parent), m_instance(instance), m_capture(capture), m_handoff(handoff), m_startup(std::move(startup)), m_executable(std::move(executable)),
        m_preflight(std::move(preflight)) {
        connect(&m_instance, &OverlayInstance::restartRequested, this, [this](QLocalSocket* client) { request(client); });
        connect(&m_capture, &CaptureController::restorationFinished, this, [this](const QString& error) {
            m_handoff.finish(error.isEmpty() ? QString{} : QStringLiteral("Capture restoration failed: %1").arg(error));
            m_capture.setBlocked(false);
        });
    }

    void RestartCoordinator::request(QLocalSocket* requester) {
        if (m_capture.busy() || m_handoff.pending()) {
            OverlayInstance::reply(requester, QStringLiteral("Overlay lifecycle is busy; try again"));
            return;
        }
        const QPointer<QLocalSocket> client(requester);
        m_capture.setBlocked(true);
        RestartContext context = m_startup;
        context.target         = m_capture.confirmedTarget();
        context.deadline       = Clock::now() + RestartBudget;
        QString error;
        if (!QFileInfo(m_executable).isExecutable()) {
            error = QStringLiteral("Replacement executable is unavailable: %1").arg(m_executable);
        } else if (const auto preflight = m_preflight(); !preflight) {
            error = preflight.error().isEmpty() ? QStringLiteral("Restart preflight failed; existing overlay retained") : preflight.error();
        } else if (client && client->state() == QLocalSocket::ConnectedState) {
            // Old objects remain alive until exec succeeds. Disconnecting the
            // requester before commit cancels the operation; after exec it does
            // not cancel a restoration whose outcome may already be committed.
            error = execReplacement(context, m_instance.lockDescriptor(), static_cast<int>(client->socketDescriptor()), m_executable);
        }
        m_capture.setBlocked(false);
        OverlayInstance::reply(client.data(), error);
    }

    void RestartCoordinator::initializeReplacement(const RestartContext& bootstrap) {
        m_capture.setBlocked(true);
        if (Clock::now() >= bootstrap.deadline) {
            m_handoff.finish(QStringLiteral("Restart deadline expired during initialization; capture remains paused"));
            m_capture.setBlocked(false);
        } else if (bootstrap.target) {
            m_capture.restoreWhenReady(*bootstrap.target, bootstrap.deadline);
        } else {
            m_handoff.finish();
            m_capture.setBlocked(false);
        }
    }
} // namespace Hyprcast::Overlay
