import QtQuick

Item {
    id: root
    anchors.fill: parent
    clip: true

    readonly property bool hasPanelContent: hyprcast.historyCount > 0 || hyprcast.fading
    readonly property bool panelDecorationVisible: hyprcast.settings.panelVisibility === "always" ||
                                                   (hyprcast.settings.panelVisibility === "with-content" && hasPanelContent)
    readonly property real measuredHistoryWidth: Math.max(activePresentation.item ? activePresentation.item.naturalWidth : 0,
                                                          fadingPresentation.item ? fadingPresentation.item.naturalWidth : 0)
    readonly property real requiredPanelWidth: {
        const border = hyprcast.settings.panelBorderWidth * 2
        const historyWidth = measuredHistoryWidth + hyprcast.settings.historyPaddingX * 2 + border
        return Math.max(hyprcast.settings.minWidth, historyWidth)
    }
    readonly property real requiredPanelHeight: {
        const historyHeight = hyprcast.options.height + 8
        return Math.max(hyprcast.settings.minHeight, historyHeight + hyprcast.settings.panelBorderWidth * 2)
    }

    Item {
        id: panelFrame
        objectName: "hyprcastKeycapsPanelFrame"
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
            opacity: hyprcast.fading && hyprcast.historyCount === 0 ? fadingPresentation.snapshotOpacity : 1
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
            opacity: hyprcast.fading && hyprcast.historyCount === 0 ? fadingPresentation.snapshotOpacity : 1
            border.width: hyprcast.settings.panelBorderWidth
            border.color: hyprcast.settings.panelBorderColor
        }

        Item {
            id: historyArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
            anchors.rightMargin: hyprcast.settings.historyPaddingX + hyprcast.settings.panelBorderWidth
            anchors.topMargin: hyprcast.settings.panelBorderWidth
            anchors.bottomMargin: hyprcast.settings.panelBorderWidth

            Loader {
                id: activePresentation
                anchors.fill: parent
                property var historyModel: hyprcast.displayHistory
                source: Qt.resolvedUrl("KeycapPresentation.qml")
                onLoaded: item.historyModel = activePresentation.historyModel
            }

            Loader {
                id: fadingPresentation
                anchors.fill: parent
                property var historyModel: hyprcast.expiredDisplayHistory
                property real snapshotOpacity: 1
                opacity: snapshotOpacity
                visible: hyprcast.fading
                source: Qt.resolvedUrl("KeycapPresentation.qml")
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
