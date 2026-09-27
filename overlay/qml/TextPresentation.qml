import QtQuick

Item {
    property var historyModel: null

    Text {
        anchors.fill: parent
        anchors.leftMargin: hyprcast.settings.textExtraPaddingX
        anchors.rightMargin: hyprcast.settings.textExtraPaddingX
        text: historyModel ? historyModel.displayRichText : ""
        textFormat: Text.RichText
        color: hyprcast.settings.foregroundColor
        font.family: hyprcast.settings.fontFamily
        font.pixelSize: hyprcast.settings.fontSize
        font.weight: hyprcast.settings.fontWeight
        wrapMode: Text.NoWrap
        elide: Text.ElideLeft
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
    }
}
