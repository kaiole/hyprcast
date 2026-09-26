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
        spacing: 3

        Repeater {
            model: root.labels

            delegate: Rectangle {
                required property string modelData

                implicitWidth: label.implicitWidth + 18
                implicitHeight: 40
                radius: 7
                color: root.kind === "text" ? "#2b3546" : "#39475c"
                border.width: 1
                border.color: "#66758c"

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    color: "#FFFFFF"
                    font.pixelSize: 19
                    font.weight: Font.Medium
                }
            }
        }
    }
}
