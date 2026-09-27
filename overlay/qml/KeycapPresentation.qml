import QtQuick

Item {
    property var historyModel: null
    readonly property real naturalWidth: historyView.contentWidth

    ListView {
        id: historyView
        anchors.fill: parent
        clip: true
        orientation: ListView.Horizontal
        spacing: hyprcast.settings.keycapSpacing
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: width
        model: historyModel

        delegate: KeycapEntry {
            kind: model.kind
            text: model.text
            keyLabel: model.displayKey
            modifiers: model.displayModifiers
            counted: model.counted
            repeatCount: model.repeatCount
            // Keep caps whole at the leading edge while following the newest retained entry.
            visible: x >= historyView.contentX - 0.5
        }

        function followTail() {
            if (count > 0)
                positionViewAtEnd()
        }

        onCountChanged: Qt.callLater(followTail)
        onContentWidthChanged: Qt.callLater(followTail)
        Component.onCompleted: Qt.callLater(followTail)
    }
}
