import QtQuick

Item {
    id: root

    property var historyModel: null

    Text {
        anchors.fill: parent
        anchors.leftMargin: hyprcastConfig.values.textExtraPaddingX
        anchors.rightMargin: hyprcastConfig.values.textExtraPaddingX
        text: root.historyModel ? root.historyModel.displayText : ""
        color: hyprcastConfig.values.foregroundColor
        font.family: hyprcastConfig.values.fontFamily
        font.pixelSize: hyprcastConfig.values.fontSize
        font.weight: hyprcastConfig.values.fontWeight
        wrapMode: Text.NoWrap
        elide: Text.ElideLeft
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
    }
}
