import QtQuick

Item {
    id: root
    anchors.fill: parent
    clip: true
    readonly property bool hasContent: hyprcast.historyCount > 0 || hyprcast.fading
    readonly property bool decorated: hyprcast.settings.panelVisibility === "always" ||
                                     (hyprcast.settings.panelVisibility === "with-content" && hasContent)
    readonly property real insetX: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
    readonly property real insetY: hyprcast.settings.panelBorderWidth + 8
    readonly property real preferredHeight: {
        let h = insetY * 2
        for (let i = 0; i < hyprcast.options.visible_groups; ++i)
            h += hyprcast.options.height * Math.max(0.65, Math.pow(hyprcast.options.depth_scale, Math.max(0, i - 1)))
        return h + (hyprcast.options.visible_groups - 1) * hyprcast.options.row_spacing
    }
    Item {
        id: panel
        objectName: "cascadePanelFrame"
        width: Math.max(0, Math.min(root.width, hyprcast.settings.width,
               hyprcast.settings.dynamicSize ? Math.max(hyprcast.settings.minWidth, 420) : hyprcast.settings.width))
        height: Math.max(0, Math.min(root.height, hyprcast.settings.height,
                hyprcast.settings.dynamicSize ? Math.max(hyprcast.settings.minHeight, root.preferredHeight) : hyprcast.settings.height))
        x: hyprcast.settings.anchor.indexOf("right") >= 0 ? root.width - width :
           (hyprcast.settings.anchor.indexOf("left") >= 0 ? 0 : (root.width - width) / 2)
        y: hyprcast.settings.anchor.indexOf("bottom") >= 0 ? root.height - height :
           (hyprcast.settings.anchor.indexOf("top") >= 0 ? 0 : (root.height - height) / 2)
        clip: true
        Rectangle {
            objectName: "hyprcastPanelBackground"
            anchors.fill: parent
            visible: root.decorated
            radius: hyprcast.settings.cornerRadius
            opacity: hyprcast.historyCount === 0 && hyprcast.fading ? snapshot.opacity : 1
            color: {
                const c = Qt.color(hyprcast.settings.backgroundColor)
                return Qt.rgba(c.r, c.g, c.b, c.a * hyprcast.settings.backgroundOpacity)
            }
            border.width: hyprcast.settings.panelBorderWidth
            border.color: hyprcast.settings.panelBorderColor
        }
        Item {
            id: viewport
            x: root.insetX
            y: root.insetY
            width: Math.max(0, panel.width - root.insetX * 2)
            height: Math.max(0, panel.height - root.insetY * 2)
            clip: true
            CascadeStage {
                id: active
                objectName: "cascadeActiveStage"
                anchors.fill: parent
                historyModel: hyprcast.displayHistory
                visible: hyprcast.historyCount > 0
            }
            CascadeStage {
                id: snapshot
                objectName: "cascadeExpiredStage"
                anchors.fill: parent
                historyModel: hyprcast.expiredDisplayHistory
                animateAdds: false
                visible: hyprcast.fading && hyprcast.historyCount === 0
                NumberAnimation {
                    id: fade
                    target: snapshot
                    property: "opacity"
                    from: 1
                    to: 0
                    duration: hyprcast.fadeDurationMs
                    easing.type: Easing.OutCubic
                }
            }
        }
    }
    function updateFade() {
        fade.stop()
        snapshot.opacity = 1
        if (hyprcast.fading) fade.start()
    }
    Connections {
        target: hyprcast
        function onFadingChanged() { root.updateFade() }
        function onFadeDurationMsChanged() { root.updateFade() }
    }
    Component.onCompleted: updateFade()
}
