import QtQuick

Item {
    id: stage
    property var historyModel
    property real capHeight: hyprcast.options.height
    property bool animateAdds: true
    property bool initialized: false
    property var pendingArrivals: []
    function consumeArrival(index) {
        const position = pendingArrivals.indexOf(index)
        if (position < 0) return false
        pendingArrivals.splice(position, 1)
        return true
    }
    Component.onCompleted: initialized = true
    Connections {
        target: stage.historyModel
        function onRowsAboutToBeInserted(parent, first, last) {
            const pending = []
            if (stage.initialized && stage.animateAdds && first >= view.count)
                for (let i = Math.max(first, last - stage.capacity + 1); i <= last; ++i) pending.push(i)
            stage.pendingArrivals = pending
        }
        function onRowsAboutToBeRemoved(parent, first, last) { stage.pendingArrivals = [] }
        function onModelReset() { stage.pendingArrivals = [] }
    }
    readonly property bool motion: hyprcast.options.motion === "full" && hyprcast.options.transition_ms > 0
    readonly property int projectedCount: view.count
    readonly property int capacity: width > 0 && capHeight > 0 ? hyprcast.options.visible_groups : 0
    property int layoutRevision: 0
    // Model slots are uniform and virtualized; visual groups are separately laid
    // out using their fitted widths. At most a tail and one buffer are realized.
    // Only the final few pixels at the left boundary fade; age never
    // changes cap size or opacity. Overflow fitting remains independent.
    readonly property real edgeFadeWidth: Math.max(1, Math.min(12, width * 0.05))
    function offsetAfter(index) {
        let offset = 0
        const revision = layoutRevision
        for (let i = Math.max(index + 1, view.count - stage.capacity - 1); i < view.count; ++i) {
            const row = view.itemAtIndex(i)
            if (!row) continue
            offset += row.displayedWidth + hyprcast.options.group_spacing
        }
        return offset
    }
    clip: true
    ListView {
        id: view
        objectName: "ribbonModelView"
        width: stage.width
        height: stage.capacity * 64
        interactive: false
        model: stage.historyModel
        cacheBuffer: 64
        reuseItems: false
        function followTail() {
            positionViewAtEnd()
            Qt.callLater(function() { stage.layoutRevision++ })
        }
        onCountChanged: followTail()
        onHeightChanged: followTail()
        onContentHeightChanged: followTail()
        Component.onCompleted: followTail()
        delegate: Item {
            id: row
            required property int index
            required property string entryId
            required property string kind
            required property string displayLabel
            required property string displayKey
            required property var displayModifiers
            required property bool counted
            required property int repeatCount
            width: view.width
            height: 64
            readonly property int depth: Math.max(0, view.count - index - 1)
            readonly property real targetScale: group.fitScale
            readonly property real displayedWidth: group.width * targetScale
            readonly property real targetX: stage.width - displayedWidth - stage.offsetAfter(index)
            property bool ready: false
            property real arrival: 1
            NumberAnimation {
                id: entrance
                target: row
                property: "arrival"
                from: 0
                to: 1
                duration: hyprcast.options.transition_ms
                easing.type: Easing.OutCubic
            }
            Connections {
                target: stage
                function onMotionChanged() {
                    if (!stage.motion) { entrance.stop(); row.arrival = 1 }
                }
            }
            onDisplayedWidthChanged: stage.layoutRevision++
            onIndexChanged: stage.layoutRevision++
            Component.onCompleted: {
                if (stage.consumeArrival(index) && stage.motion) entrance.start()
                Qt.callLater(function() { stage.layoutRevision++; row.ready = true })
            }
            Component.onDestruction: stage.layoutRevision++
            RibbonGroup {
                id: group
                parent: stage
                objectName: "ribbonGroup"
                property string projectedId: row.entryId
                property int depth: row.depth
                kind: row.kind
                label: row.displayLabel
                keyLabel: row.displayKey
                modifiers: row.displayModifiers
                counted: row.counted
                repeatCount: row.repeatCount
                availableWidth: stage.width
                capHeight: stage.capHeight
                transformOrigin: Item.BottomLeft
                scale: row.targetScale
                x: row.targetX
                y: stage.height - height
                // Follow the animated position directly so a departing group
                // vanishes at the boundary rather than clipping a moving cap.
                opacity: row.depth >= stage.capacity ? 0 : row.arrival *
                         (row.depth === 0 ? 1 : Math.max(0, Math.min(1, x / stage.edgeFadeWidth)))
                Behavior on x { enabled: row.ready && stage.motion; NumberAnimation { duration: hyprcast.options.transition_ms; easing.type: Easing.OutCubic } }
            }
        }
    }
}
