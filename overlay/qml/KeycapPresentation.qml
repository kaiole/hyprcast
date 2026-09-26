import QtQuick

Item {
    id: root

    property var historyModel: null

    ListView {
        id: historyView

        anchors.fill: parent
        clip: true
        orientation: ListView.Horizontal
        spacing: 6
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: width
        model: root.historyModel

        delegate: KeycapEntry {
            kind: model.kind
            text: model.text
            keyLabel: model.key
            modifiers: model.modifiers
            // Preserve keycap boundaries at the clipped left edge; the newest
            // entries stay right-aligned while an overflowing oldest cap is hidden whole.
            visible: x >= historyView.contentX - 0.5
        }

        function followTail() {
            if (count > 0)
                positionViewAtEnd();
        }

        onCountChanged: Qt.callLater(followTail)
        onContentWidthChanged: Qt.callLater(followTail)
        Component.onCompleted: Qt.callLater(followTail)
    }
}
