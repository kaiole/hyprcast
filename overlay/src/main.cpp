#include "config/OverlayConfig.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/IpcClient.hpp"
#include "ipc/OverlayInstance.hpp"
#include "ipc/CaptureController.hpp"
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
#include <QQmlError>
#include <QQmlEngine>
#include <QQuickView>
#include <QScreen>
#include <QSize>
#include <QSurfaceFormat>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

namespace {
    using LayerWindow = LayerShellQt::Window;
    using Hyprcast::Overlay::ConfigOverrides;
    using Hyprcast::Overlay::OverlayConfig;
    using Hyprcast::Overlay::Range;
    namespace Limits = Hyprcast::Overlay::Limits;

    LayerWindow::Anchors toLayerAnchors(Hyprcast::Overlay::Anchor anchor) {
        using Anchor = Hyprcast::Overlay::Anchor;
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

    bool parseMargins(const QString& value, QMargins* margins) {
        const auto parts = value.split(QLatin1Char(','));
        if (parts.size() != 4) {
            return false;
        }
        int parsed[4] = {};
        for (qsizetype i = 0; i < parts.size(); ++i) {
            bool ok   = false;
            parsed[i] = parts[i].toInt(&ok);
            if (!ok || !Limits::Margin.contains(parsed[i])) {
                return false;
            }
        }
        *margins = QMargins(parsed[0], parsed[1], parsed[2], parsed[3]);
        return true;
    }

    bool parseInteger(const QString& value, Range<int> range, int* result) {
        bool      ok     = false;
        const int parsed = value.toInt(&ok);
        if (!ok || !range.contains(parsed)) {
            return false;
        }
        *result = parsed;
        return true;
    }

    void writeError(const QString& message) {
        QTextStream(stderr) << "hyprcast-overlay: " << message << Qt::endl;
    }

    bool resolveSocketPath(const QCommandLineParser& parser, const QCommandLineOption& socketOption, const QCommandLineOption& instanceOption, QString* socketPath) {
        if (parser.isSet(socketOption)) {
            *socketPath = parser.value(socketOption);
            if (socketPath->isEmpty()) {
                writeError(QStringLiteral("--socket must not be empty"));
                return false;
            }
            return true;
        }

        const QString runtimeDirectory = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDirectory.isEmpty() || !QDir::isAbsolutePath(runtimeDirectory)) {
            writeError(QStringLiteral("XDG_RUNTIME_DIR must be set to an absolute path (or pass --socket)"));
            return false;
        }

        const QString signature = parser.isSet(instanceOption) ? parser.value(instanceOption) : qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
        if (signature.isEmpty() || signature == QStringLiteral(".") || signature == QStringLiteral("..") || signature.contains(QLatin1Char('/')) ||
            signature.contains(QChar::Null)) {
            writeError(QStringLiteral("Set HYPRLAND_INSTANCE_SIGNATURE or pass --instance-signature (or --socket)"));
            return false;
        }
        *socketPath = QDir(runtimeDirectory).filePath(QStringLiteral("hyprcast/%1/events.sock").arg(signature));
        return true;
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

    bool parseOverrides(const QCommandLineParser& parser, const QCommandLineOption& monitorOption, const QCommandLineOption& anchorOption, const QCommandLineOption& marginsOption,
                        const QCommandLineOption& widthOption, const QCommandLineOption& heightOption, const QCommandLineOption& opacityOption,
                        const QCommandLineOption& expireAfterOption, const QCommandLineOption& fadeDurationOption, const QCommandLineOption& backspaceOption,
                        const QCommandLineOption& retentionOption, const QCommandLineOption& repeatEnabledOption, ConfigOverrides* overrides) {
        if (parser.isSet(monitorOption)) {
            overrides->monitor = parser.value(monitorOption);
        }
        if (parser.isSet(anchorOption)) {
            const auto anchor = Hyprcast::Overlay::parseAnchor(parser.value(anchorOption));
            if (!anchor) {
                writeError(QStringLiteral("--anchor: %1").arg(anchor.error()));
                return false;
            }
            overrides->anchor = *anchor;
        }
        if (parser.isSet(marginsOption)) {
            QMargins margins;
            if (!parseMargins(parser.value(marginsOption), &margins)) {
                writeError(QStringLiteral("--margins must contain four integers from %1 to %2: left,top,right,bottom").arg(Limits::Margin.minimum).arg(Limits::Margin.maximum));
                return false;
            }
            overrides->margins = margins;
        }
        if (parser.isSet(widthOption)) {
            int width = 0;
            if (!parseInteger(parser.value(widthOption), Limits::SurfaceSize, &width)) {
                writeError(QStringLiteral("--width must be an integer in the range %1..%2").arg(Limits::SurfaceSize.minimum).arg(Limits::SurfaceSize.maximum));
                return false;
            }
            overrides->width = width;
        }
        if (parser.isSet(heightOption)) {
            int height = 0;
            if (!parseInteger(parser.value(heightOption), Limits::SurfaceSize, &height)) {
                writeError(QStringLiteral("--height must be an integer in the range %1..%2").arg(Limits::SurfaceSize.minimum).arg(Limits::SurfaceSize.maximum));
                return false;
            }
            overrides->height = height;
        }
        if (parser.isSet(opacityOption)) {
            bool         ok      = false;
            const double opacity = parser.value(opacityOption).toDouble(&ok);
            if (!ok || !std::isfinite(opacity) || opacity < 0 || opacity > 1) {
                writeError(QStringLiteral("--background-opacity must be a number from 0 to 1"));
                return false;
            }
            overrides->backgroundOpacity = opacity;
        }
        if (parser.isSet(expireAfterOption)) {
            int duration = 0;
            if (!parseInteger(parser.value(expireAfterOption), Limits::ExpireAfterMs, &duration)) {
                writeError(QStringLiteral("--expire-after-ms must be an integer from %1 to %2").arg(Limits::ExpireAfterMs.minimum).arg(Limits::ExpireAfterMs.maximum));
                return false;
            }
            overrides->expireAfterMs = duration;
        }
        if (parser.isSet(fadeDurationOption)) {
            int duration = 0;
            if (!parseInteger(parser.value(fadeDurationOption), Limits::FadeDurationMs, &duration)) {
                writeError(QStringLiteral("--fade-duration-ms must be an integer from %1 to %2").arg(Limits::FadeDurationMs.minimum).arg(Limits::FadeDurationMs.maximum));
                return false;
            }
            overrides->fadeDurationMs = duration;
        }
        if (parser.isSet(backspaceOption)) {
            const auto mode = Hyprcast::Overlay::parseBackspaceMode(parser.value(backspaceOption));
            if (!mode) {
                writeError(QStringLiteral("--backspace-mode must be delete or symbol"));
                return false;
            }
            overrides->backspaceMode = *mode;
        }
        if (parser.isSet(retentionOption)) {
            int                  units = 0;
            constexpr Range<int> retention{static_cast<int>(Limits::RetainedUtf16CodeUnits.minimum), static_cast<int>(Limits::RetainedUtf16CodeUnits.maximum)};
            if (!parseInteger(parser.value(retentionOption), retention, &units)) {
                writeError(QStringLiteral("--max-retained-utf16-code-units must be an integer from %1 to %2").arg(retention.minimum).arg(retention.maximum));
                return false;
            }
            overrides->maxRetainedUtf16CodeUnits = units;
        }
        if (parser.isSet(repeatEnabledOption)) {
            const QString value = parser.value(repeatEnabledOption).toLower();
            if (value != QStringLiteral("true") && value != QStringLiteral("false")) {
                writeError(QStringLiteral("--repeat-enabled must be true or false"));
                return false;
            }
            overrides->repeatsEnabled = value == QStringLiteral("true");
        }
        return true;
    }
} // namespace

int main(int argc, char* argv[]) {
    Hyprcast::Overlay::RestartHandoff handoff;
    if (const auto adopted = handoff.adopt(); !adopted) {
        writeError(adopted.error());
        return 1;
    }
    // A plain copy of the bootstrap data; the descriptors stay owned by `handoff`.
    const std::optional<Hyprcast::Overlay::RestartContext> bootstrap = handoff.context();
    QGuiApplication                                        application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprcast-overlay"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A lightweight Hyprland keyboard overlay."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"),
                                 QStringLiteral("toggle: launch/enable or toggle capture; restart: replace this session's overlay and restore capture (selectors only)."),
                                 QStringLiteral("[toggle|restart]"));

    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Load this TOML configuration file (must exist)."), QStringLiteral("path"));
    const QCommandLineOption monitorOption({QStringLiteral("m"), QStringLiteral("monitor")}, QStringLiteral("Output name (defaults to the primary output)."),
                                           QStringLiteral("name"));
    const QCommandLineOption anchorOption(QStringLiteral("anchor"), QStringLiteral("Position: top, bottom, left, right, or a corner such as bottom-right."),
                                          QStringLiteral("position"));
    const QCommandLineOption marginsOption(QStringLiteral("margins"), QStringLiteral("Layer-shell margins in left,top,right,bottom order."), QStringLiteral("pixels"));
    const QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Maximum surface/panel width in logical pixels."), QStringLiteral("pixels"));
    const QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Maximum surface/panel height in logical pixels."), QStringLiteral("pixels"));
    const QCommandLineOption opacityOption(QStringLiteral("background-opacity"), QStringLiteral("Panel fill opacity from 0 (transparent) to 1 (opaque); borders are independent."),
                                           QStringLiteral("alpha"));
    const QCommandLineOption expireAfterOption(QStringLiteral("expire-after-ms"), QStringLiteral("Remove history after this much inactivity; 0 disables expiration."),
                                               QStringLiteral("ms"));
    const QCommandLineOption fadeDurationOption(QStringLiteral("fade-duration-ms"), QStringLiteral("Fade expired history over this duration; 0 disables fading."),
                                                QStringLiteral("ms"));
    const QCommandLineOption backspaceOption(QStringLiteral("backspace-mode"), QStringLiteral("Plain Backspace behavior: delete or symbol."), QStringLiteral("mode"));
    const QCommandLineOption retentionOption(QStringLiteral("max-retained-utf16-code-units"), QStringLiteral("History retention limit in formatted UTF-16 code units."),
                                             QStringLiteral("units"));
    const QCommandLineOption repeatEnabledOption(QStringLiteral("repeat-enabled"), QStringLiteral("Enable locally generated key repeats (true or false)."),
                                                 QStringLiteral("boolean"));
    const QCommandLineOption socketOption(QStringLiteral("socket"), QStringLiteral("Connect to this Hyprcast event socket instead of discovering it from the environment."),
                                          QStringLiteral("path"));
    const QCommandLineOption instanceOption(QStringLiteral("instance-signature"), QStringLiteral("Hyprland instance signature (defaults to HYPRLAND_INSTANCE_SIGNATURE)."),
                                            QStringLiteral("signature"));
    const QCommandLineOption quitAfterOption(QStringLiteral("quit-after-ms"), QStringLiteral("Exit after this many milliseconds (useful for smoke tests)."), QStringLiteral("ms"));
    parser.addOptions({configOption, monitorOption, anchorOption, marginsOption, widthOption, heightOption, opacityOption, expireAfterOption, fadeDurationOption, backspaceOption,
                       retentionOption, repeatEnabledOption, socketOption, instanceOption, quitAfterOption});
    if (!parser.parse(application.arguments())) {
        writeError(parser.errorText());
        return 2;
    }
    parser.process(application);
    const QStringList commands = parser.positionalArguments();
    if (commands.size() > 1 || (!commands.isEmpty() && commands.front() != QStringLiteral("toggle") && commands.front() != QStringLiteral("restart"))) {
        writeError(QStringLiteral("Expected no command, 'toggle', or 'restart'"));
        return 2;
    }
    const bool restart = commands == QStringList{QStringLiteral("restart")};
    const bool toggle  = commands == QStringList{QStringLiteral("toggle")};
    if (restart) {
        for (const auto& name : parser.optionNames()) {
            if (name != QStringLiteral("socket") && name != QStringLiteral("instance-signature")) {
                writeError(QStringLiteral("restart accepts only --socket and --instance-signature; resident startup overrides are preserved"));
                return 2;
            }
        }
    }
    QString socketPath;
    if (!resolveSocketPath(parser, socketOption, instanceOption, &socketPath)) {
        return 2;
    }
    socketPath = QFileInfo(socketPath).absoluteFilePath();
    Hyprcast::Overlay::OverlayInstance instance(socketPath);
    using Instance       = Hyprcast::Overlay::OverlayInstance;
    const auto ownership = instance.acquire(restart ? Instance::Command::Restart : toggle ? Instance::Command::Toggle : Instance::Command::Start, handoff.takeLock());
    if (!ownership) {
        writeError(ownership.error());
        return 1;
    }
    if (*ownership == Instance::Ownership::Forwarded) {
        return 0;
    }

    ConfigOverrides overrides;
    if (!parseOverrides(parser, monitorOption, anchorOption, marginsOption, widthOption, heightOption, opacityOption, expireAfterOption, fadeDurationOption, backspaceOption,
                        retentionOption, repeatEnabledOption, &overrides)) {
        return 2;
    }

    const bool explicitConfigPath = bootstrap ? bootstrap->explicitConfig : parser.isSet(configOption);
    QString    configPath;
    if (bootstrap) {
        configPath = bootstrap->configPath;
    } else if (explicitConfigPath) {
        configPath = parser.value(configOption);
        if (configPath.isEmpty()) {
            writeError(QStringLiteral("--config path must not be empty"));
            return 2;
        }
    } else {
        QString warning;
        configPath = Hyprcast::Overlay::defaultConfigPath(&warning);
        if (!warning.isEmpty()) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(warning);
        }
    }

    configPath                             = QFileInfo(configPath).absoluteFilePath();
    const ConfigOverrides startupOverrides = overrides;
    QStringList           replacementArguments{QStringLiteral("--socket"), socketPath};
    for (const QString& name : parser.optionNames()) {
        if (name == QStringLiteral("socket") || name == QStringLiteral("instance-signature") || name == QStringLiteral("config") || name == QStringLiteral("quit-after-ms")) {
            continue;
        }
        replacementArguments << QStringLiteral("--") + name << parser.value(name);
    }
    const QString replacementExecutable = QCoreApplication::applicationFilePath();

    QStringList   themeRoots;
    for (const QString& dataLocation : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        themeRoots.push_back(QDir(dataLocation).filePath(QStringLiteral("hyprcast/themes")));
    }
    Hyprcast::Overlay::ThemeCatalog themeCatalog(themeRoots, QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/themes/default/theme.toml")));
    const auto                      themeWarnings = themeCatalog.discover();
    if (!themeWarnings) {
        writeError(themeWarnings.error());
        return 1;
    }
    for (const QString& warning : *themeWarnings) {
        qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(warning);
    }

    Hyprcast::Overlay::ThemeRuntime*        themeRuntime = nullptr;
    Hyprcast::Overlay::OverlayConfigManager configuration(configPath, explicitConfigPath, std::move(overrides));
    configuration.setRuntimeValidator(
        [&application, &themeCatalog, &themeRuntime](const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate) -> std::expected<void, QString> {
            if (previous && previous->monitor != candidate.monitor) {
                return std::unexpected(QStringLiteral("window.monitor cannot be changed live; restart the overlay to apply it"));
            }
            if (previous && previous->clickThrough != candidate.clickThrough) {
                return std::unexpected(QStringLiteral("window.click_through cannot be changed live; restart the overlay to apply it"));
            }
            if (!resolveScreen(application, candidate.monitor)) {
                return std::unexpected(unavailableScreenMessage(application, candidate.monitor));
            }
            if (auto options = themeCatalog.validateOptions(candidate.themeId, candidate.themeOptions); !options) {
                return std::unexpected(options.error());
            }
            if (previous && previous->themeId != candidate.themeId && themeRuntime) {
                return themeRuntime->prepareSwitch(candidate);
            }
            return {};
        });
    if (const auto initialized = configuration.initialize(); !initialized) {
        writeError(initialized.error());
        return 2;
    }

    int quitAfterMs = 0;
    if (parser.isSet(quitAfterOption) && !parseInteger(parser.value(quitAfterOption), {1, std::numeric_limits<int>::max()}, &quitAfterMs)) {
        writeError(QStringLiteral("--quit-after-ms must be a positive integer"));
        return 2;
    }

    const OverlayConfig& initialConfig  = configuration.config();
    QScreen*             selectedScreen = resolveScreen(application, initialConfig.monitor);
    if (!selectedScreen) {
        writeError(unavailableScreenMessage(application, initialConfig.monitor));
        return 1;
    }

    Hyprcast::Overlay::KeyboardPresenter keyboardPresenter;
    auto                                 makeHistoryOptions = [](const OverlayConfig& config) {
        return Hyprcast::Overlay::InputHistoryOptions{
            .backspaceMode             = config.backspaceMode,
            .maxRetainedUtf16CodeUnits = config.maxRetainedUtf16CodeUnits,
            .presentation =
                {
                    .countedRepeats   = config.repeatPresentation == Hyprcast::Overlay::RepeatPresentation::Counted,
                    .repeatThreshold  = config.repeatCountThreshold,
                    .symbolFontFamily = config.symbolFontFamily,
                    .spaceSymbol      = config.spaceSymbol,
                    .keySymbols       = config.keySymbols,
                    .modifierSymbols  = config.modifierSymbols,
                },
        };
    };
    keyboardPresenter.setHistoryOptions(makeHistoryOptions(initialConfig));
    keyboardPresenter.setRepeatsEnabled(initialConfig.repeatsEnabled);
    keyboardPresenter.setExpiration(initialConfig.expireAfterMs, initialConfig.fadeDurationMs);

    Hyprcast::Overlay::IpcClient ipcClient(socketPath);
    QObject::connect(
        &ipcClient, &Hyprcast::Overlay::IpcClient::protocolMessageReceived, &keyboardPresenter,
        [&keyboardPresenter](const Hyprcast::Overlay::ProtocolMessage& message) { keyboardPresenter.processMessage(message); }, Qt::DirectConnection);
    QObject::connect(
        &ipcClient, &Hyprcast::Overlay::IpcClient::connectionReset, &keyboardPresenter, [&keyboardPresenter] { keyboardPresenter.resetConnection(); }, Qt::DirectConnection);

    QQuickView view;
    view.setTitle(QStringLiteral("Hyprcast"));
    view.setScreen(selectedScreen);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(initialConfig.width, initialConfig.height);
    view.setColor(Qt::transparent);

    QSurfaceFormat surfaceFormat = view.format();
    surfaceFormat.setAlphaBufferSize(8);
    view.setFormat(surfaceFormat);
    Qt::WindowFlags flags = view.flags() | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus;
    if (initialConfig.clickThrough) {
        flags |= Qt::WindowTransparentForInput;
    }
    view.setFlags(flags);

    QObject::connect(view.engine(), &QQmlEngine::warnings, &view, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) {
            qWarning().noquote() << QStringLiteral("hyprcast-overlay: QML: %1").arg(warning.toString());
        }
    });
    view.setSource(QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/ThemeHost.qml")));
    if (view.status() == QQuickView::Error) {
        for (const auto& error : view.errors()) {
            writeError(error.toString());
        }
        return 1;
    }

    LayerWindow* layerWindow = LayerWindow::get(&view);
    layerWindow->setScope(QStringLiteral("hyprcast-overlay"));
    layerWindow->setLayer(LayerWindow::LayerOverlay);
    layerWindow->setAnchors(toLayerAnchors(initialConfig.anchor));
    layerWindow->setMargins(initialConfig.margins);
    layerWindow->setDesiredSize(QSize(initialConfig.width, initialConfig.height));
    layerWindow->setScreen(selectedScreen);
    layerWindow->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    layerWindow->setActivateOnShow(false);
    layerWindow->setCloseOnDismissed(true);

    Hyprcast::Overlay::ThemeRuntime loadedTheme(themeCatalog, view, keyboardPresenter);
    if (const auto loaded = loadedTheme.loadInitial(initialConfig); !loaded) {
        writeError(loaded.error());
        return 1;
    }
    themeRuntime = &loadedTheme;

    OverlayConfig appliedConfig = initialConfig;
    QObject::connect(&configuration, &Hyprcast::Overlay::OverlayConfigManager::configurationChanged, &view,
                     [&configuration, &keyboardPresenter, &view, &loadedTheme, layerWindow, &appliedConfig, &makeHistoryOptions] {
                         const OverlayConfig& config = configuration.config();
                         loadedTheme.applyAcceptedConfiguration(config);
                         view.resize(config.width, config.height);
                         layerWindow->setDesiredSize(QSize(config.width, config.height));
                         layerWindow->setAnchors(toLayerAnchors(config.anchor));
                         layerWindow->setMargins(config.margins);

                         if (config.backspaceMode != appliedConfig.backspaceMode || config.maxRetainedUtf16CodeUnits != appliedConfig.maxRetainedUtf16CodeUnits ||
                             config.repeatPresentation != appliedConfig.repeatPresentation || config.repeatCountThreshold != appliedConfig.repeatCountThreshold ||
                             config.symbolFontFamily != appliedConfig.symbolFontFamily || config.spaceSymbol != appliedConfig.spaceSymbol ||
                             config.keySymbols != appliedConfig.keySymbols || config.modifierSymbols != appliedConfig.modifierSymbols) {
                             keyboardPresenter.setHistoryOptions(makeHistoryOptions(config));
                         }
                         if (config.repeatsEnabled != appliedConfig.repeatsEnabled) {
                             keyboardPresenter.setRepeatsEnabled(config.repeatsEnabled);
                         }
                         if (config.expireAfterMs != appliedConfig.expireAfterMs || config.fadeDurationMs != appliedConfig.fadeDurationMs) {
                             keyboardPresenter.setExpiration(config.expireAfterMs, config.fadeDurationMs);
                         }
                         appliedConfig = config;
                     });

    QObject::connect(&application, &QGuiApplication::screenRemoved, &view, [&application, &view, &selectedScreen](QScreen* removedScreen) {
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
                             .arg(Hyprcast::Overlay::toString(initialConfig.anchor))
                             .arg(loadedTheme.activeThemeId());

    Hyprcast::Overlay::CaptureController capture(ipcClient);
    QObject::connect(&instance, &Hyprcast::Overlay::OverlayInstance::toggleRequested, &capture, &Hyprcast::Overlay::CaptureController::toggle);
    QObject::connect(&capture, &Hyprcast::Overlay::CaptureController::activeChanged, &view, [&view, &keyboardPresenter](bool active) {
        if (!active) {
            keyboardPresenter.clearHistory();
        }
        view.setVisible(active);
    });
    bool initialActivation = toggle;
    QObject::connect(&capture, &Hyprcast::Overlay::CaptureController::commandFailed, &application, [&application, &initialActivation](const QString& error) {
        writeError(error);
        if (initialActivation) {
            application.exit(1);
        }
    });
    QObject::connect(&capture, &Hyprcast::Overlay::CaptureController::activeChanged, &application, [&initialActivation](bool active) {
        if (active) {
            initialActivation = false;
        }
    });
    if (toggle) {
        capture.enableWhenReady();
    }
    Hyprcast::Overlay::RestartContext startup;
    startup.socketPath     = socketPath;
    startup.configPath     = configPath;
    startup.explicitConfig = explicitConfigPath;
    startup.arguments      = replacementArguments;
    auto preflight         = [&]() -> std::expected<void, QString> {
        Hyprcast::Overlay::ThemeCatalog catalog(themeRoots, QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/themes/default/theme.toml")));
        if (auto discovered = catalog.discover(); !discovered) {
            return std::unexpected(discovered.error());
        }
        Hyprcast::Overlay::OverlayConfigManager candidate(configPath, explicitConfigPath, startupOverrides);
        candidate.setRuntimeValidator([&](const std::optional<OverlayConfig>&, const OverlayConfig& config) -> std::expected<void, QString> {
            if (!resolveScreen(application, config.monitor)) {
                return std::unexpected(unavailableScreenMessage(application, config.monitor));
            }
            return catalog.validateOptions(config.themeId, config.themeOptions).transform([](const QVariantMap&) {});
        });
        if (auto initialized = candidate.initialize(); !initialized) {
            return initialized;
        }
        // Trusted QML is instantiated in an isolated engine, never the active cache.
        Hyprcast::Overlay::KeyboardPresenter presenter;
        QQuickView                           probe;
        probe.setScreen(resolveScreen(application, candidate.config().monitor));
        probe.setResizeMode(QQuickView::SizeRootObjectToView);
        probe.resize(candidate.config().width, candidate.config().height);
        probe.setSource(QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/ThemeHost.qml")));
        if (probe.status() == QQuickView::Error) {
            return std::unexpected(QStringLiteral("Cannot initialize isolated theme preflight host"));
        }
        Hyprcast::Overlay::ThemeRuntime runtime(catalog, probe, presenter);
        return runtime.loadInitial(candidate.config());
    };
    Hyprcast::Overlay::RestartCoordinator restartCoordinator(instance, capture, handoff, std::move(startup), replacementExecutable, preflight);
    if (bootstrap) {
        restartCoordinator.initializeReplacement(*bootstrap);
    }
    ipcClient.start();
    configuration.startWatching();
    if (quitAfterMs > 0) {
        QTimer::singleShot(quitAfterMs, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
