import QtQuick

Item {
    id: root
    anchors.fill: parent
    clip: true

    readonly property bool heldVisible: hyprcast.settings.showHeldKeys && hyprcast.heldKeyCount > 0
    readonly property bool hasPanelContent: hyprcast.historyCount > 0 || hyprcast.fading || heldVisible
    readonly property bool panelDecorationVisible: hyprcast.settings.panelVisibility === "always" ||
                                                   (hyprcast.settings.panelVisibility === "with-content" && hasPanelContent)
    readonly property real measuredHistoryWidth: {
        if (hyprcast.settings.presentation === "keycaps") {
            const activeWidth = activePresentation.item ? (activePresentation.item.naturalWidth || 0) : 0
            const fadingWidth = fadingPresentation.item ? (fadingPresentation.item.naturalWidth || 0) : 0
            return Math.max(activeWidth, fadingWidth)
        }
        return Math.max(naturalActiveText.implicitWidth, naturalExpiredText.implicitWidth)
    }
    readonly property real requiredPanelWidth: {
        const border = hyprcast.settings.panelBorderWidth * 2
        const historyExtra = hyprcast.settings.presentation === "text" ? hyprcast.settings.textExtraPaddingX * 2 : 0
        const historyWidth = measuredHistoryWidth + historyExtra + hyprcast.settings.historyPaddingX * 2 + border
        const heldWidth = heldVisible ? heldRow.implicitWidth + hyprcast.settings.heldRowPaddingX * 2 + border : 0
        return Math.max(hyprcast.settings.minWidth, historyWidth, heldWidth)
    }
    readonly property real requiredPanelHeight: {
        const historyHeight = Math.max(hyprcast.settings.fontSize * 1.45, hyprcast.settings.keycapHeight + 8)
        const heldExtra = heldVisible ? hyprcast.settings.heldKeyHeight + hyprcast.settings.heldRowPaddingBottom + 8 : 0
        return Math.max(hyprcast.settings.minHeight, historyHeight + hyprcast.settings.panelBorderWidth * 2 + heldExtra)
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
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: heldRow.visible ? heldRow.top : parent.bottom
            anchors.leftMargin: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
            anchors.rightMargin: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
            anchors.topMargin: hyprcast.settings.panelBorderWidth + (heldRow.visible ? 8 : 0)
            anchors.bottomMargin: heldRow.visible ? 4 : hyprcast.settings.panelBorderWidth

            Loader {
                id: activePresentation
                anchors.fill: parent
                property var historyModel: hyprcast.displayHistory
                source: hyprcast.settings.presentation === "keycaps" ? Qt.resolvedUrl("../../KeycapPresentation.qml") : Qt.resolvedUrl("../../TextPresentation.qml")
                onLoaded: item.historyModel = activePresentation.historyModel
            }

            Loader {
                id: fadingPresentation
                anchors.fill: parent
                property var historyModel: hyprcast.expiredDisplayHistory
                property real snapshotOpacity: 1
                opacity: snapshotOpacity
                visible: hyprcast.fading
                source: hyprcast.settings.presentation === "keycaps" ? Qt.resolvedUrl("../../KeycapPresentation.qml") : Qt.resolvedUrl("../../TextPresentation.qml")
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

        Row {
            id: heldRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: hyprcast.settings.heldRowPaddingX + hyprcast.settings.panelBorderWidth
            anchors.rightMargin: hyprcast.settings.heldRowPaddingX + hyprcast.settings.panelBorderWidth
            anchors.bottomMargin: hyprcast.settings.heldRowPaddingBottom + hyprcast.settings.panelBorderWidth
            height: visible ? hyprcast.settings.heldKeyHeight : 0
            spacing: hyprcast.settings.heldKeySpacing
            visible: root.heldVisible

            Repeater {
                model: hyprcast.heldKeyItems

                delegate: Rectangle {
                    required property var modelData

                    implicitWidth: label.implicitWidth + hyprcast.settings.heldKeyPaddingX * 2
                    implicitHeight: hyprcast.settings.heldKeyHeight
                    radius: hyprcast.settings.heldKeyRadius
                    color: hyprcast.settings.heldKeyBackground
                    border.width: hyprcast.settings.heldKeyBorderWidth
                    border.color: hyprcast.settings.heldKeyBorderColor

                    Text {
                        id: label
                        anchors.centerIn: parent
                        text: modelData.label
                        color: hyprcast.settings.heldKeyTextColor
                        font.family: modelData.kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ?
                                     hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                        font.pixelSize: hyprcast.settings.heldFontSize
                        font.weight: hyprcast.settings.fontWeight
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
