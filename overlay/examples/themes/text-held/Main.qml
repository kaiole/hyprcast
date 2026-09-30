import QtQuick

Item {
    id: root
    objectName: "hyprcastTextHeldRoot"
    anchors.fill: parent
    clip: true
    readonly property var slots: hyprcast.options.show_altgr ? ["Ctrl", "Shift", "Alt", "Super", "AltGr"] : ["Ctrl", "Shift", "Alt", "Super"]
    readonly property var observed: {
        const result = {}
        for (const item of hyprcast.heldKeyItems)
            if (item.kind === "modifier") result[item.identity] = true
        return result
    }
    readonly property bool heldVisible: hyprcast.options.show_held_keys && slots.some(function(identity) { return root.observed[identity] === true })
    readonly property bool hasContent: hyprcast.historyCount > 0 || hyprcast.fading || heldVisible
    readonly property bool contentVisible: hasContent || hyprcast.settings.panelVisibility === "always"
    readonly property bool decorated: hyprcast.settings.panelVisibility === "always" ||
                                      (hyprcast.settings.panelVisibility === "with-content" && hasContent)
    readonly property real contentOpacity: hyprcast.settings.panelVisibility !== "always" &&
                                           hyprcast.historyCount === 0 && hyprcast.fading && !heldVisible ? snapshot.opacity : 1
    readonly property real insetX: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
    readonly property real insetY: hyprcast.options.inner_padding + hyprcast.settings.panelBorderWidth
    readonly property real preferredHeight: insetY * 2 + hyprcast.options.height * 2 + hyprcast.options.row_gap
    Item {
        id: panel
        objectName: "hyprcastPanelFrame"
        width: Math.max(0, Math.min(root.width, hyprcast.settings.width,
               hyprcast.settings.dynamicSize ? Math.max(hyprcast.settings.minWidth, 600) : hyprcast.settings.width))
        height: Math.max(0, Math.min(root.height, hyprcast.settings.height,
                hyprcast.settings.dynamicSize ? Math.max(hyprcast.settings.minHeight, root.preferredHeight) : hyprcast.settings.height))
        x: hyprcast.settings.anchor.indexOf("right") >= 0 ? root.width - width :
           (hyprcast.settings.anchor.indexOf("left") >= 0 ? 0 : (root.width - width) / 2)
        y: hyprcast.settings.anchor.indexOf("bottom") >= 0 ? root.height - height :
           (hyprcast.settings.anchor.indexOf("top") >= 0 ? 0 : (root.height - height) / 2)
        readonly property real bandHeight: Math.max(0, Math.min(hyprcast.options.height,
                (height - root.insetY * 2 - hyprcast.options.row_gap) / 2))
        clip: true
        Rectangle {
            id: background
            objectName: "hyprcastPanelBackground"
            anchors.fill: parent
            visible: root.decorated
            radius: hyprcast.settings.cornerRadius
            opacity: root.contentOpacity
            color: {
                const c = Qt.color(hyprcast.settings.backgroundColor)
                return Qt.rgba(c.r, c.g, c.b, c.a * hyprcast.settings.backgroundOpacity)
            }
            border.width: hyprcast.settings.panelBorderWidth
            border.color: hyprcast.settings.panelBorderColor
        }
        Item {
            id: historyArea
            objectName: "hyprcastHistoryArea"
            x: Math.min(root.insetX, panel.width / 2)
            y: Math.min(root.insetY, panel.height / 2)
            width: Math.max(0, panel.width - root.insetX * 2)
            height: panel.bandHeight
            clip: true
            RibbonStage {
                id: active
                objectName: "textHeldActiveStage"
                anchors.fill: parent
                capHeight: panel.bandHeight
                historyModel: hyprcast.displayHistory
                visible: hyprcast.historyCount > 0
            }
            RibbonStage {
                id: snapshot
                objectName: "textHeldExpiredStage"
                anchors.fill: parent
                capHeight: panel.bandHeight
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
        Item {
            id: dock
            objectName: "hyprcastHeldViewport"
            x: historyArea.x
            y: Math.min(panel.height, historyArea.y + historyArea.height + hyprcast.options.row_gap)
            width: historyArea.width
            height: panel.bandHeight
            visible: root.contentVisible && hyprcast.options.show_held_keys
            opacity: root.contentOpacity
            clip: true
            readonly property real spacing: Math.min(hyprcast.options.dock_spacing, width / Math.max(1, root.slots.length * 2))
            readonly property real slotWidth: Math.max(0, Math.min(panel.bandHeight * 2.4, (width - (root.slots.length - 1) * spacing) / root.slots.length))
            Item {
                id: dockRow
                objectName: "hyprcastHeldRow"
                width: root.slots.length * dock.slotWidth + (root.slots.length - 1) * dock.spacing
                height: dock.height
                x: hyprcast.options.dock_alignment === "right" ? dock.width - width :
                   (hyprcast.options.dock_alignment === "left" ? 0 : (dock.width - width) / 2)
                Repeater {
                    model: root.slots
                    delegate: Keycap {
                        required property string modelData
                        required property int index
                        x: index * (dock.slotWidth + dock.spacing)
                        objectName: "modifierSlot" + modelData
                        property string identity: modelData
                        width: dock.slotWidth
                        capHeight: panel.bandHeight
                        label: hyprcast.settings.modifierSymbols[identity] === undefined ? identity : hyprcast.settings.modifierSymbols[identity]
                        live: true
                        pressed: root.observed[identity] === true
                    }
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
