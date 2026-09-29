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

    width: caps.implicitWidth + (counted ? countBadge.implicitWidth + hyprcast.options.inner_spacing : 0)
    height: Math.max(caps.implicitHeight, counted ? countBadge.implicitHeight : 0)

    Row {
        id: caps
        spacing: hyprcast.options.inner_spacing

        Repeater {
            model: root.counted ? Math.min(root.repeatCount, hyprcast.settings.repeatCountThreshold - 1) : 1

            delegate: Row {
                spacing: hyprcast.options.inner_spacing

                Repeater {
                    model: root.labels

                    delegate: Rectangle {
                        required property string modelData

                        implicitWidth: label.implicitWidth + hyprcast.options.padding_x * 2
                        implicitHeight: hyprcast.options.height
                        radius: hyprcast.options.radius
                        color: root.kind === "text" ? hyprcast.options.text_background : hyprcast.options.key_background
                        border.width: hyprcast.options.border_width
                        border.color: hyprcast.options.border_color

                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: modelData
                            color: hyprcast.options.text_color
                            font.family: root.kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ? hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                            font.pixelSize: hyprcast.options.font_size
                            font.weight: hyprcast.settings.fontWeight
                        }
                    }
                }
            }
        }
    }

    Text {
        id: countBadge
        anchors.left: caps.right
        anchors.leftMargin: hyprcast.options.inner_spacing
        anchors.bottom: caps.bottom
        anchors.bottomMargin: -Math.max(2, hyprcast.options.font_size * 0.15)
        visible: root.counted
        text: "…" + root.repeatCount + "x"
        color: hyprcast.settings.foregroundColor
        font.family: hyprcast.settings.fontFamily
        font.pixelSize: Math.max(10, hyprcast.options.font_size * 0.72)
        font.weight: hyprcast.settings.fontWeight
    }
}
