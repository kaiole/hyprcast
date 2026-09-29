import QtQuick

Item {
    property var historyModel: null
    readonly property real naturalWidth: capRow.width

    Item {
        id: viewport
        anchors.fill: parent
        clip: true

        Row {
            id: capRow
            spacing: hyprcast.options.spacing
            x: Math.min(0, viewport.width - width)
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: historyModel
                delegate: KeycapEntry {
                    kind: model.kind
                    text: model.displayLabel
                    keyLabel: model.displayKey
                    modifiers: model.displayModifiers
                    counted: model.counted
                    repeatCount: model.repeatCount
                    // Keep the oldest partly clipped cap whole while following the tail.
                    opacity: x + capRow.x >= -0.5 ? 1 : 0
                }
            }
        }
    }
}
