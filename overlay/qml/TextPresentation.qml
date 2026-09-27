import QtQuick

Item {
    property var historyModel: null

    Item {
        id: viewport
        anchors.fill: parent
        anchors.leftMargin: hyprcast.settings.textExtraPaddingX
        anchors.rightMargin: hyprcast.settings.textExtraPaddingX
        clip: true

        Text {
            objectName: "hyprcastTailText"
            // Rich text cannot use Text.ElideLeft. Keep the full line and shift it
            // left when it outgrows the viewport, so the newest input stays visible.
            x: Math.min(0, viewport.width - width)
            anchors.verticalCenter: parent.verticalCenter
            text: historyModel ? historyModel.displayRichText : ""
            textFormat: Text.RichText
            color: hyprcast.settings.foregroundColor
            font.family: hyprcast.settings.fontFamily
            font.pixelSize: hyprcast.settings.fontSize
            font.weight: hyprcast.settings.fontWeight
            wrapMode: Text.NoWrap
        }
    }
}
