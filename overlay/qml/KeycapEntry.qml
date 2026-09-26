import QtQuick

Item {
    id: root

    property string kind: "text"
    property string text: ""
    property string keyLabel: ""
    property var modifiers: []
    readonly property var labels: {
        if (kind === "chord") {
            const result = Array.from(modifiers);
            if (keyLabel.length > 0)
                result.push(keyLabel);
            return result;
        }
        return [kind === "text" ? text : keyLabel];
    }

    width: caps.implicitWidth
    height: caps.implicitHeight

    Row {
        id: caps
        spacing: hyprcastConfig.values.keycapInnerSpacing

        Repeater {
            model: root.labels

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + hyprcastConfig.values.keycapPaddingX * 2
                implicitHeight: hyprcastConfig.values.keycapHeight
                radius: hyprcastConfig.values.keycapRadius
                color: root.kind === "text" ? hyprcastConfig.values.keycapTextBackground : hyprcastConfig.values.keycapKeyBackground
                border.width: 1
                border.color: hyprcastConfig.values.keycapBorderColor

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: hyprcastConfig.values.keycapTextColor
                    font.family: hyprcastConfig.values.fontFamily
                    font.pixelSize: hyprcastConfig.values.keycapFontSize
                    font.weight: hyprcastConfig.values.fontWeight
                }
            }
        }
    }
}
