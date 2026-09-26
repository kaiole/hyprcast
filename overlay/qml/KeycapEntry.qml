import QtQuick

Item {
    id: root

    property string kind: "text"
    property string text: ""
    property string keyLabel: ""
    property var modifiers: []
    readonly property var labels: {
        if (root.kind === "chord") {
            const result = Array.from(root.modifiers)
            if (root.keyLabel.length > 0)
                result.push(root.keyLabel)
            return result
        }
        return [root.kind === "text" ? root.text : root.keyLabel]
    }

    width: caps.implicitWidth
    height: caps.implicitHeight

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
                border.width: 1
                border.color: hyprcast.settings.keycapBorderColor

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: hyprcast.settings.keycapTextColor
                    font.family: hyprcast.settings.fontFamily
                    font.pixelSize: hyprcast.settings.keycapFontSize
                    font.weight: hyprcast.settings.fontWeight
                }
            }
        }
    }
}
