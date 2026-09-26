import QtQuick

Item {
    id: root

    property var historyModel: null

    Text {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        text: root.historyModel ? root.historyModel.displayText : ""
        color: "#FFFFFF"
        font.pixelSize: 30
        font.weight: Font.Medium
        wrapMode: Text.NoWrap
        elide: Text.ElideLeft
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
    }
}
