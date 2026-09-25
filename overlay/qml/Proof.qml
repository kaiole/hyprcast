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

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: "transparent"
        border.width: 1
        border.color: "#4AFFFFFF"
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        spacing: 14

        Rectangle {
            width: 48
            height: 48
            anchors.verticalCenter: parent.verticalCenter
            radius: 10
            color: "#263A5B"

            Text {
                anchors.centerIn: parent
                text: "⌨"
                color: "#D8E7FF"
                font.pixelSize: 23
            }
        }

        Column {
            width: root.width - 114
            anchors.verticalCenter: parent.verticalCenter
            spacing: 5

            Text {
                width: parent.width
                text: "HYPRCAST  /  LAYER-SHELL PROOF"
                color: "#AAB8CC"
                font.pixelSize: 11
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: hyprcastDemoText
                color: "#FFFFFF"
                font.pixelSize: 18
                elide: Text.ElideRight
                wrapMode: Text.NoWrap
            }
        }
    }
}
