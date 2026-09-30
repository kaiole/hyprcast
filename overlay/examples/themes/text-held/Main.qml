import QtQuick

Item {
    id: root
    objectName: "hyprcastTextHeldRoot"
    anchors.fill: parent
    clip: true

    // Choose composition from the maximum surface, not transient content.
    readonly property real historyLineHeight: Math.max(naturalActiveText.implicitHeight, naturalExpiredText.implicitHeight, hyprcast.settings.fontSize * 1.45)
    readonly property real stackedHeight: historyLineHeight + hyprcast.options.held_key_height +
                                         hyprcast.options.held_row_padding_bottom + hyprcast.settings.panelBorderWidth * 2 + 12
    readonly property bool stacked: hyprcast.options.layout === "stacked" ||
                                    (hyprcast.options.layout === "auto" && hyprcast.settings.height >= stackedHeight)
    readonly property bool heldLeft: hyprcast.options.held_side === "left"
    readonly property real compactFraction: hyprcast.options.compact_held_fraction
    readonly property bool reserveHeld: hyprcast.options.show_held_keys
    readonly property bool heldVisible: hyprcast.options.show_held_keys && hyprcast.heldKeyCount > 0
    readonly property bool hasPanelContent: hyprcast.historyCount > 0 || hyprcast.fading || heldVisible
    readonly property bool panelDecorationVisible: hyprcast.settings.panelVisibility === "always" ||
                                                   (hyprcast.settings.panelVisibility === "with-content" && hasPanelContent)
    readonly property real measuredHistoryWidth: Math.max(naturalActiveText.implicitWidth, naturalExpiredText.implicitWidth)
    readonly property real requiredPanelWidth: {
        const border = hyprcast.settings.panelBorderWidth * 2
        const historyExtra = hyprcast.settings.textExtraPaddingX * 2
        const historyWidth = measuredHistoryWidth + historyExtra + hyprcast.settings.historyPaddingX * 2 + border
        // In compact mode reserve a stable fraction even when no keys are down.
        const compactWidth = reserveHeld && !stacked ? historyWidth / (1 - compactFraction) : historyWidth
        const heldWidth = reserveHeld && stacked ? heldRow.implicitWidth + hyprcast.options.held_row_padding_x * 2 + border : 0
        return Math.max(hyprcast.settings.minWidth, compactWidth, heldWidth)
    }
    readonly property real requiredPanelHeight: {
        const contentHeight = reserveHeld && stacked ? stackedHeight :
                              Math.max(historyLineHeight, reserveHeld ? hyprcast.options.held_key_height : 0) + hyprcast.settings.panelBorderWidth * 2 + 8
        return Math.max(hyprcast.settings.minHeight, contentHeight)
    }

    Item {
        id: panelFrame
        objectName: "hyprcastPanelFrame"
        x: hyprcast.settings.anchor.indexOf("right") >= 0 ? parent.width - width :
           (hyprcast.settings.anchor.indexOf("left") >= 0 ? 0 : (parent.width - width) / 2)
        y: hyprcast.settings.anchor.indexOf("bottom") >= 0 ? parent.height - height :
           (hyprcast.settings.anchor.indexOf("top") >= 0 ? 0 : (parent.height - height) / 2)
        width: hyprcast.settings.dynamicSize ? Math.min(hyprcast.settings.width, root.requiredPanelWidth) : hyprcast.settings.width
        height: hyprcast.settings.dynamicSize ? Math.min(hyprcast.settings.height, root.requiredPanelHeight) : hyprcast.settings.height
        clip: true

        Rectangle {
            objectName: "hyprcastPanelBackground"
            anchors.fill: parent
            radius: hyprcast.settings.cornerRadius
            visible: root.panelDecorationVisible
            opacity: hyprcast.fading && hyprcast.historyCount === 0 && !root.heldVisible ? fadingPresentation.snapshotOpacity : 1
            color: {
                const base = Qt.color(hyprcast.settings.backgroundColor)
                return Qt.rgba(base.r, base.g, base.b, base.a * hyprcast.settings.backgroundOpacity)
            }
        }

        Rectangle {
            objectName: "hyprcastPanelBorder"
            anchors.fill: parent
            radius: hyprcast.settings.cornerRadius
            color: "transparent"
            visible: root.panelDecorationVisible && hyprcast.settings.panelBorderWidth > 0
            opacity: hyprcast.fading && hyprcast.historyCount === 0 && !root.heldVisible ? fadingPresentation.snapshotOpacity : 1
            border.width: hyprcast.settings.panelBorderWidth
            border.color: hyprcast.settings.panelBorderColor
        }

        Item {
            id: historyArea
            objectName: "hyprcastHistoryArea"
            x: root.reserveHeld && !root.stacked && root.heldLeft ? heldViewport.x + heldViewport.width + 8 :
               hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
            y: hyprcast.settings.panelBorderWidth + 4
            width: Math.max(0, (root.reserveHeld && !root.stacked && !root.heldLeft ? heldViewport.x - 8 :
                               parent.width - hyprcast.settings.historyPaddingX - hyprcast.settings.panelBorderWidth) - x)
            height: Math.max(0, (root.reserveHeld && root.stacked ? heldViewport.y - 4 : parent.height - hyprcast.settings.panelBorderWidth - 4) - y)

            Loader {
                id: activePresentation
                anchors.fill: parent
                property var historyModel: hyprcast.displayHistory
                source: Qt.resolvedUrl("TextPresentation.qml")
                onLoaded: item.historyModel = activePresentation.historyModel
            }

            Loader {
                id: fadingPresentation
                anchors.fill: parent
                property var historyModel: hyprcast.expiredDisplayHistory
                property real snapshotOpacity: 1
                opacity: snapshotOpacity
                visible: hyprcast.fading
                source: Qt.resolvedUrl("TextPresentation.qml")
                onLoaded: item.historyModel = fadingPresentation.historyModel

                NumberAnimation {
                    id: snapshotFade
                    target: fadingPresentation
                    property: "snapshotOpacity"
                    from: 1
                    to: 0
                    duration: hyprcast.fadeDurationMs
                    easing.type: Easing.OutCubic
                }
            }
        }

        Item {
            id: heldViewport
            objectName: "hyprcastHeldViewport"
            readonly property real inset: hyprcast.options.held_row_padding_x + hyprcast.settings.panelBorderWidth
            x: root.stacked || root.heldLeft ? inset : parent.width * (1 - root.compactFraction)
            width: Math.max(0, (root.stacked ? parent.width : parent.width * root.compactFraction) -
                              (root.stacked ? inset * 2 : inset))
            // On short forced-stacked surfaces shrink chips before sacrificing history.
            height: Math.max(0, Math.min(hyprcast.options.held_key_height,
                                        parent.height - hyprcast.settings.panelBorderWidth * 2 - 8 -
                                        (root.stacked ? root.historyLineHeight + hyprcast.options.held_row_padding_bottom + 4 : 0)))
            y: root.stacked ? parent.height - hyprcast.settings.panelBorderWidth - hyprcast.options.held_row_padding_bottom - height : (parent.height - height) / 2
            clip: true
            visible: root.reserveHeld

        Row {
            id: heldRow
            objectName: "hyprcastHeldRow"
            x: root.heldLeft ? 0 : Math.max(0, parent.width - implicitWidth)
            height: parent.height
            spacing: hyprcast.options.held_key_spacing
            visible: root.heldVisible

            Repeater {
                model: hyprcast.heldKeyItems

                delegate: Rectangle {
                    required property var modelData

                    implicitWidth: label.implicitWidth + hyprcast.options.held_key_padding_x * 2
                    implicitHeight: heldViewport.height
                    radius: hyprcast.options.held_key_radius
                    color: modelData.kind === "text" ? hyprcast.options.held_text_background : hyprcast.options.held_key_background
                    border.width: hyprcast.options.held_key_border_width
                    border.color: hyprcast.options.held_key_border_color

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: modelData.label
                        color: hyprcast.options.held_key_text_color
                        font.family: modelData.kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ?
                                     hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                        font.pixelSize: Math.min(hyprcast.options.held_font_size, Math.max(1, heldViewport.height - hyprcast.options.held_key_border_width * 2 - 4))
                        font.weight: hyprcast.settings.fontWeight
                    }
                }
            }
        }
    }

    }

    Text {
        id: naturalActiveText
        x: -10000
        y: -10000
        text: hyprcast.displayHistory.displayRichText
        textFormat: Text.RichText
        font.family: hyprcast.settings.fontFamily
        font.pixelSize: hyprcast.settings.fontSize
        font.weight: hyprcast.settings.fontWeight
        wrapMode: Text.NoWrap
        visible: false
    }

    Text {
        id: naturalExpiredText
        x: -10000
        y: -10000
        text: hyprcast.expiredDisplayHistory.displayRichText
        textFormat: Text.RichText
        font.family: hyprcast.settings.fontFamily
        font.pixelSize: hyprcast.settings.fontSize
        font.weight: hyprcast.settings.fontWeight
        wrapMode: Text.NoWrap
        visible: false
    }

    Connections {
        target: hyprcast

        function onFadingChanged() {
            snapshotFade.stop()
            fadingPresentation.snapshotOpacity = 1
            if (hyprcast.fading)
                snapshotFade.start()
        }

        function onFadeDurationMsChanged() {
            if (hyprcast.fading) {
                snapshotFade.stop()
                fadingPresentation.snapshotOpacity = 1
                snapshotFade.start()
            }
        }
    }

    Component.onCompleted: {
        if (hyprcast.fading)
            snapshotFade.start()
    }
}
