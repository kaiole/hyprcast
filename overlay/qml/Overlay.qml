import QtQuick

Item {
    id: root

    width: 600
    height: 88
    clip: true

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: Qt.rgba(0.055, 0.065, 0.085, hyprcastBackgroundOpacity)
    }

    Item {
        id: historyArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: heldRow.visible ? heldRow.top : parent.bottom
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: hyprcastShowHeldKeys && heldRow.visible ? 8 : 0
        anchors.bottomMargin: heldRow.visible ? 4 : 0

        Loader {
            id: activePresentation
            anchors.fill: parent
            property var historyModel: hyprcastHistory
            source: hyprcastPresentation === "keycaps" ? "KeycapPresentation.qml" : "TextPresentation.qml"
            onLoaded: item.historyModel = activePresentation.historyModel
        }

        Loader {
            id: fadingPresentation
            anchors.fill: parent
            property var historyModel: hyprcastFadingHistory
            property real snapshotOpacity: 1
            opacity: snapshotOpacity
            visible: hyprcastKeyboardOutput.fading
            source: hyprcastPresentation === "keycaps" ? "KeycapPresentation.qml" : "TextPresentation.qml"
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
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.bottomMargin: 10
        height: visible ? 22 : 0
        spacing: 5
        visible: hyprcastShowHeldKeys && hyprcastKeyboardOutput.heldKeyCount > 0

        Repeater {
            model: hyprcastKeyboardOutput.heldKeys

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + 14
                implicitHeight: 22
                radius: 5
                color: "#46536a"

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: "#FFFFFF"
                    font.pixelSize: 14
                    font.weight: Font.Medium
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
    }

    Component.onCompleted: {
        if (hyprcastKeyboardOutput.fading)
            snapshotFade.start()
    }
}
