import QtQuick

Item {
    id: cap
    property string label: ""
    property bool printable: false
    property bool live: false
    property bool pressed: false
    property real capHeight: hyprcast.options.height
    property real labelLimit: 10000
    readonly property real padding: Math.min(hyprcast.options.padding_x, capHeight / 2)
    readonly property real depth: Math.min(hyprcast.options.edge_depth, capHeight / 2)
    readonly property real fontSize: Math.max(1, Math.min(hyprcast.options.font_size, capHeight * 0.6))
    implicitWidth: Math.min(labelText.implicitWidth, labelLimit) + padding * 2
    implicitHeight: capHeight
    width: implicitWidth
    height: capHeight
    opacity: live && !pressed ? hyprcast.options.dock_idle_opacity : 1
    Rectangle {
        anchors.fill: parent
        radius: Math.min(hyprcast.options.radius, width / 2, height / 2)
        color: hyprcast.options.edge_color
    }
    Rectangle {
        id: face
        readonly property real travel: cap.live && cap.pressed ? cap.depth * 0.7 : 0
        y: travel
        width: parent.width
        height: Math.max(0, parent.height - cap.depth)
        radius: Math.min(hyprcast.options.radius, width / 2, height / 2)
        color: cap.live && cap.pressed ? hyprcast.options.dock_active_background :
               (cap.printable ? hyprcast.options.text_background : hyprcast.options.key_background)
        border.width: Math.min(hyprcast.options.border_width, width / 2, height / 2)
        border.color: hyprcast.options.border_color
        clip: true
        Behavior on y {
            enabled: hyprcast.options.motion === "full"
            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
        }
        Text {
            id: labelText
            anchors.centerIn: parent
            width: Math.max(0, parent.width - cap.padding * 2)
            text: cap.label
            textFormat: Text.PlainText
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignHCenter
            color: hyprcast.options.text_color
            font.family: cap.printable || hyprcast.settings.symbolFontFamily.length === 0 ? hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
            font.weight: hyprcast.settings.fontWeight
            font.pixelSize: cap.fontSize
        }
    }
}
