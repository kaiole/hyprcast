#include "Restart.hpp"
#include "CaptureController.hpp"
#include "OverlayInstance.hpp"
#include <QFileInfo>
#include <QLocalSocket>
#include <QPointer>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace Hyprcast::Overlay {
    namespace {
        constexpr int MaxContextBytes = 65536;
        constexpr auto BootstrapVariable = "HYPRCAST_PRIVATE_REEXEC_FD";
        bool inheritable(int fd) { return ::fcntl(fd, F_SETFD, 0) == 0; }
        void cloexec(int fd) { ::fcntl(fd, F_SETFD, FD_CLOEXEC); }
    } // namespace
    qint64 monotonicMilliseconds() {
        timespec now{};
        ::clock_gettime(CLOCK_MONOTONIC, &now);
        return qint64(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    }
    RestartHandoff::~RestartHandoff() {
        if (pending())
            finish(QStringLiteral("Replacement failed during initialization; capture remains paused"));
    }
    bool RestartHandoff::adopt(QString *error) {
        const QByteArray value = qgetenv(BootstrapVariable);
        if (value.isEmpty())
            return true;
        qunsetenv(BootstrapVariable);
        bool ok = false;
        const int fd = value.toInt(&ok);
        struct stat st{};
        if (!ok || fd < 3 || ::fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != ::getuid() || st.st_size <= 0 || st.st_size > MaxContextBytes ||
            ::fcntl(fd, F_GET_SEALS) != (F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) {
            *error = QStringLiteral("Invalid private restart bootstrap");
            return false;
        }
        QByteArray bytes(int(st.st_size), '\0');
        const auto count = ::pread(fd, bytes.data(), bytes.size(), 0);
        ::close(fd);
        const QJsonObject object = QJsonDocument::fromJson(bytes).object();
        RestartContext c;
        c.socketPath = object.value("socket").toString();
        c.configPath = object.value("config").toString();
        c.explicitConfig = object.value("explicit").toBool();
        for (const auto &arg : object.value("args").toArray())
            c.arguments.push_back(arg.toString());
        if (object.value("target").isBool())
            c.target = object.value("target").toBool();
        c.deadline = object.value("deadline").toVariant().toLongLong();
        c.lockFd = object.value("lock").toInt(-1);
        c.replyFd = object.value("reply").toInt(-1);
        int type = 0;
        socklen_t length = sizeof(type);
        if (count != bytes.size() || !c.socketPath.startsWith('/') || !c.configPath.startsWith('/') || c.lockFd < 3 || c.replyFd < 3 || c.lockFd == c.replyFd ||
            ::getsockopt(c.replyFd, SOL_SOCKET, SO_TYPE, &type, &length) || type != SOCK_STREAM) {
            *error = QStringLiteral("Invalid private restart descriptors/context");
            return false;
        }
        cloexec(c.lockFd);
        cloexec(c.replyFd);
        m_context = std::move(c);
        if (monotonicMilliseconds() >= m_context->deadline) {
            *error = QStringLiteral("Restart deadline expired before replacement initialization");
            finish(*error);
            return false;
        }
        return true;
    }
    bool RestartHandoff::execute(const RestartContext &c, int lockFd, int replyFd, const QString &executable, QString *error) {
        const int reply = ::fcntl(replyFd, F_DUPFD_CLOEXEC, 3);
        const int state = ::memfd_create("hyprcast-restart", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (reply < 0 || state < 0) {
            if (reply >= 0)
                ::close(reply);
            if (state >= 0)
                ::close(state);
            *error = QStringLiteral("Cannot create restart handoff descriptors");
            return false;
        }
        QJsonObject object{{"socket", c.socketPath},
                           {"config", c.configPath},
                           {"explicit", c.explicitConfig},
                           {"args", QJsonArray::fromStringList(c.arguments)},
                           {"deadline", c.deadline},
                           {"lock", lockFd},
                           {"reply", reply}};
        if (c.target)
            object.insert("target", *c.target);
        const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        bool prepared = bytes.size() <= MaxContextBytes && ::write(state, bytes.constData(), bytes.size()) == bytes.size() &&
                        ::fcntl(state, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) == 0 && inheritable(state) &&
                        inheritable(reply) && inheritable(lockFd);
        std::vector<QByteArray> storage{QFile::encodeName(executable)};
        for (const auto &arg : c.arguments)
            storage.push_back(arg.toLocal8Bit());
        std::vector<char *> argv;
        for (auto &arg : storage)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        // Do not rely on Qt or theme code setting CLOEXEC on every descriptor.
        // Restore the previous flags if exec fails, keeping the old runtime usable.
        std::vector<std::pair<int, int>> changedFlags;
        if (DIR *directory = ::opendir("/proc/self/fd")) {
            while (const auto *entry = ::readdir(directory)) {
                bool valid = false;
                const int fd = QByteArray(entry->d_name).toInt(&valid);
                if (!valid || fd < 3 || fd == state || fd == reply || fd == lockFd)
                    continue;
                const int flags = ::fcntl(fd, F_GETFD);
                if (flags >= 0 && !(flags & FD_CLOEXEC)) {
                    if (::fcntl(fd, F_SETFD, flags | FD_CLOEXEC))
                        prepared = false;
                    else
                        changedFlags.emplace_back(fd, flags);
                }
            }
            ::closedir(directory);
        } else
            prepared = false;
        if (prepared && monotonicMilliseconds() < c.deadline) {
            qputenv(BootstrapVariable, QByteArray::number(state));
            ::execv(storage.front().constData(), argv.data());
            *error = QStringLiteral("Executable replacement failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        } else {
            *error = QStringLiteral("Cannot prepare restart handoff, or operation deadline expired");
        }
        qunsetenv(BootstrapVariable);
        for (const auto &[fd, flags] : changedFlags)
            ::fcntl(fd, F_SETFD, flags);
        cloexec(lockFd);
        ::close(state);
        ::close(reply);
        return false;
    }
    RestartCoordinator::RestartCoordinator(OverlayInstance &instance, CaptureController &capture, RestartHandoff &handoff, RestartContext startup,
                                           QString executable, Preflight preflight, QObject *parent)
        : QObject(parent), m_instance(instance), m_capture(capture), m_handoff(handoff), m_startup(std::move(startup)), m_executable(std::move(executable)),
          m_preflight(std::move(preflight)) {
        connect(&m_instance, &OverlayInstance::restartRequested, this, [this](QLocalSocket *client) { request(client); });
        connect(&m_capture, &CaptureController::restorationFinished, this, [this](const QString &error) {
            m_handoff.finish(error.isEmpty() ? QString{} : QStringLiteral("Capture restoration failed: %1").arg(error));
            m_capture.setBlocked(false);
        });
    }
    void RestartCoordinator::request(QLocalSocket *requester) {
        if (m_capture.busy() || m_handoff.pending()) {
            OverlayInstance::reply(requester, QStringLiteral("Overlay lifecycle is busy; try again"));
            return;
        }
        const QPointer<QLocalSocket> client(requester);
        m_capture.setBlocked(true);
        RestartContext context = m_startup;
        context.target = m_capture.confirmedTarget();
        context.deadline = monotonicMilliseconds() + RestartBudgetMs;
        QString error;
        if (!QFileInfo(m_executable).isExecutable()) {
            error = QStringLiteral("Replacement executable is unavailable: %1").arg(m_executable);
        } else if (!m_preflight(&error)) {
            if (error.isEmpty())
                error = QStringLiteral("Restart preflight failed; existing overlay retained");
        } else if (client && client->state() == QLocalSocket::ConnectedState) {
            // Old objects remain alive until exec succeeds. Disconnecting the
            // requester before commit cancels the operation; after exec it does
            // not cancel a restoration whose outcome may already be committed.
            m_handoff.execute(context, m_instance.lockDescriptor(), int(client->socketDescriptor()), m_executable, &error);
        }
        m_capture.setBlocked(false);
        OverlayInstance::reply(client.data(), error);
    }
    void RestartCoordinator::initializeReplacement(const RestartContext &bootstrap) {
        m_capture.setBlocked(true);
        const int remaining = int(bootstrap.deadline - monotonicMilliseconds());
        if (remaining <= 0) {
            m_handoff.finish(QStringLiteral("Restart deadline expired during initialization; capture remains paused"));
            m_capture.setBlocked(false);
        } else if (bootstrap.target) {
            m_capture.restoreWhenReady(*bootstrap.target, bootstrap.deadline);
        } else {
            m_handoff.finish();
            m_capture.setBlocked(false);
        }
    }
    void RestartHandoff::finish(const QString &error) {
        if (!pending())
            return;
        QString clean = error;
        clean.replace('\n', ' ');
        clean.replace('\r', ' ');
        QByteArray response = error.isEmpty() ? QByteArray("ok\n") : QByteArray("error: ") + clean.toUtf8().left(1000) + '\n';
        ::send(m_context->replyFd, response.constData(), response.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
        ::close(m_context->replyFd);
        m_context.reset();
    }
} // namespace Hyprcast::Overlay
