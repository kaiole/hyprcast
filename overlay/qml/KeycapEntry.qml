import QtQuick

Item {
    id: root

    property string kind: "text"
    property string text: ""
    property string keyLabel: ""
    property var modifiers: []
    property bool counted: false
    property int repeatCount: 1
    readonly property var labels: {
        if (root.kind === "chord") {
            const result = Array.from(root.modifiers)
            if (root.keyLabel.length > 0)
                result.push(root.keyLabel)
            return result
        }
        return [root.kind === "text" ? root.text : root.keyLabel]
    }

    width: caps.implicitWidth + (counted ? countBadge.implicitWidth + hyprcast.settings.keycapInnerSpacing : 0)
    height: Math.max(caps.implicitHeight, counted ? countBadge.implicitHeight : 0)

    Row {
        id: caps
        spacing: hyprcast.settings.keycapInnerSpacing

        Repeater {
            model: labels

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + hyprcast.settings.keycapPaddingX * 2
                implicitHeight: hyprcast.settings.keycapHeight
                radius: hyprcast.settings.keycapRadius
                color: root.kind === "text" ? hyprcast.settings.keycapTextBackground : hyprcast.settings.keycapKeyBackground
                border.width: hyprcast.settings.keycapBorderWidth
                border.color: hyprcast.settings.keycapBorderColor

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: hyprcast.settings.keycapTextColor
                    font.family: root.kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ? hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                    font.pixelSize: hyprcast.settings.keycapFontSize
                    font.weight: hyprcast.settings.fontWeight
                }
            }
        }
    }

    Text {
        id: countBadge
        anchors.left: caps.right
        anchors.leftMargin: hyprcast.settings.keycapInnerSpacing
        anchors.verticalCenter: parent.verticalCenter
        visible: root.counted
        text: "x" + root.repeatCount
        color: hyprcast.settings.foregroundColor
        font.family: hyprcast.settings.fontFamily
        font.pixelSize: Math.max(10, hyprcast.settings.keycapFontSize * 0.72)
        font.weight: hyprcast.settings.fontWeight
    }
}
