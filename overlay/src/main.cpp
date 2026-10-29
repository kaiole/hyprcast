#include "config/OverlayConfig.hpp"
#include "hyprcast/Version.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/CaptureController.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/OverlayInstance.hpp"
#include "ipc/Restart.hpp"
#include "theme/Theme.hpp"

#include <LayerShellQt/Window>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMargins>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QScreen>
#include <QSize>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <array>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

namespace {
    using namespace Hyprcast::Overlay;
    using LayerWindow = LayerShellQt::Window;

    const QString ThemeHostUrl         = QStringLiteral("qrc:/hyprcast/overlay/qml/ThemeHost.qml");
    const QString BundledThemeManifest = QStringLiteral("qrc:/hyprcast/overlay/qml/themes/default/theme.toml");

    // Exit codes: usage and configuration errors are distinguished from runtime failures.
    constexpr int ExitFailure = 1;
    constexpr int ExitUsage   = 2;

    void          writeError(const QString& message) {
        QTextStream(stderr) << "hyprcast-overlay: " << message << Qt::endl;
    }

    void writeWarning(const QString& message) {
        qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(message);
    }

    std::expected<int, QString> parseInteger(const QString& value, Range<int> range, const QString& option) {
        bool      ok     = false;
        const int parsed = value.toInt(&ok);
        if (!ok || !range.contains(parsed)) {
            return std::unexpected(QStringLiteral("--%1 must be an integer from %2 to %3").arg(option).arg(range.minimum).arg(range.maximum));
        }
        return parsed;
    }

    std::expected<QMargins, QString> parseMargins(const QString& value) {
        const auto error =
            std::unexpected(QStringLiteral("--margins must contain four integers from %1 to %2: left,top,right,bottom").arg(Limits::Margin.minimum).arg(Limits::Margin.maximum));
        const auto parts = value.split(QLatin1Char(','));
        if (parts.size() != 4) {
            return error;
        }
        std::array<int, 4> parsed{};
        for (std::size_t i = 0; i < parsed.size(); ++i) {
            bool ok   = false;
            parsed[i] = parts[static_cast<qsizetype>(i)].toInt(&ok);
            if (!ok || !Limits::Margin.contains(parsed[i])) {
                return error;
            }
        }
        return QMargins(parsed[0], parsed[1], parsed[2], parsed[3]);
    }

    LayerWindow::Anchors toLayerAnchors(Anchor anchor) {
        LayerWindow::Anchors anchors;
        if (anchor.vertical == Anchor::Vertical::Top) {
            anchors |= LayerWindow::AnchorTop;
        } else if (anchor.vertical == Anchor::Vertical::Bottom) {
            anchors |= LayerWindow::AnchorBottom;
        }
        if (anchor.horizontal == Anchor::Horizontal::Left) {
            anchors |= LayerWindow::AnchorLeft;
        } else if (anchor.horizontal == Anchor::Horizontal::Right) {
            anchors |= LayerWindow::AnchorRight;
        }
        return anchors;
    }

    QScreen* resolveScreen(const QGuiApplication& application, const QString& requestedName) {
        if (requestedName.isEmpty()) {
            return application.primaryScreen();
        }
        for (QScreen* screen : application.screens()) {
            if (screen->name() == requestedName) {
                return screen;
            }
        }
        return nullptr;
    }

    QString unavailableScreenMessage(const QGuiApplication& application, const QString& requestedName) {
        if (requestedName.isEmpty()) {
            return QStringLiteral("No display is available");
        }
        QStringList available;
        for (QScreen* screen : application.screens()) {
            available.push_back(screen->name());
        }
        return QStringLiteral("Unknown monitor '%1'; available outputs: %2").arg(requestedName, available.join(QStringLiteral(", ")));
    }

    std::expected<void, QString> requireScreen(const QGuiApplication& application, const QString& monitor) {
        if (!resolveScreen(application, monitor)) {
            return std::unexpected(unavailableScreenMessage(application, monitor));
        }
        return {};
    }

    InputHistoryOptions historyOptions(const OverlayConfig& config) {
        return {
            .backspaceMode             = config.backspaceMode,
            .maxRetainedUtf16CodeUnits = config.maxRetainedUtf16CodeUnits,
            .presentation =
                {
                    .countedRepeats   = config.repeatPresentation == RepeatPresentation::Counted,
                    .repeatThreshold  = config.repeatCountThreshold,
                    .symbolFontFamily = config.symbolFontFamily,
                    .spaceSymbol      = config.spaceSymbol,
                    .keySymbols       = config.keySymbols,
                    .modifierSymbols  = config.modifierSymbols,
                },
        };
    }

    QStringList themeRoots() {
        QStringList roots;
        for (const QString& dataLocation : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
            roots.push_back(QDir(dataLocation).filePath(QStringLiteral("hyprcast/themes")));
        }
        return roots;
    }

    // The command line, parsed once. Option objects live here so helpers need no long
    // parameter lists.
    class CommandLine {
      public:
        enum class Command {
            Start,
            Toggle,
            Restart,
        };

        CommandLine() {
            m_parser.setApplicationDescription(QStringLiteral("A lightweight Hyprland keyboard overlay."));
            m_parser.addPositionalArgument(QStringLiteral("command"),
                                           QStringLiteral("toggle: launch/enable or toggle capture; restart: replace this session's overlay and restore capture (selectors only)."),
                                           QStringLiteral("[toggle|restart]"));
            m_parser.addOptions({m_config, m_monitor, m_anchor, m_margins, m_width, m_height, m_opacity, m_expireAfter, m_fadeDuration, m_backspace, m_retention, m_repeatEnabled,
                                 m_socket, m_instance, m_quitAfter});
        }

        // Handles --help and --version itself (they exit), then validates the command.
        [[nodiscard]] std::expected<Command, QString> parse(const QStringList& arguments) {
            if (!m_parser.parse(arguments)) {
                return std::unexpected(m_parser.errorText());
            }
            if (m_parser.isSet(m_help)) {
                m_parser.showHelp();
            }
            if (m_parser.isSet(m_version)) {
                m_parser.showVersion();
            }

            const QStringList commands = m_parser.positionalArguments();
            Command           command  = Command::Start;
            if (commands == QStringList{QStringLiteral("toggle")}) {
                command = Command::Toggle;
            } else if (commands == QStringList{QStringLiteral("restart")}) {
                command = Command::Restart;
            } else if (!commands.isEmpty()) {
                return std::unexpected(QStringLiteral("Expected no command, 'toggle', or 'restart'"));
            }
            if (command == Command::Restart) {
                for (const auto& name : m_parser.optionNames()) {
                    if (name != m_socket.names().front() && name != m_instance.names().front()) {
                        return std::unexpected(QStringLiteral("restart accepts only --socket and --instance-signature; resident startup overrides are preserved"));
                    }
                }
            }
            return command;
        }

        [[nodiscard]] std::expected<QString, QString> socketPath() const {
            if (m_parser.isSet(m_socket)) {
                const QString path = m_parser.value(m_socket);
                if (path.isEmpty()) {
                    return std::unexpected(QStringLiteral("--socket must not be empty"));
                }
                return QFileInfo(path).absoluteFilePath();
            }

            const QString runtimeDirectory = qEnvironmentVariable("XDG_RUNTIME_DIR");
            if (runtimeDirectory.isEmpty() || !QDir::isAbsolutePath(runtimeDirectory)) {
                return std::unexpected(QStringLiteral("XDG_RUNTIME_DIR must be set to an absolute path (or pass --socket)"));
            }
            const QString signature = m_parser.isSet(m_instance) ? m_parser.value(m_instance) : qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
            if (signature.isEmpty() || signature == u"." || signature == u".." || signature.contains(QLatin1Char('/')) || signature.contains(QChar::Null)) {
                return std::unexpected(QStringLiteral("Set HYPRLAND_INSTANCE_SIGNATURE or pass --instance-signature (or --socket)"));
            }
            return QDir(runtimeDirectory).filePath(QStringLiteral("hyprcast/%1/events.sock").arg(signature));
        }

        [[nodiscard]] std::expected<ConfigOverrides, QString> overrides() const {
            ConfigOverrides overrides;
            if (m_parser.isSet(m_monitor)) {
                overrides.monitor = m_parser.value(m_monitor);
            }
            if (m_parser.isSet(m_anchor)) {
                const auto anchor = parseAnchor(m_parser.value(m_anchor));
                if (!anchor) {
                    return std::unexpected(QStringLiteral("--anchor: %1").arg(anchor.error()));
                }
                overrides.anchor = *anchor;
            }
            if (m_parser.isSet(m_margins)) {
                const auto margins = parseMargins(m_parser.value(m_margins));
                if (!margins) {
                    return std::unexpected(margins.error());
                }
                overrides.margins = *margins;
            }
            for (const auto& [option, target, range] :
                 {std::tuple{&m_width, &overrides.width, Limits::SurfaceSize}, std::tuple{&m_height, &overrides.height, Limits::SurfaceSize},
                  std::tuple{&m_expireAfter, &overrides.expireAfterMs, Limits::ExpireAfterMs}, std::tuple{&m_fadeDuration, &overrides.fadeDurationMs, Limits::FadeDurationMs}}) {
                if (!m_parser.isSet(*option)) {
                    continue;
                }
                const auto value = parseInteger(m_parser.value(*option), range, option->names().front());
                if (!value) {
                    return std::unexpected(value.error());
                }
                *target = *value;
            }
            if (m_parser.isSet(m_opacity)) {
                bool         ok      = false;
                const double opacity = m_parser.value(m_opacity).toDouble(&ok);
                if (!ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
                    return std::unexpected(QStringLiteral("--background-opacity must be a number from 0 to 1"));
                }
                overrides.backgroundOpacity = opacity;
            }
            if (m_parser.isSet(m_backspace)) {
                const auto mode = parseBackspaceMode(m_parser.value(m_backspace));
                if (!mode) {
                    return std::unexpected(QStringLiteral("--backspace-mode must be delete or symbol"));
                }
                overrides.backspaceMode = *mode;
            }
            if (m_parser.isSet(m_retention)) {
                constexpr Range<int> retention{static_cast<int>(Limits::RetainedUtf16CodeUnits.minimum), static_cast<int>(Limits::RetainedUtf16CodeUnits.maximum)};
                const auto           units = parseInteger(m_parser.value(m_retention), retention, m_retention.names().front());
                if (!units) {
                    return std::unexpected(units.error());
                }
                overrides.maxRetainedUtf16CodeUnits = *units;
            }
            if (m_parser.isSet(m_repeatEnabled)) {
                const QString value = m_parser.value(m_repeatEnabled).toLower();
                if (value != u"true" && value != u"false") {
                    return std::unexpected(QStringLiteral("--repeat-enabled must be true or false"));
                }
                overrides.repeatsEnabled = value == u"true";
            }
            return overrides;
        }

        // Explicit --config path, if given.
        [[nodiscard]] std::optional<QString> configPath() const {
            return m_parser.isSet(m_config) ? std::optional(m_parser.value(m_config)) : std::nullopt;
        }

        [[nodiscard]] std::expected<int, QString> quitAfterMs() const {
            if (!m_parser.isSet(m_quitAfter)) {
                return 0;
            }
            return parseInteger(m_parser.value(m_quitAfter), {1, std::numeric_limits<int>::max()}, m_quitAfter.names().front());
        }

        // Arguments for a restarted resident: the same startup overrides, minus selectors and
        // one-shot options.
        [[nodiscard]] QStringList replacementArguments(const QString& socketPath) const {
            QStringList arguments{QStringLiteral("--socket"), socketPath};
            for (const QString& name : m_parser.optionNames()) {
                if (name == u"socket" || name == u"instance-signature" || name == u"config" || name == u"quit-after-ms") {
                    continue;
                }
                arguments << QStringLiteral("--") + name << m_parser.value(name);
            }
            return arguments;
        }

      private:
        static QCommandLineOption option(const QString& name, const QString& description, const QString& value) {
            return QCommandLineOption(name, description, value);
        }

        QCommandLineParser       m_parser;
        const QCommandLineOption m_help    = m_parser.addHelpOption();
        const QCommandLineOption m_version = m_parser.addVersionOption();
        const QCommandLineOption m_config  = option(QStringLiteral("config"), QStringLiteral("Load this TOML configuration file (must exist)."), QStringLiteral("path"));
        const QCommandLineOption m_monitor{
            {QStringLiteral("m"), QStringLiteral("monitor")}, QStringLiteral("Output name (defaults to the primary output)."), QStringLiteral("name")};
        const QCommandLineOption m_anchor =
            option(QStringLiteral("anchor"), QStringLiteral("Position: top, bottom, left, right, or a corner such as bottom-right."), QStringLiteral("position"));
        const QCommandLineOption m_margins = option(QStringLiteral("margins"), QStringLiteral("Layer-shell margins in left,top,right,bottom order."), QStringLiteral("pixels"));
        const QCommandLineOption m_width   = option(QStringLiteral("width"), QStringLiteral("Maximum surface/panel width in logical pixels."), QStringLiteral("pixels"));
        const QCommandLineOption m_height  = option(QStringLiteral("height"), QStringLiteral("Maximum surface/panel height in logical pixels."), QStringLiteral("pixels"));
        const QCommandLineOption m_opacity = option(QStringLiteral("background-opacity"),
                                                    QStringLiteral("Panel fill opacity from 0 (transparent) to 1 (opaque); borders are independent."), QStringLiteral("alpha"));
        const QCommandLineOption m_expireAfter =
            option(QStringLiteral("expire-after-ms"), QStringLiteral("Remove history after this much inactivity; 0 disables expiration."), QStringLiteral("ms"));
        const QCommandLineOption m_fadeDuration =
            option(QStringLiteral("fade-duration-ms"), QStringLiteral("Fade expired history over this duration; 0 disables fading."), QStringLiteral("ms"));
        const QCommandLineOption m_backspace = option(QStringLiteral("backspace-mode"), QStringLiteral("Plain Backspace behavior: delete or symbol."), QStringLiteral("mode"));
        const QCommandLineOption m_retention =
            option(QStringLiteral("max-retained-utf16-code-units"), QStringLiteral("History retention limit in formatted UTF-16 code units."), QStringLiteral("units"));
        const QCommandLineOption m_repeatEnabled =
            option(QStringLiteral("repeat-enabled"), QStringLiteral("Enable locally generated key repeats (true or false)."), QStringLiteral("boolean"));
        const QCommandLineOption m_socket =
            option(QStringLiteral("socket"), QStringLiteral("Connect to this Hyprcast event socket instead of discovering it from the environment."), QStringLiteral("path"));
        const QCommandLineOption m_instance =
            option(QStringLiteral("instance-signature"), QStringLiteral("Hyprland instance signature (defaults to HYPRLAND_INSTANCE_SIGNATURE)."), QStringLiteral("signature"));
        const QCommandLineOption m_quitAfter =
            option(QStringLiteral("quit-after-ms"), QStringLiteral("Exit after this many milliseconds (useful for smoke tests)."), QStringLiteral("ms"));
    };

    // Settings that pick which configuration file the overlay reads.
    struct ConfigSource {
        QString path;
        bool    explicitPath = false;
    };

    std::expected<ConfigSource, QString> resolveConfigSource(const CommandLine& commandLine, const std::optional<RestartContext>& bootstrap) {
        if (bootstrap) {
            return ConfigSource{.path = bootstrap->configPath, .explicitPath = bootstrap->explicitConfig};
        }
        if (const auto path = commandLine.configPath()) {
            if (path->isEmpty()) {
                return std::unexpected(QStringLiteral("--config path must not be empty"));
            }
            return ConfigSource{.path = QFileInfo(*path).absoluteFilePath(), .explicitPath = true};
        }
        QString       warning;
        const QString path = defaultConfigPath(&warning);
        if (!warning.isEmpty()) {
            writeWarning(warning);
        }
        return ConfigSource{.path = QFileInfo(path).absoluteFilePath(), .explicitPath = false};
    }

    // Applies window settings that must be in place before the surface is first shown.
    void configureWindow(QQuickView& view, LayerWindow& layerWindow, const OverlayConfig& config, QScreen* screen) {
        view.setTitle(QStringLiteral("Hyprcast"));
        view.setScreen(screen);
        view.resize(config.width, config.height);
        view.setColor(Qt::transparent);
        QSurfaceFormat surfaceFormat = view.format();
        surfaceFormat.setAlphaBufferSize(8);
        view.setFormat(surfaceFormat);
        Qt::WindowFlags flags = view.flags() | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
        if (config.clickThrough) {
            flags |= Qt::WindowTransparentForInput;
        }
        view.setFlags(flags);

        layerWindow.setScope(QStringLiteral("hyprcast-overlay"));
        layerWindow.setLayer(LayerWindow::LayerOverlay);
        layerWindow.setAnchors(toLayerAnchors(config.anchor));
        layerWindow.setMargins(config.margins);
        layerWindow.setDesiredSize(QSize(config.width, config.height));
        layerWindow.setScreen(screen);
        layerWindow.setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
        layerWindow.setActivateOnShow(false);
        layerWindow.setCloseOnDismissed(true);
    }

    std::expected<void, QString> loadThemeHost(QQuickView& view) {
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.setSource(QUrl(ThemeHostUrl));
        if (view.status() == QQuickView::Error) {
            QStringList errors;
            for (const auto& error : view.errors()) {
                errors.push_back(error.toString());
            }
            return std::unexpected(errors.join(QLatin1Char('\n')));
        }
        return {};
    }

    // Loads the startup configuration and theme in isolation, so a restart is refused
    // while the running overlay is still intact. Trusted QML is instantiated in a
    // separate engine, never the active cache.
    std::expected<void, QString> preflightRestart(const QGuiApplication& application, const ConfigSource& source, const ConfigOverrides& overrides) {
        ThemeCatalog catalog(themeRoots(), QUrl(BundledThemeManifest));
        if (auto discovered = catalog.discover(); !discovered) {
            return std::unexpected(discovered.error());
        }
        OverlayConfigManager candidate(source.path, source.explicitPath, overrides);
        candidate.setRuntimeValidator([&](const std::optional<OverlayConfig>&, const OverlayConfig& config) -> std::expected<void, QString> {
            return requireScreen(application, config.monitor).and_then([&] {
                return catalog.validateOptions(config.themeId, config.themeOptions).transform([](const QVariantMap&) {});
            });
        });
        if (auto initialized = candidate.initialize(); !initialized) {
            return initialized;
        }
        KeyboardPresenter presenter;
        QQuickView        probe;
        probe.setScreen(resolveScreen(application, candidate.config().monitor));
        probe.resize(candidate.config().width, candidate.config().height);
        if (!loadThemeHost(probe)) {
            return std::unexpected(QStringLiteral("Cannot initialize isolated theme preflight host"));
        }
        ThemeRuntime runtime(catalog, probe, presenter);
        return runtime.loadInitial(candidate.config());
    }
} // namespace

int main(int argc, char* argv[]) {
    // Adopt restart descriptors before Qt opens anything of its own.
    RestartHandoff handoff;
    if (const auto adopted = handoff.adopt(); !adopted) {
        writeError(adopted.error());
        return ExitFailure;
    }
    // A plain copy of the bootstrap data; the descriptors stay owned by `handoff`.
    const std::optional<RestartContext> bootstrap = handoff.context();

    QGuiApplication                     application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprcast-overlay"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(Hyprcast::PROJECT_VERSION));

    CommandLine commandLine;
    const auto  command = commandLine.parse(application.arguments());
    if (!command) {
        writeError(command.error());
        return ExitUsage;
    }
    const auto socketPath = commandLine.socketPath();
    if (!socketPath) {
        writeError(socketPath.error());
        return ExitUsage;
    }

    OverlayInstance instance(*socketPath);
    const auto      instanceCommand = *command == CommandLine::Command::Restart ? OverlayInstance::Command::Restart :
        *command == CommandLine::Command::Toggle                                ? OverlayInstance::Command::Toggle :
                                                                                  OverlayInstance::Command::Start;
    const auto      ownership       = instance.acquire(instanceCommand, handoff.takeLock());
    if (!ownership) {
        writeError(ownership.error());
        return ExitFailure;
    }
    if (*ownership == OverlayInstance::Ownership::Forwarded) {
        return EXIT_SUCCESS;
    }

    // From here on this process is the resident overlay.
    const auto overrides = commandLine.overrides();
    if (!overrides) {
        writeError(overrides.error());
        return ExitUsage;
    }
    const auto configSource = resolveConfigSource(commandLine, bootstrap);
    if (!configSource) {
        writeError(configSource.error());
        return ExitUsage;
    }
    const auto quitAfterMs = commandLine.quitAfterMs();
    if (!quitAfterMs) {
        writeError(quitAfterMs.error());
        return ExitUsage;
    }

    ThemeCatalog themeCatalog(themeRoots(), QUrl(BundledThemeManifest));
    const auto   themeWarnings = themeCatalog.discover();
    if (!themeWarnings) {
        writeError(themeWarnings.error());
        return ExitFailure;
    }
    for (const QString& warning : *themeWarnings) {
        writeWarning(warning);
    }

    // Declaration order is lifetime order: each object outlives everything declared
    // after it, so references captured below never dangle during teardown.
    KeyboardPresenter keyboardPresenter;
    QQuickView        view;
    QObject::connect(view.engine(), &QQmlEngine::warnings, &view, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            writeWarning(QStringLiteral("QML: %1").arg(warning.toString()));
        }
    });
    if (const auto hosted = loadThemeHost(view); !hosted) {
        writeError(hosted.error());
        return ExitFailure;
    }
    ThemeRuntime         theme(themeCatalog, view, keyboardPresenter);

    OverlayConfigManager configuration(configSource->path, configSource->explicitPath, *overrides);
    configuration.setRuntimeValidator([&](const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate) -> std::expected<void, QString> {
        if (previous && previous->monitor != candidate.monitor) {
            return std::unexpected(QStringLiteral("window.monitor cannot be changed live; restart the overlay to apply it"));
        }
        if (previous && previous->clickThrough != candidate.clickThrough) {
            return std::unexpected(QStringLiteral("window.click_through cannot be changed live; restart the overlay to apply it"));
        }
        if (auto screen = requireScreen(application, candidate.monitor); !screen) {
            return screen;
        }
        if (auto options = themeCatalog.validateOptions(candidate.themeId, candidate.themeOptions); !options) {
            return std::unexpected(options.error());
        }
        if (previous && previous->themeId != candidate.themeId) {
            return theme.prepareSwitch(candidate);
        }
        return {};
    });
    if (const auto initialized = configuration.initialize(); !initialized) {
        writeError(initialized.error());
        return ExitUsage;
    }

    const OverlayConfig& initialConfig  = configuration.config();
    QScreen*             selectedScreen = resolveScreen(application, initialConfig.monitor);
    if (!selectedScreen) {
        writeError(unavailableScreenMessage(application, initialConfig.monitor));
        return ExitFailure;
    }
    LayerWindow* layerWindow = LayerWindow::get(&view);
    configureWindow(view, *layerWindow, initialConfig, selectedScreen);

    keyboardPresenter.setHistoryOptions(historyOptions(initialConfig));
    keyboardPresenter.setRepeatsEnabled(initialConfig.repeatsEnabled);
    keyboardPresenter.setExpiration(initialConfig.expireAfterMs, initialConfig.fadeDurationMs);
    if (const auto loaded = theme.loadInitial(initialConfig); !loaded) {
        writeError(loaded.error());
        return ExitFailure;
    }

    OverlayConfig appliedConfig = initialConfig;
    QObject::connect(&configuration, &OverlayConfigManager::configurationChanged, &view, [&] {
        const OverlayConfig& config = configuration.config();
        theme.applyAcceptedConfiguration(config);
        view.resize(config.width, config.height);
        layerWindow->setDesiredSize(QSize(config.width, config.height));
        layerWindow->setAnchors(toLayerAnchors(config.anchor));
        layerWindow->setMargins(config.margins);
        if (historyOptions(config) != historyOptions(appliedConfig)) {
            keyboardPresenter.setHistoryOptions(historyOptions(config));
        }
        if (config.repeatsEnabled != appliedConfig.repeatsEnabled) {
            keyboardPresenter.setRepeatsEnabled(config.repeatsEnabled);
        }
        if (config.expireAfterMs != appliedConfig.expireAfterMs || config.fadeDurationMs != appliedConfig.fadeDurationMs) {
            keyboardPresenter.setExpiration(config.expireAfterMs, config.fadeDurationMs);
        }
        appliedConfig = config;
    });
    QObject::connect(&application, &QGuiApplication::screenRemoved, &view, [&](QScreen* removedScreen) {
        if (removedScreen != selectedScreen) {
            return;
        }
        writeError(QStringLiteral("Selected output was removed; closing the overlay."));
        view.close();
        application.quit();
    });

    qInfo().noquote() << QStringLiteral("Overlay on %1 (%2x%3 logical px, scale %4), anchor=%5, theme=%6")
                             .arg(selectedScreen->name())
                             .arg(initialConfig.width)
                             .arg(initialConfig.height)
                             .arg(selectedScreen->devicePixelRatio())
                             .arg(toString(initialConfig.anchor))
                             .arg(theme.activeThemeId());

    IpcClient ipcClient(*socketPath);
    QObject::connect(
        &ipcClient, &IpcClient::protocolMessageReceived, &keyboardPresenter, [&](const ProtocolMessage& message) { keyboardPresenter.processMessage(message); },
        Qt::DirectConnection);
    QObject::connect(&ipcClient, &IpcClient::connectionReset, &keyboardPresenter, &KeyboardPresenter::resetConnection, Qt::DirectConnection);

    CaptureController capture(ipcClient);
    QObject::connect(&instance, &OverlayInstance::toggleRequested, &capture, &CaptureController::toggle);
    QObject::connect(&capture, &CaptureController::activeChanged, &view, [&](bool active) {
        if (!active) {
            keyboardPresenter.clearHistory();
        }
        view.setVisible(active);
    });
    // A failed first `toggle` launch exits instead of leaving an idle resident behind.
    bool initialActivation = *command == CommandLine::Command::Toggle;
    QObject::connect(&capture, &CaptureController::commandFailed, &application, [&](const QString& error) {
        writeError(error);
        if (initialActivation) {
            application.exit(ExitFailure);
        }
    });
    QObject::connect(&capture, &CaptureController::activeChanged, &application, [&](bool active) {
        if (active) {
            initialActivation = false;
        }
    });
    if (initialActivation) {
        capture.enableWhenReady();
    }

    const RestartContext startup{
        .socketPath     = *socketPath,
        .configPath     = configSource->path,
        .explicitConfig = configSource->explicitPath,
        .arguments      = commandLine.replacementArguments(*socketPath),
    };
    RestartCoordinator restartCoordinator(instance, capture, handoff, startup, QCoreApplication::applicationFilePath(),
                                          [&] { return preflightRestart(application, *configSource, *overrides); });
    if (bootstrap) {
        restartCoordinator.initializeReplacement(*bootstrap);
    }

    ipcClient.start();
    configuration.startWatching();
    if (*quitAfterMs > 0) {
        QTimer::singleShot(*quitAfterMs, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
