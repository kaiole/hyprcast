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
    readonly property var labels: {
        if (kind !== "chord")
            return [kind === "text" ? label : keyLabel]
        const result = Array.from(modifiers)
        if (keyLabel.length) result.push(keyLabel)
        return result
    }
    // Bound individual labels before fitting the whole chord. Uniform fitting
    // preserves all modifiers and the final key rather than clipping half a cap.
    readonly property real labelLimit: Math.max(24, availableWidth * 0.65)
    readonly property real fitScale: width > 0 ? Math.min(1, availableWidth / width) : 1
    width: caps.width
    height: hyprcast.options.height

    Row {
        id: caps
        spacing: hyprcast.options.inner_spacing
        Repeater {
            model: group.labels
            delegate: Item {
                required property string modelData
                width: Math.min(labelText.implicitWidth, group.labelLimit) + hyprcast.options.padding_x * 2
                height: group.height
                Rectangle {
                    anchors.fill: parent
                    radius: hyprcast.options.radius
                    color: hyprcast.options.edge_color
                }
                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: Math.max(0, parent.height - Math.min(hyprcast.options.edge_depth, parent.height / 2))
                    radius: Math.min(hyprcast.options.radius, height / 2)
                    color: group.kind === "text" ? hyprcast.options.text_background : hyprcast.options.key_background
                    border.width: Math.min(hyprcast.options.border_width, height / 2, width / 2)
                    border.color: hyprcast.options.border_color
                    clip: true
                    Text {
                        id: labelText
                        anchors.centerIn: parent
                        width: Math.max(0, parent.width - hyprcast.options.padding_x * 2)
                        text: modelData
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        horizontalAlignment: Text.AlignHCenter
                        color: hyprcast.options.text_color
                        font.family: group.kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ? hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                        font.weight: hyprcast.settings.fontWeight
                        font.pixelSize: hyprcast.options.font_size
                    }
                }
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
            font.pixelSize: Math.max(10, hyprcast.options.font_size * 0.72)
        }
    }
}
