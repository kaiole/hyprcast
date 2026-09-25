#include <LayerShellQt/Window>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMargins>
#include <QQmlContext>
#include <QQuickView>
#include <QScreen>
#include <QSize>
#include <QSurfaceFormat>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cmath>

namespace {
    using LayerWindow = LayerShellQt::Window;

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
            if (!ok || parsed[i] < 0) {
                return false;
            }
        }

        *margins = QMargins(parsed[0], parsed[1], parsed[2], parsed[3]);
        return true;
    }

    bool parsePositiveInt(const QString& value, int* result) {
        bool      ok     = false;
        const int parsed = value.toInt(&ok);
        if (!ok || parsed <= 0 || parsed > 8192) {
            return false;
        }
        *result = parsed;
        return true;
    }

    void writeError(const QString& message) {
        QTextStream(stderr) << "hyprcast-overlay: " << message << Qt::endl;
    }
} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hyprcast-overlay"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Qt Quick + LayerShellQt windowing proof for Hyprcast."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption monitorOption({QStringLiteral("m"), QStringLiteral("monitor")}, QStringLiteral("Output name (defaults to the primary output)."),
                                           QStringLiteral("name"));
    const QCommandLineOption anchorOption(QStringLiteral("anchor"), QStringLiteral("Position: top, bottom, left, right, or a corner such as bottom-right."),
                                          QStringLiteral("position"), QStringLiteral("bottom-right"));
    const QCommandLineOption marginsOption(QStringLiteral("margins"), QStringLiteral("Layer-shell margins in left,top,right,bottom order."), QStringLiteral("pixels"),
                                           QStringLiteral("24,24,24,24"));
    const QCommandLineOption widthOption(QStringLiteral("width"), QStringLiteral("Surface width in logical pixels."), QStringLiteral("pixels"), QStringLiteral("600"));
    const QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Surface height in logical pixels."), QStringLiteral("pixels"), QStringLiteral("88"));
    const QCommandLineOption opacityOption(QStringLiteral("background-opacity"), QStringLiteral("Background alpha from 0 (transparent) to 1 (opaque); text stays opaque."),
                                           QStringLiteral("alpha"), QStringLiteral("0.78"));
    const QCommandLineOption textOption(QStringLiteral("text"), QStringLiteral("Sample text (long text is clipped to the surface)."), QStringLiteral("text"));
    const QCommandLineOption quitAfterOption(QStringLiteral("quit-after-ms"), QStringLiteral("Exit after this many milliseconds (useful for smoke tests)."), QStringLiteral("ms"));

    parser.addOptions({monitorOption, anchorOption, marginsOption, widthOption, heightOption, opacityOption, textOption, quitAfterOption});
    parser.process(application);

    int width  = 0;
    int height = 0;
    if (!parsePositiveInt(parser.value(widthOption), &width) || !parsePositiveInt(parser.value(heightOption), &height)) {
        writeError(QStringLiteral("--width and --height must be integers in the range 1..8192"));
        return 2;
    }

    QMargins margins;
    if (!parseMargins(parser.value(marginsOption), &margins)) {
        writeError(QStringLiteral("--margins must contain four non-negative integers: left,top,right,bottom"));
        return 2;
    }

    LayerWindow::Anchors anchors;
    if (!parseAnchor(parser.value(anchorOption), &anchors)) {
        writeError(QStringLiteral("Invalid --anchor. Use one edge or one vertical and one horizontal edge, e.g. top or bottom-right."));
        return 2;
    }

    bool         opacityOk         = false;
    const double backgroundOpacity = parser.value(opacityOption).toDouble(&opacityOk);
    if (!opacityOk || !std::isfinite(backgroundOpacity) || backgroundOpacity < 0.0 || backgroundOpacity > 1.0) {
        writeError(QStringLiteral("--background-opacity must be a number from 0 to 1"));
        return 2;
    }

    int quitAfterMs = 0;
    if (parser.isSet(quitAfterOption)) {
        bool durationOk = false;
        quitAfterMs     = parser.value(quitAfterOption).toInt(&durationOk);
        if (!durationOk || quitAfterMs <= 0) {
            writeError(QStringLiteral("--quit-after-ms must be a positive integer"));
            return 2;
        }
    }

    QScreen* selectedScreen = application.primaryScreen();
    if (parser.isSet(monitorOption)) {
        selectedScreen              = nullptr;
        const QString requestedName = parser.value(monitorOption);
        for (QScreen* screen : application.screens()) {
            if (screen->name() == requestedName) {
                selectedScreen = screen;
                break;
            }
        }
        if (!selectedScreen) {
            writeError(QStringLiteral("Unknown monitor '%1'; available outputs:").arg(requestedName));
            for (QScreen* screen : application.screens()) {
                writeError(QStringLiteral("  %1").arg(screen->name()));
            }
            return 2;
        }
    }
    if (!selectedScreen) {
        writeError(QStringLiteral("No display is available"));
        return 1;
    }

    const QString sampleText = parser.isSet(textOption) ?
        parser.value(textOption) :
        QStringLiteral("Typed text 123  ·  [Ctrl+Shift+K]  ·  This deliberately long sample demonstrates clipping at the fixed surface width.");

    QQuickView    view;
    view.setTitle(QStringLiteral("Hyprcast Overlay Proof"));
    view.setScreen(selectedScreen);
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(width, height);
    view.setColor(Qt::transparent);

    QSurfaceFormat surfaceFormat = view.format();
    surfaceFormat.setAlphaBufferSize(8);
    view.setFormat(surfaceFormat);
    // Layer-shell keyboard focus and Wayland pointer input are separate controls.
    view.setFlags(view.flags() | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);

    view.rootContext()->setContextProperty(QStringLiteral("hyprcastDemoText"), sampleText);
    view.rootContext()->setContextProperty(QStringLiteral("hyprcastBackgroundOpacity"), backgroundOpacity);
    view.setSource(QUrl(QStringLiteral("qrc:/hyprcast/overlay/qml/Proof.qml")));
    if (view.status() == QQuickView::Error) {
        for (const auto& error : view.errors()) {
            writeError(error.toString());
        }
        return 1;
    }

    LayerWindow* layerWindow = LayerWindow::get(&view);
    layerWindow->setScope(QStringLiteral("hyprcast-overlay-proof"));
    layerWindow->setLayer(LayerWindow::LayerOverlay);
    layerWindow->setAnchors(anchors);
    layerWindow->setMargins(margins);
    layerWindow->setDesiredSize(QSize(width, height));
    layerWindow->setScreen(selectedScreen);
    layerWindow->setKeyboardInteractivity(LayerWindow::KeyboardInteractivityNone);
    layerWindow->setActivateOnShow(false);
    layerWindow->setCloseOnDismissed(true);

    QObject::connect(&application, &QGuiApplication::screenRemoved, &view, [&application, &view, selectedScreen](QScreen* removedScreen) {
        if (removedScreen != selectedScreen) {
            return;
        }
        writeError(QStringLiteral("Selected output was removed; closing the overlay."));
        view.close();
        application.quit();
    });

    qInfo().noquote() << QStringLiteral("Overlay proof on %1 (%2x%3 logical px, scale %4), anchor=%5, opacity=%6")
                             .arg(selectedScreen->name())
                             .arg(width)
                             .arg(height)
                             .arg(selectedScreen->devicePixelRatio())
                             .arg(parser.value(anchorOption))
                             .arg(backgroundOpacity);

    view.show();
    if (quitAfterMs > 0) {
        QTimer::singleShot(quitAfterMs, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
