import QtQuick

Item {
    anchors.fill: parent
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: hyprcast.settings.cornerRadius
        color: hyprcast.settings.backgroundColor
        opacity: hyprcast.settings.backgroundOpacity
    }

    Item {
        id: historyArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: heldRow.visible ? heldRow.top : parent.bottom
        anchors.leftMargin: hyprcast.settings.historyPaddingX
        anchors.rightMargin: hyprcast.settings.historyPaddingX
        anchors.topMargin: hyprcast.settings.showHeldKeys && heldRow.visible ? 8 : 0
        anchors.bottomMargin: heldRow.visible ? 4 : 0

        Loader {
            id: activePresentation
            anchors.fill: parent
            property var historyModel: hyprcast.history
            source: hyprcast.settings.presentation === "keycaps" ? Qt.resolvedUrl("../../KeycapPresentation.qml") : Qt.resolvedUrl("../../TextPresentation.qml")
            onLoaded: item.historyModel = activePresentation.historyModel
        }

        Loader {
            id: fadingPresentation
            anchors.fill: parent
            property var historyModel: hyprcast.expiredHistory
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
        anchors.leftMargin: hyprcast.settings.heldRowPaddingX
        anchors.rightMargin: hyprcast.settings.heldRowPaddingX
        anchors.bottomMargin: hyprcast.settings.heldRowPaddingBottom
        height: visible ? hyprcast.settings.heldKeyHeight : 0
        spacing: hyprcast.settings.heldKeySpacing
        visible: hyprcast.settings.showHeldKeys && hyprcast.heldKeyCount > 0

        Repeater {
            model: hyprcast.heldKeys

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + hyprcast.settings.heldKeyPaddingX * 2
                implicitHeight: hyprcast.settings.heldKeyHeight
                radius: hyprcast.settings.heldKeyRadius
                color: hyprcast.settings.heldKeyBackground

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: hyprcast.settings.heldKeyTextColor
                    font.family: hyprcast.settings.fontFamily
                    font.pixelSize: hyprcast.settings.heldFontSize
                    font.weight: hyprcast.settings.fontWeight
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
