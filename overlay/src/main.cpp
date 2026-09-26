#include "config/OverlayConfig.hpp"
#include "input/KeyboardPresenter.hpp"
#include "ipc/IpcClient.hpp"
#include "theme/Theme.hpp"

#include <LayerShellQt/Window>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
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
#include <limits>
#include <utility>

namespace {
    using LayerWindow = LayerShellQt::Window;
    using Hyprcast::Overlay::ConfigOverrides;
    using Hyprcast::Overlay::OverlayConfig;

    bool parseAnchor(const QString& value, LayerWindow::Anchors* anchors) {
        const auto parts = value.toLower().split(QLatin1Char('-'), Qt::SkipEmptyParts);
        if (parts.isEmpty() || parts.size() > 2) {
            return false;
        }

        *anchors           = {};
        bool hasHorizontal = false;
        bool hasVertical   = false;
        for (const auto& part : parts) {
            if (part == QStringLiteral("top")) {
                if (hasVertical) {
                    return false;
                }
                *anchors |= LayerWindow::AnchorTop;
                hasVertical = true;
            } else if (part == QStringLiteral("bottom")) {
                if (hasVertical) {
                    return false;
                }
                *anchors |= LayerWindow::AnchorBottom;
                hasVertical = true;
            } else if (part == QStringLiteral("left")) {
                if (hasHorizontal) {
                    return false;
                }
                *anchors |= LayerWindow::AnchorLeft;
                hasHorizontal = true;
            } else if (part == QStringLiteral("right")) {
                if (hasHorizontal) {
                    return false;
                }
                *anchors |= LayerWindow::AnchorRight;
                hasHorizontal = true;
            } else {
                return false;
            }
        }
        return true;
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
            if (!ok || parsed[i] < 0 || parsed[i] > 8192) {
                return false;
            }
        }
        *margins = QMargins(parsed[0], parsed[1], parsed[2], parsed[3]);
        return true;
    }

    bool parseInteger(const QString& value, int minimum, int maximum, int* result) {
        bool      ok     = false;
        const int parsed = value.toInt(&ok);
        if (!ok || parsed < minimum || parsed > maximum) {
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
                        const QCommandLineOption& presentationOption, const QCommandLineOption& showHeldKeysOption, const QCommandLineOption& hideHeldKeysOption,
                        const QCommandLineOption& expireAfterOption, const QCommandLineOption& fadeDurationOption, const QCommandLineOption& backspaceOption,
                        const QCommandLineOption& retentionOption, const QCommandLineOption& repeatEnabledOption, ConfigOverrides* overrides) {
        if (parser.isSet(monitorOption))
            overrides->monitor = parser.value(monitorOption);
        if (parser.isSet(anchorOption))
            overrides->anchor = parser.value(anchorOption);
        if (parser.isSet(marginsOption)) {
            QMargins margins;
            if (!parseMargins(parser.value(marginsOption), &margins)) {
                writeError(QStringLiteral("--margins must contain four integers from 0 to 8192: left,top,right,bottom"));
                return false;
            }
            overrides->margins = margins;
        }
        if (parser.isSet(widthOption)) {
            int width = 0;
            if (!parseInteger(parser.value(widthOption), 1, 8192, &width)) {
                writeError(QStringLiteral("--width must be an integer in the range 1..8192"));
                return false;
            }
            overrides->width = width;
        }
        if (parser.isSet(heightOption)) {
            int height = 0;
            if (!parseInteger(parser.value(heightOption), 1, 8192, &height)) {
                writeError(QStringLiteral("--height must be an integer in the range 1..8192"));
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
        if (parser.isSet(presentationOption))
            overrides->presentation = parser.value(presentationOption);
        if (parser.isSet(showHeldKeysOption) && parser.isSet(hideHeldKeysOption)) {
            writeError(QStringLiteral("--show-held-keys and --hide-held-keys cannot be used together"));
            return false;
        }
        if (parser.isSet(showHeldKeysOption))
            overrides->showHeldKeys = true;
        if (parser.isSet(hideHeldKeysOption))
            overrides->showHeldKeys = false;
        if (parser.isSet(expireAfterOption)) {
            int duration = 0;
            if (!parseInteger(parser.value(expireAfterOption), 0, 86'400'000, &duration)) {
                writeError(QStringLiteral("--expire-after-ms must be an integer from 0 to 86400000"));
                return false;
            }
            overrides->expireAfterMs = duration;
        }
        if (parser.isSet(fadeDurationOption)) {
            int duration = 0;
            if (!parseInteger(parser.value(fadeDurationOption), 0, 60'000, &duration)) {
                writeError(QStringLiteral("--fade-duration-ms must be an integer from 0 to 60000"));
                return false;
            }
            overrides->fadeDurationMs = duration;
        }
        if (parser.isSet(backspaceOption))
            overrides->backspaceMode = parser.value(backspaceOption);
        if (parser.isSet(retentionOption)) {
            int units = 0;
            if (!parseInteger(parser.value(retentionOption), 1, 1'048'576, &units)) {
                writeError(QStringLiteral("--max-retained-utf16-code-units must be an integer from 1 to 1048576"));
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
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprcast-overlay"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("A lightweight Hyprland keyboard overlay."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("Load this TOML configuration file (must exist)."), QStringLiteral("path"));
    const QCommandLineOption monitorOption({QStringLiteral("m"), QStringLiteral("monitor")}, QStringLiteral("Output name (defaults to the primary output)."),
                                           QStringLiteral("name"));
    const QCommandLineOption anchorOption(QStringLiteral("anchor"), QStringLiteral("Position: top, bottom, left, right, or a corner such as bottom-right."),
                                          QStringLiteral("position"));
    const QCommandLineOption marginsOption(QStringLiteral("margins"), QStringLiteral("Layer-shell margins in left,top,right,bottom order."), QStringLiteral("pixels"));
    const QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Surface width in logical pixels."), QStringLiteral("pixels"));
    const QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Surface height in logical pixels."), QStringLiteral("pixels"));
    const QCommandLineOption opacityOption(QStringLiteral("background-opacity"), QStringLiteral("Background alpha from 0 (transparent) to 1 (opaque); text stays opaque."),
                                           QStringLiteral("alpha"));
    const QCommandLineOption presentationOption(QStringLiteral("presentation"), QStringLiteral("Bundled presentation: text or keycaps."), QStringLiteral("mode"));
    const QCommandLineOption showHeldKeysOption(QStringLiteral("show-held-keys"), QStringLiteral("Show currently held keys below the history."));
    const QCommandLineOption hideHeldKeysOption(QStringLiteral("hide-held-keys"), QStringLiteral("Hide held-key feedback, overriding the TOML setting."));
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
    parser.addOptions({configOption, monitorOption, anchorOption, marginsOption, widthOption, heightOption, opacityOption, presentationOption, showHeldKeysOption,
                       hideHeldKeysOption, expireAfterOption, fadeDurationOption, backspaceOption, retentionOption, repeatEnabledOption, socketOption, instanceOption,
                       quitAfterOption});
    parser.process(application);

    ConfigOverrides overrides;
    if (!parseOverrides(parser, monitorOption, anchorOption, marginsOption, widthOption, heightOption, opacityOption, presentationOption, showHeldKeysOption, hideHeldKeysOption,
                        expireAfterOption, fadeDurationOption, backspaceOption, retentionOption, repeatEnabledOption, &overrides)) {
        return 2;
    }

    const bool explicitConfigPath = parser.isSet(configOption);
    QString    configPath;
    if (explicitConfigPath) {
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

    QString socketPath;
    if (!resolveSocketPath(parser, socketOption, instanceOption, &socketPath)) {
        return 2;
    }

    QStringList themeRoots;
    for (const QString& dataLocation : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation)) {
        themeRoots.push_back(QDir(dataLocation).filePath(QStringLiteral("hyprcast/themes")));
    }
    Hyprcast::Overlay::ThemeCatalog themeCatalog(themeRoots, QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/themes/default/theme.toml")));
    QStringList                     themeWarnings;
    QString                         themeDiscoveryError;
    if (!themeCatalog.discover(&themeWarnings, &themeDiscoveryError)) {
        writeError(themeDiscoveryError);
        return 1;
    }
    for (const QString& warning : themeWarnings) {
        qWarning().noquote() << QStringLiteral("hyprcast-overlay: %1").arg(warning);
    }

    Hyprcast::Overlay::ThemeRuntime*        themeRuntime = nullptr;
    Hyprcast::Overlay::OverlayConfigManager configuration(configPath, explicitConfigPath, std::move(overrides));
    configuration.setRuntimeValidator([&application, &themeCatalog, &themeRuntime](const std::optional<OverlayConfig>& previous, const OverlayConfig& candidate, QString* error) {
        if (previous && previous->monitor != candidate.monitor) {
            if (error) {
                *error = QStringLiteral("window.monitor cannot be changed live; restart the overlay to apply it");
            }
            return false;
        }
        if (previous && previous->clickThrough != candidate.clickThrough) {
            if (error) {
                *error = QStringLiteral("window.click_through cannot be changed live; restart the overlay to apply it");
            }
            return false;
        }
        if (!resolveScreen(application, candidate.monitor)) {
            if (error) {
                *error = unavailableScreenMessage(application, candidate.monitor);
            }
            return false;
        }
        QVariantMap effectiveOptions;
        if (!themeCatalog.validateOptions(candidate.themeId, candidate.themeOptions, &effectiveOptions, error)) {
            return false;
        }
        if (previous && previous->themeId != candidate.themeId && themeRuntime && !themeRuntime->prepareSwitch(candidate, error)) {
            return false;
        }
        return true;
    });
    QString configError;
    if (!configuration.initialize(&configError)) {
        writeError(configError);
        return 2;
    }

    int quitAfterMs = 0;
    if (parser.isSet(quitAfterOption) && !parseInteger(parser.value(quitAfterOption), 1, std::numeric_limits<int>::max(), &quitAfterMs)) {
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
            .backspaceMode             = config.backspaceMode == QStringLiteral("symbol") ? Hyprcast::Overlay::BackspaceMode::Symbol : Hyprcast::Overlay::BackspaceMode::Delete,
            .maxRetainedUtf16CodeUnits = config.maxRetainedUtf16CodeUnits,
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
    LayerWindow::Anchors anchors;
    if (!parseAnchor(initialConfig.anchor, &anchors)) {
        writeError(QStringLiteral("invalid configured anchor '%1'").arg(initialConfig.anchor));
        return 2;
    }
    layerWindow->setAnchors(anchors);
    layerWindow->setMargins(initialConfig.margins);
    layerWindow->setDesiredSize(QSize(initialConfig.width, initialConfig.height));
    layerWindow->setScreen(selectedScreen);
    layerWindow->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    layerWindow->setActivateOnShow(false);
    layerWindow->setCloseOnDismissed(true);

    Hyprcast::Overlay::ThemeRuntime loadedTheme(themeCatalog, view, keyboardPresenter);
    QString                         themeLoadError;
    if (!loadedTheme.loadInitial(initialConfig, &themeLoadError)) {
        writeError(themeLoadError);
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
                         LayerWindow::Anchors anchors;
                         parseAnchor(config.anchor, &anchors); // The candidate was validated before it was accepted.
                         layerWindow->setAnchors(anchors);
                         layerWindow->setMargins(config.margins);

                         if (config.backspaceMode != appliedConfig.backspaceMode || config.maxRetainedUtf16CodeUnits != appliedConfig.maxRetainedUtf16CodeUnits) {
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
                             .arg(initialConfig.anchor)
                             .arg(loadedTheme.activeThemeId());

    ipcClient.start();
    view.show();
    configuration.startWatching();
    if (quitAfterMs > 0) {
        QTimer::singleShot(quitAfterMs, &application, &QCoreApplication::quit);
    }
    return application.exec();
}
