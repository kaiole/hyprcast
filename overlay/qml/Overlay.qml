import QtQuick

Item {
    id: root

    width: 600
    height: 88
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: hyprcastConfig.values.cornerRadius
        color: hyprcastConfig.values.backgroundColor
        opacity: hyprcastConfig.values.backgroundOpacity
    }

    Item {
        id: historyArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: heldRow.visible ? heldRow.top : parent.bottom
        anchors.leftMargin: hyprcastConfig.values.historyPaddingX
        anchors.rightMargin: hyprcastConfig.values.historyPaddingX
        anchors.topMargin: hyprcastConfig.values.showHeldKeys && heldRow.visible ? 8 : 0
        anchors.bottomMargin: heldRow.visible ? 4 : 0

        Loader {
            id: activePresentation
            anchors.fill: parent
            property var historyModel: hyprcastHistory
            source: hyprcastConfig.values.presentation === "keycaps" ? "KeycapPresentation.qml" : "TextPresentation.qml"
            onLoaded: item.historyModel = activePresentation.historyModel
        }

        Loader {
            id: fadingPresentation
            anchors.fill: parent
            property var historyModel: hyprcastFadingHistory
            property real snapshotOpacity: 1
            opacity: snapshotOpacity
            visible: hyprcastKeyboardOutput.fading
            source: hyprcastConfig.values.presentation === "keycaps" ? "KeycapPresentation.qml" : "TextPresentation.qml"
            onLoaded: item.historyModel = fadingPresentation.historyModel

            NumberAnimation {
                id: snapshotFade
                target: fadingPresentation
                property: "snapshotOpacity"
                from: 1
                to: 0
                duration: hyprcastKeyboardOutput.fadeDurationMs
                easing.type: Easing.OutCubic
            }
        }
    }

    Row {
        id: heldRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: hyprcastConfig.values.heldRowPaddingX
        anchors.rightMargin: hyprcastConfig.values.heldRowPaddingX
        anchors.bottomMargin: hyprcastConfig.values.heldRowPaddingBottom
        height: visible ? hyprcastConfig.values.heldKeyHeight : 0
        spacing: hyprcastConfig.values.heldKeySpacing
        visible: hyprcastConfig.values.showHeldKeys && hyprcastKeyboardOutput.heldKeyCount > 0

        Repeater {
            model: hyprcastKeyboardOutput.heldKeys

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + hyprcastConfig.values.heldKeyPaddingX * 2
                implicitHeight: hyprcastConfig.values.heldKeyHeight
                radius: hyprcastConfig.values.heldKeyRadius
                color: hyprcastConfig.values.heldKeyBackground

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: hyprcastConfig.values.heldKeyTextColor
                    font.family: hyprcastConfig.values.fontFamily
                    font.pixelSize: hyprcastConfig.values.heldFontSize
                    font.weight: hyprcastConfig.values.fontWeight
                }
            }
        }
    }

    Connections {
        target: hyprcastKeyboardOutput

        function onFadingChanged() {
            snapshotFade.stop()
            fadingPresentation.snapshotOpacity = 1
            if (hyprcastKeyboardOutput.fading)
                snapshotFade.start()
        }

        function onFadeDurationMsChanged() {
            if (hyprcastKeyboardOutput.fading) {
                snapshotFade.stop()
                fadingPresentation.snapshotOpacity = 1
                snapshotFade.start()
            }
        }
    }

    Component.onCompleted: {
        if (hyprcastKeyboardOutput.fading)
            snapshotFade.start()
    }
}
