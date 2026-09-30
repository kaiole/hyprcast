import QtQuick

Item {
    id: group
    property string kind: "text"
    property string label: ""
    property string keyLabel: ""
    property var modifiers: []
    property bool counted: false
    property int repeatCount: 1
    property real availableWidth: 0
    property real capHeight: hyprcast.options.height
    readonly property var labels: {
        if (kind !== "chord") return [kind === "text" ? label : keyLabel]
        const result = Array.from(modifiers)
        if (keyLabel.length) result.push(keyLabel)
        return result
    }
    readonly property real fitScale: width > 0 ? Math.min(1, availableWidth / width) : 1
    width: caps.width
    height: capHeight
    Row {
        id: caps
        spacing: hyprcast.options.inner_spacing
        Repeater {
            model: group.labels
            delegate: Keycap {
                required property string modelData
                label: modelData
                printable: group.kind === "text"
                capHeight: group.capHeight
                labelLimit: Math.max(0, group.availableWidth * 0.65)
            }
        }
        Text {
            visible: group.counted
            width: visible ? implicitWidth : 0
            height: group.height
            verticalAlignment: Text.AlignVCenter
            text: "×" + group.repeatCount
            textFormat: Text.PlainText
            color: hyprcast.options.text_color
            font.family: hyprcast.settings.fontFamily
            font.weight: hyprcast.settings.fontWeight
            font.pixelSize: Math.max(1, Math.min(hyprcast.options.font_size * 0.72, group.capHeight * 0.5))
        }
    }
}
