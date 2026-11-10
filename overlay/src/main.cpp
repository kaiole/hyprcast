#include "config/OverlayConfig.hpp"
#include "history/InputHistory.hpp"
#include "hyprcast/version.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/CaptureController.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/OverlayInstance.hpp"
#include "ipc/Protocol.hpp"
#include "theme/Theme.hpp"

#include <LayerShellQt/Window>

#include <QChar>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QList>
#include <QLocalSocket>
#include <QMargins>
#include <QObject>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickView>
#include <QScreen>
#include <QSize>
#include <QStandardPaths>
#include <QString>
#include <QStringView>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <array>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace {

using namespace hyprcast::overlay;
using LayerWindow = LayerShellQt::Window;

constexpr QStringView ThemeHostUrl = u"qrc:/hyprcast/overlay/qml/ThemeHost.qml";
constexpr QStringView BundledThemeManifest =
    u"qrc:/hyprcast/overlay/qml/themes/default/theme.toml";

// Exit codes: usage and configuration errors are distinguished from runtime
// failures.
constexpr int ExitFailure = 1;
constexpr int ExitUsage = 2;

void writeError(const QString& message) {
    QTextStream(stderr) << "hyprcast-overlay: " << message << Qt::endl;
}

void writeWarning(const QString& message) {
    qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(message);
}

std::expected<int, QString>
parseInteger(const QString& value, Range<int> range, const QString& option) {
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok || !range.contains(parsed)) {
        return std::unexpected(
            QStringLiteral("--%1 must be an integer from %2 to %3")
                .arg(option)
                .arg(range.minimum)
                .arg(range.maximum));
    }
    return parsed;
}

std::expected<QMargins, QString> parseMargins(const QString& value) {
    const auto error =
        std::unexpected(QStringLiteral("--margins must contain four integers "
                                       "from %1 to %2: left,top,right,bottom")
                            .arg(Limits::Margin.minimum)
                            .arg(Limits::Margin.maximum));
    const auto parts = value.split(QLatin1Char(','));
    if (parts.size() != 4) {
        return error;
    }
    std::array<int, 4> parsed {};
    for (std::size_t i = 0; i < parsed.size(); ++i) {
        bool ok = false;
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

QScreen* resolveScreen(const QString& requestedName) {
    if (requestedName.isEmpty()) {
        return QGuiApplication::primaryScreen();
    }
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == requestedName) {
            return screen;
        }
    }
    return nullptr;
}

QString unavailableScreenMessage(const QString& requestedName) {
    if (requestedName.isEmpty()) {
        return QStringLiteral("No display is available");
    }
    QStringList available;
    for (QScreen* screen : QGuiApplication::screens()) {
        available.push_back(screen->name());
    }
    return QStringLiteral("Unknown monitor '%1'; available outputs: %2")
        .arg(requestedName, available.join(QStringLiteral(", ")));
}

std::expected<void, QString> requireScreen(const QString& monitor) {
    if (!resolveScreen(monitor)) {
        return std::unexpected(unavailableScreenMessage(monitor));
    }
    return {};
}

InputHistoryOptions historyOptions(const OverlayConfig& config) {
    return {
        .backspaceMode = config.backspaceMode,
        .maxRetainedUtf16CodeUnits = config.maxRetainedUtf16CodeUnits,
        .presentation = {
            .countedRepeats =
                config.repeatPresentation == RepeatPresentation::Counted,
            .repeatThreshold = config.repeatCountThreshold,
            .symbolFontFamily = config.symbolFontFamily,
            .spaceSymbol = config.spaceSymbol,
            .keySymbols = config.keySymbols,
            .modifierSymbols = config.modifierSymbols,
        },
    };
}

QStringList themeRoots() {
    QStringList roots;
    for (const QString& dataLocation : QStandardPaths::standardLocations(
             QStandardPaths::GenericDataLocation)) {
        roots.push_back(
            QDir(dataLocation).filePath(QStringLiteral("hyprcast/themes")));
    }
    return roots;
}

// The command line, parsed once. Option objects live here so helpers need no
// long parameter lists.
class CommandLine {
public:
    using Command = OverlayInstance::Command;

    CommandLine() {
        m_parser.setApplicationDescription(
            QStringLiteral("A lightweight Hyprland keyboard overlay."));
        m_parser.addPositionalArgument(
            QStringLiteral("command"),
            QStringLiteral(
                "toggle: launch/enable or toggle capture; restart: rebuild "
                "this session's overlay window from the saved configuration "
                "and installed themes, keeping capture as it is (selectors "
                "only)."),
            QStringLiteral("[toggle|restart]"));
        m_parser.addOptions({
            m_config,
            m_monitor,
            m_anchor,
            m_margins,
            m_width,
            m_height,
            m_opacity,
            m_expireAfter,
            m_fadeDuration,
            m_backspace,
            m_retention,
            m_repeatEnabled,
            m_socket,
            m_instance,
            m_quitAfter,
        });
    }

    // Handles --help and --version itself (they exit), then validates the
    // command.
    [[nodiscard]] std::expected<Command, QString>
    parse(const QStringList& arguments) {
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
        Command command = Command::Start;
        if (commands == QStringList {QStringLiteral("toggle")}) {
            command = Command::Toggle;
        } else if (commands == QStringList {QStringLiteral("restart")}) {
            command = Command::Restart;
        } else if (!commands.isEmpty()) {
            return std::unexpected(
                QStringLiteral("Expected no command, 'toggle', or 'restart'"));
        }
        if (command == Command::Restart) {
            for (const auto& name : m_parser.optionNames()) {
                if (name != m_socket.names().front()
                    && name != m_instance.names().front()) {
                    return std::unexpected(
                        QStringLiteral("restart accepts only --socket and "
                                       "--instance-signature; the running "
                                       "overlay keeps its startup overrides"));
                }
            }
        }
        return command;
    }

    [[nodiscard]] std::expected<QString, QString> socketPath() const {
        if (m_parser.isSet(m_socket)) {
            const QString path = m_parser.value(m_socket);
            if (path.isEmpty()) {
                return std::unexpected(
                    QStringLiteral("--socket must not be empty"));
            }
            return QFileInfo(path).absoluteFilePath();
        }

        const QString runtimeDirectory =
            qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDirectory.isEmpty()
            || !QDir::isAbsolutePath(runtimeDirectory)) {
            return std::unexpected(
                QStringLiteral("XDG_RUNTIME_DIR must be set to an absolute "
                               "path (or pass --socket)"));
        }
        const QString signature = m_parser.isSet(m_instance)
            ? m_parser.value(m_instance)
            : qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
        if (signature.isEmpty() || signature == u"." || signature == u".."
            || signature.contains(QLatin1Char('/'))
            || signature.contains(QChar::Null)) {
            return std::unexpected(
                QStringLiteral("Set HYPRLAND_INSTANCE_SIGNATURE or pass "
                               "--instance-signature (or --socket)"));
        }
        return QDir(runtimeDirectory)
            .filePath(QStringLiteral("hyprcast/%1/events.sock").arg(signature));
    }

    // Reads the integer options that only differ in name and range.
    [[nodiscard]] std::expected<void, QString>
    readIntegerOverrides(ConfigOverrides& overrides) const {
        for (const auto& [option, target, range] : {
                 std::tuple {&m_width, &overrides.width, Limits::SurfaceSize},
                 std::tuple {&m_height, &overrides.height, Limits::SurfaceSize},
                 std::tuple {
                     &m_expireAfter,
                     &overrides.expireAfterMs,
                     Limits::ExpireAfterMs
                 },
                 std::tuple {
                     &m_fadeDuration,
                     &overrides.fadeDurationMs,
                     Limits::FadeDurationMs
                 },
             }) {
            if (!m_parser.isSet(*option)) {
                continue;
            }
            const auto value = parseInteger(m_parser.value(*option),
                                            range,
                                            option->names().front());
            if (!value) {
                return std::unexpected(value.error());
            }
            *target = *value;
        }
        return {};
    }

    [[nodiscard]] std::expected<ConfigOverrides, QString> overrides() const {
        ConfigOverrides overrides;
        if (m_parser.isSet(m_monitor)) {
            overrides.monitor = m_parser.value(m_monitor);
        }
        if (m_parser.isSet(m_anchor)) {
            const auto anchor = parseAnchor(m_parser.value(m_anchor));
            if (!anchor) {
                return std::unexpected(
                    QStringLiteral("--anchor: %1").arg(anchor.error()));
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
        if (auto integers = readIntegerOverrides(overrides); !integers) {
            return std::unexpected(integers.error());
        }
        if (m_parser.isSet(m_opacity)) {
            bool ok = false;
            const double opacity = m_parser.value(m_opacity).toDouble(&ok);
            if (!ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
                return std::unexpected(QStringLiteral(
                    "--background-opacity must be a number from 0 to 1"));
            }
            overrides.backgroundOpacity = opacity;
        }
        if (m_parser.isSet(m_backspace)) {
            const auto mode = parseBackspaceMode(m_parser.value(m_backspace));
            if (!mode) {
                return std::unexpected(QStringLiteral(
                    "--backspace-mode must be delete or symbol"));
            }
            overrides.backspaceMode = *mode;
        }
        if (m_parser.isSet(m_retention)) {
            constexpr Range<int> retention {
                .minimum =
                    static_cast<int>(Limits::RetainedUtf16CodeUnits.minimum),
                .maximum =
                    static_cast<int>(Limits::RetainedUtf16CodeUnits.maximum),
            };
            const auto units = parseInteger(m_parser.value(m_retention),
                                            retention,
                                            m_retention.names().front());
            if (!units) {
                return std::unexpected(units.error());
            }
            overrides.maxRetainedUtf16CodeUnits = *units;
        }
        if (m_parser.isSet(m_repeatEnabled)) {
            const QString value = m_parser.value(m_repeatEnabled).toLower();
            if (value != u"true" && value != u"false") {
                return std::unexpected(
                    QStringLiteral("--repeat-enabled must be true or false"));
            }
            overrides.repeatsEnabled = value == u"true";
        }
        return overrides;
    }

    // Explicit --config path, if given.
    [[nodiscard]] std::optional<QString> configPath() const {
        return m_parser.isSet(m_config)
            ? std::optional(m_parser.value(m_config))
            : std::nullopt;
    }

    [[nodiscard]] std::expected<int, QString> quitAfterMs() const {
        if (!m_parser.isSet(m_quitAfter)) {
            return 0;
        }
        return parseInteger(
            m_parser.value(m_quitAfter),
            {.minimum = 1, .maximum = std::numeric_limits<int>::max()},
            m_quitAfter.names().front());
    }

private:
    QCommandLineParser m_parser;
    const QCommandLineOption m_help = m_parser.addHelpOption();
    const QCommandLineOption m_version = m_parser.addVersionOption();
    const QCommandLineOption m_config = QCommandLineOption(
        QStringLiteral("config"),
        QStringLiteral("Load this TOML configuration file (must exist)."),
        QStringLiteral("path"));
    const QCommandLineOption m_monitor {
        {QStringLiteral("m"), QStringLiteral("monitor")},
        QStringLiteral("Output name (defaults to the primary output)."),
        QStringLiteral("name")
    };
    const QCommandLineOption m_anchor =
        QCommandLineOption(QStringLiteral("anchor"),
                           QStringLiteral("Position: top, bottom, left, right, "
                                          "or a corner such as bottom-right."),
                           QStringLiteral("position"));
    const QCommandLineOption m_margins = QCommandLineOption(
        QStringLiteral("margins"),
        QStringLiteral("Layer-shell margins in left,top,right,bottom order."),
        QStringLiteral("pixels"));
    const QCommandLineOption m_width = QCommandLineOption(
        QStringLiteral("width"),
        QStringLiteral("Maximum surface/panel width in logical pixels."),
        QStringLiteral("pixels"));
    const QCommandLineOption m_height = QCommandLineOption(
        QStringLiteral("height"),
        QStringLiteral("Maximum surface/panel height in logical pixels."),
        QStringLiteral("pixels"));
    const QCommandLineOption m_opacity = QCommandLineOption(
        QStringLiteral("background-opacity"),
        QStringLiteral("Panel fill opacity from 0 (transparent) to 1 (opaque); "
                       "borders are independent."),
        QStringLiteral("alpha"));
    const QCommandLineOption m_expireAfter =
        QCommandLineOption(QStringLiteral("expire-after-ms"),
                           QStringLiteral("Remove history after this much "
                                          "inactivity; 0 disables expiration."),
                           QStringLiteral("ms"));
    const QCommandLineOption m_fadeDuration = QCommandLineOption(
        QStringLiteral("fade-duration-ms"),
        QStringLiteral(
            "Fade expired history over this duration; 0 disables fading."),
        QStringLiteral("ms"));
    const QCommandLineOption m_backspace = QCommandLineOption(
        QStringLiteral("backspace-mode"),
        QStringLiteral("Plain Backspace behavior: delete or symbol."),
        QStringLiteral("mode"));
    const QCommandLineOption m_retention = QCommandLineOption(
        QStringLiteral("max-retained-utf16-code-units"),
        QStringLiteral(
            "History retention limit in formatted UTF-16 code units."),
        QStringLiteral("units"));
    const QCommandLineOption m_repeatEnabled = QCommandLineOption(
        QStringLiteral("repeat-enabled"),
        QStringLiteral("Enable locally generated key repeats (true or false)."),
        QStringLiteral("boolean"));
    const QCommandLineOption m_socket = QCommandLineOption(
        QStringLiteral("socket"),
        QStringLiteral("Connect to this Hyprcast event socket instead of "
                       "discovering it from the environment."),
        QStringLiteral("path"));
    const QCommandLineOption m_instance = QCommandLineOption(
        QStringLiteral("instance-signature"),
        QStringLiteral("Hyprland instance signature (defaults to "
                       "HYPRLAND_INSTANCE_SIGNATURE)."),
        QStringLiteral("signature"));
    const QCommandLineOption m_quitAfter = QCommandLineOption(
        QStringLiteral("quit-after-ms"),
        QStringLiteral(
            "Exit after this many milliseconds (useful for smoke tests)."),
        QStringLiteral("ms"));
};

// Settings that pick which configuration file the overlay reads.
struct ConfigSource {
    QString path;
    bool explicitPath = false;
};

std::expected<ConfigSource, QString>
resolveConfigSource(const CommandLine& commandLine) {
    if (const auto path = commandLine.configPath()) {
        if (path->isEmpty()) {
            return std::unexpected(
                QStringLiteral("--config path must not be empty"));
        }
        return ConfigSource {
            .path = QFileInfo(*path).absoluteFilePath(),
            .explicitPath = true,
        };
    }
    QString warning;
    const QString path = defaultConfigPath(&warning);
    if (!warning.isEmpty()) {
        writeWarning(warning);
    }
    return ConfigSource {
        .path = QFileInfo(path).absoluteFilePath(),
        .explicitPath = false,
    };
}

// Applies window settings that must be in place before the surface is first
// shown.
void configureWindow(QQuickView& view,
                     LayerWindow& layerWindow,
                     const OverlayConfig& config,
                     QScreen* screen) {
    view.setTitle(QStringLiteral("Hyprcast"));
    view.setScreen(screen);
    view.resize(config.width, config.height);
    view.setColor(Qt::transparent);
    QSurfaceFormat surfaceFormat = view.format();
    surfaceFormat.setAlphaBufferSize(8);
    view.setFormat(surfaceFormat);
    Qt::WindowFlags flags =
        view.flags() | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
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
    layerWindow.setKeyboardInteractivity(
        LayerWindow::KeyboardInteractivityNone);
    layerWindow.setActivateOnShow(false);
    layerWindow.setCloseOnDismissed(true);
}

std::expected<void, QString> loadThemeHost(QQuickView& view) {
    QObject::connect(
        view.engine(),
        &QQmlEngine::warnings,
        &view,
        [](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) {
                writeWarning(QStringLiteral("QML: %1").arg(warning.toString()));
            }
        });
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(ThemeHostUrl.toString()));
    if (view.status() == QQuickView::Error) {
        QStringList errors;
        for (const auto& error : view.errors()) {
            errors.push_back(error.toString());
        }
        return std::unexpected(errors.join(QLatin1Char('\n')));
    }
    return {};
}

// Whether this session can show `config`: its monitor is connected and the
// catalog knows its theme and accepts the theme's options.
std::expected<void, QString> checkAgainstSession(const OverlayConfig& config,
                                                 const ThemeCatalog& catalog) {
    return requireScreen(config.monitor).and_then([&] {
        return catalog.validateOptions(config.themeId, config.themeOptions)
            .transform([](const QVariantMap&) {});
    });
}

OverlayConfig withBundledTheme(OverlayConfig config) {
    config.themeId = BuiltinDefaultId.toString();
    config.themeOptions.clear();
    return config;
}

// What a restart replaces: the window, the QML engine it owns and the theme
// loaded into that engine. A new engine has compiled no QML yet, so it reads
// every theme file again.
struct Scene {
    Scene(const ThemeCatalog& catalog,
          KeyboardPresenter& presenter,
          const OverlayConfig& config,
          QScreen* onScreen)
        : theme(catalog, view, presenter),
          layer(LayerWindow::get(&view)),
          screen(onScreen),
          monitor(config.monitor),
          clickThrough(config.clickThrough) {
        configureWindow(view, *layer, config, screen);
    }

    [[nodiscard]] std::expected<void, QString>
    load(const OverlayConfig& config) {
        return hosted.and_then([&] { return theme.loadInitial(config); });
    }

    QQuickView view;
    // Before the theme runtime, which looks its host up in the view.
    std::expected<void, QString> hosted = loadThemeHost(view);
    ThemeRuntime theme;
    LayerWindow* layer = nullptr; // Owned by the view.
    QScreen* screen = nullptr;
    // The settings only a new window can change, as this one was built.
    QString monitor;
    bool clickThrough = false;
};

// This process as the resident overlay. Everything but the scene lives until
// the process exits; a restart replaces the scene alone, so the plugin
// connection, the capture state and the key history carry over.
class Resident {
public:
    Resident(const ConfigSource& source,
             const ConfigOverrides& overrides,
             const QString& socketPath)
        : m_configPath(source.path),
          m_configuration(source.path, source.explicitPath, overrides),
          m_ipc(socketPath) {}

    // Brings the overlay up. Returns the exit code if it cannot start.
    [[nodiscard]] std::optional<int> start(const QGuiApplication& application,
                                           OverlayInstance& instance,
                                           OverlayInstance::Command command) {
        const auto themeWarnings = m_catalog.discover();
        if (!themeWarnings) {
            writeError(themeWarnings.error());
            return ExitFailure;
        }
        for (const QString& warning : *themeWarnings) {
            writeWarning(warning);
        }

        m_configuration.setRuntimeValidator(
            [this](const std::optional<OverlayConfig>& previous,
                   const OverlayConfig& candidate) {
                return validate(previous, candidate);
            });
        if (const auto initialized = m_configuration.initialize();
            !initialized) {
            writeError(initialized.error());
            return ExitUsage;
        }
        const OverlayConfig& config = m_configuration.config();
        applyInputSettings(config);
        // Unlike a restart, startup has nothing to keep running, so a theme
        // that does not load is an error and not a reason to show another.
        m_scene = std::make_unique<Scene>(m_catalog,
                                          m_presenter,
                                          config,
                                          resolveScreen(config.monitor));
        if (const auto loaded = m_scene->load(config); !loaded) {
            writeError(loaded.error());
            return ExitFailure;
        }
        present();

        connectSignals(application, instance);
        m_initialActivation = command == OverlayInstance::Command::Toggle;
        if (m_initialActivation) {
            m_capture.enableWhenReady();
        }
        m_ipc.start();
        m_configuration.startWatching();
        return std::nullopt;
    }

private:
    void connectSignals(const QGuiApplication& application,
                        OverlayInstance& instance) {
        QObject::connect(&m_configuration,
                         &OverlayConfigManager::configurationChanged,
                         &m_receiver,
                         [this] { applyConfiguration(); });
        QObject::connect(
            &application,
            &QGuiApplication::screenRemoved,
            &m_receiver,
            [this](const QScreen* removedScreen) {
                if (removedScreen != m_scene->screen) {
                    return;
                }
                writeError(QStringLiteral(
                    "Selected output was removed; closing the overlay."));
                m_scene->view.close();
                QGuiApplication::quit();
            });

        QObject::connect(
            &m_ipc,
            &IpcClient::protocolMessageReceived,
            &m_presenter,
            [this](const ProtocolMessage& message) {
                m_presenter.processMessage(message);
            },
            Qt::DirectConnection);
        QObject::connect(&m_ipc,
                         &IpcClient::connectionReset,
                         &m_presenter,
                         &KeyboardPresenter::resetConnection,
                         Qt::DirectConnection);

        QObject::connect(&instance,
                         &OverlayInstance::toggleRequested,
                         &m_capture,
                         &CaptureController::toggle);
        QObject::connect(&instance,
                         &OverlayInstance::restartRequested,
                         &m_receiver,
                         [this](QLocalSocket* client) {
                             const auto restarted = restart();
                             if (!restarted) {
                                 writeError(restarted.error());
                             }
                             OverlayInstance::reply(
                                 client,
                                 restarted ? QString {} : restarted.error());
                         });
        QObject::connect(&m_capture,
                         &CaptureController::activeChanged,
                         &m_receiver,
                         [this](bool active) {
                             if (active) {
                                 m_initialActivation = false;
                             } else {
                                 m_presenter.clearHistory();
                             }
                             m_scene->view.setVisible(active);
                         });
        // A failed first `toggle` launch exits instead of leaving an idle
        // resident behind.
        QObject::connect(&m_capture,
                         &CaptureController::commandFailed,
                         &m_receiver,
                         [this](const QString& error) {
                             writeError(error);
                             if (m_initialActivation) {
                                 QCoreApplication::exit(ExitFailure);
                             }
                         });
    }

    void applyInputSettings(const OverlayConfig& config) {
        // Each setter ignores a value it already has.
        m_presenter.setHistoryOptions(historyOptions(config));
        m_presenter.setRepeatsEnabled(config.repeatsEnabled);
        m_presenter.setExpiration(config.expireAfterMs, config.fadeDurationMs);
    }

    // Decides whether a saved configuration can be applied to the running
    // overlay: `previous` is empty for the initial load.
    [[nodiscard]] std::expected<void, QString>
    validate(const std::optional<OverlayConfig>& previous,
             const OverlayConfig& candidate) {
        if (auto shown = checkAgainstSession(candidate, m_catalog); !shown) {
            return shown;
        }
        if (previous && previous->themeId != candidate.themeId) {
            return m_scene->theme.prepareSwitch(candidate);
        }
        return {};
    }

    // The configuration as the theme runtime sees it: while the bundled theme
    // stands in for one that failed to load, that theme's settings are shown
    // by the bundled theme.
    [[nodiscard]] OverlayConfig themed(const OverlayConfig& config) const {
        return !m_standInFor.isEmpty() && config.themeId == m_standInFor
            ? withBundledTheme(config)
            : config;
    }

    // Applies a configuration the watcher accepted to the running scene, or
    // to a new one if it changes what only a new window can take.
    void applyConfiguration() {
        const OverlayConfig& config = m_configuration.config();
        applyInputSettings(config);
        if (config.monitor != m_scene->monitor
            || config.clickThrough != m_scene->clickThrough) {
            if (const auto rebuilt = rebuildScene(); !rebuilt) {
                writeError(rebuilt.error());
            }
            return;
        }
        if (config.themeId != m_standInFor) {
            m_standInFor.clear();
        }
        m_scene->theme.applyAcceptedConfiguration(themed(config));
        m_scene->view.resize(config.width, config.height);
        m_scene->layer->setDesiredSize(QSize(config.width, config.height));
        m_scene->layer->setAnchors(toLayerAnchors(config.anchor));
        m_scene->layer->setMargins(config.margins);
    }

    // The `restart` command: reads the installed themes and the saved
    // configuration again and rebuilds the scene from them. Anything wrong
    // that can be found without running theme QML is reported with the
    // running overlay untouched.
    [[nodiscard]] std::expected<void, QString> restart() {
        ThemeCatalog catalog = newCatalog();
        const auto themeWarnings = catalog.discover();
        if (!themeWarnings) {
            return std::unexpected(themeWarnings.error());
        }
        auto candidate = m_configuration.loadCandidate();
        if (!candidate) {
            return std::unexpected(candidate.error());
        }
        if (const auto shown = checkAgainstSession(*candidate, catalog);
            !shown) {
            return std::unexpected(
                QStringLiteral("%1: %2").arg(m_configPath, shown.error()));
        }
        for (const QString& warning : *themeWarnings) {
            writeWarning(warning);
        }
        // The old scene goes before the catalog it refers to.
        m_scene.reset();
        m_catalog = std::move(catalog);
        m_configuration.adopt(std::move(*candidate));
        applyInputSettings(m_configuration.config());
        return rebuildScene();
    }

    // Replaces the scene with one built from the current configuration, which
    // has passed checkAgainstSession().
    [[nodiscard]] std::expected<void, QString> rebuildScene() {
        const OverlayConfig& config = m_configuration.config();
        // The old scene goes before the new one loads: images are cached for
        // the whole process by URL, and only the end of a window drops them.
        m_scene.reset();
        m_scene = std::make_unique<Scene>(m_catalog,
                                          m_presenter,
                                          config,
                                          resolveScreen(config.monitor));
        auto loaded = m_scene->load(config);
        m_standInFor.clear();
        if (!loaded) {
            // QML is code: whether a theme loads is only known by running it,
            // and by then the old scene is gone.
            if (const auto bundled = m_scene->load(withBundledTheme(config));
                !bundled) {
                writeError(bundled.error());
                QCoreApplication::exit(ExitFailure);
                return loaded;
            }
            m_standInFor = config.themeId;
            loaded = std::unexpected(
                QStringLiteral("The built-in theme is shown until 'hyprcast-"
                               "overlay restart' loads the configured one: %1")
                    .arg(loaded.error()));
        }
        present();
        return loaded;
    }

    // Shows the scene if capture is on and logs what was built.
    void present() {
        const OverlayConfig& config = m_configuration.config();
        m_scene->view.setVisible(m_capture.active());
        qInfo().noquote() << QStringLiteral(
                                 "Overlay on %1 (%2x%3 logical px, scale %4), "
                                 "anchor=%5, theme=%6")
                                 .arg(m_scene->screen->name())
                                 .arg(config.width)
                                 .arg(config.height)
                                 .arg(m_scene->screen->devicePixelRatio())
                                 .arg(toString(config.anchor))
                                 .arg(m_scene->theme.activeThemeId());
    }

    [[nodiscard]] static ThemeCatalog newCatalog() {
        return {themeRoots(), QUrl(BundledThemeManifest.toString())};
    }

    // Declaration order is lifetime order: each object outlives everything
    // declared after it, so nothing refers to an object that is already gone
    // during teardown.
    ThemeCatalog m_catalog = newCatalog();
    KeyboardPresenter m_presenter;
    QString m_configPath;
    OverlayConfigManager m_configuration;
    IpcClient m_ipc;
    CaptureController m_capture {m_ipc};
    std::unique_ptr<Scene> m_scene;
    // The configured theme that failed to load, while the bundled one is shown
    // in its place.
    QString m_standInFor;
    bool m_initialActivation = false;
    // Context of every connection that calls into this object: destroyed
    // first, it disconnects them before any member goes.
    QObject m_receiver;
};

// Runs this process as the resident overlay until it quits.
int runResident(const QGuiApplication& application,
                const CommandLine& commandLine,
                OverlayInstance::Command command,
                const QString& socketPath,
                OverlayInstance& instance) {
    const auto overrides = commandLine.overrides();
    if (!overrides) {
        writeError(overrides.error());
        return ExitUsage;
    }
    const auto configSource = resolveConfigSource(commandLine);
    if (!configSource) {
        writeError(configSource.error());
        return ExitUsage;
    }
    const auto quitAfterMs = commandLine.quitAfterMs();
    if (!quitAfterMs) {
        writeError(quitAfterMs.error());
        return ExitUsage;
    }

    Resident resident(*configSource, *overrides, socketPath);
    if (const auto failure = resident.start(application, instance, command)) {
        return *failure;
    }
    if (*quitAfterMs > 0) {
        QTimer::singleShot(*quitAfterMs, &application, &QCoreApplication::quit);
    }
    return QGuiApplication::exec();
}

} // namespace

int main(int argc, char* argv[]) {
    const QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprcast-overlay"));
    QCoreApplication::setApplicationVersion(
        QString::fromLatin1(hyprcast::project_version));

    CommandLine commandLine;
    const auto command = commandLine.parse(QGuiApplication::arguments());
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
    const auto ownership = instance.acquire(*command);
    if (!ownership) {
        writeError(ownership.error());
        return ExitFailure;
    }
    if (*ownership == OverlayInstance::Ownership::Forwarded) {
        return EXIT_SUCCESS;
    }

    // From here on this process is the resident overlay.
    return runResident(application,
                       commandLine,
                       *command,
                       *socketPath,
                       instance);
}
