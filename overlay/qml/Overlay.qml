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

    Text {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        text: hyprcastKeyboardOutput.outputText
        color: "#FFFFFF"
        font.pixelSize: 30
        font.weight: Font.Medium
        elide: Text.ElideLeft
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
    }
}
