import QtQuick

Item {
    id: stage
    property var historyModel
    property bool animateAdds: true
    readonly property bool motion: hyprcast.options.motion === "full" && hyprcast.options.transition_ms > 0
    readonly property int duration: motion ? hyprcast.options.transition_ms : 0
    readonly property real capHeight: hyprcast.options.height
    readonly property real slot: capHeight + hyprcast.options.row_spacing
    function depthScale(depth) {
        return Math.max(0.65, Math.pow(hyprcast.options.depth_scale, Math.max(0, depth - 1)))
    }
    function bottomOffset(depth) {
        let offset = 0
        for (let i = 0; i < depth; ++i)
            offset += capHeight * depthScale(i) + hyprcast.options.row_spacing
        return offset
    }
    readonly property int capacity: {
        let n = 0
        for (let i = 0; i < hyprcast.options.visible_groups; ++i) {
            if (bottomOffset(i) + capHeight * depthScale(i) > height || width <= 0)
                break
            ++n
        }
        return n
    }
    readonly property int projectedCount: view.count
    property bool initialized: false
    property var pendingArrivals: []
    property var outgoingRow: null
    function consumeArrival(index) {
        const position = pendingArrivals.indexOf(index)
        if (position < 0) return false
        pendingArrivals.splice(position, 1)
        return true
    }
    function holdRemoval(row) {
        if (outgoingRow) outgoingRow.finishRemoval()
        outgoingRow = row
        row.beginRemoval()
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
    clip: true

    // The view virtualizes model delegates in uniform slots. Their visual children
    // are reparented into the stage, where depth geometry is independent of scroll
    // coordinates. No model reads, copied history, or private C++ API are needed.
    ListView {
        id: view
        objectName: "cascadeModelView"
        width: stage.width
        height: stage.capacity * stage.slot
        visible: stage.capacity > 0
        interactive: false
        model: stage.historyModel
        cacheBuffer: stage.slot
        reuseItems: false
        function followTail() { positionViewAtEnd() }
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
            height: stage.slot
            property real arrival: 1
            property real departure: 1
            property bool ready: false
            readonly property int depth: Math.max(0, view.count - index - 1)
            readonly property bool inStack: index >= 0 && depth < stage.capacity
            property bool removing: false
            property real removalY: 0
            property real removalScale: 1
            property real removalOpacity: 1
            function beginRemoval() {
                removalY = group.y
                removalScale = group.scale
                removalOpacity = group.opacity
                removing = true
                ListView.delayRemove = true
                exitAnimation.start()
            }
            function finishRemoval() {
                exitAnimation.stop()
                ListView.delayRemove = false
                if (stage.outgoingRow === row) stage.outgoingRow = null
            }
            ListView.onRemove: {
                if (stage.motion && stage.animateAdds && group.opacity > 0)
                    stage.holdRemoval(row)
            }
            NumberAnimation {
                id: entranceAnimation
                target: row
                property: "arrival"
                from: 0
                to: 1
                duration: stage.duration
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                id: exitAnimation
                target: row
                property: "departure"
                to: 0
                duration: stage.duration * 0.67
                onFinished: row.finishRemoval()
            }
            Connections {
                target: stage
                function onMotionChanged() {
                    if (!stage.motion) {
                        entranceAnimation.stop()
                        row.arrival = 1
                        row.finishRemoval()
                    }
                }
            }
            Component.onCompleted: {
                if (stage.consumeArrival(index) && stage.motion) entranceAnimation.start()
                Qt.callLater(function() { row.ready = true })
            }

            CascadeGroup {
                id: group
                parent: stage
                objectName: "cascadeGroup"
                property string projectedId: row.entryId
                property int depth: row.depth
                property bool inStack: row.inStack
                kind: row.kind
                label: row.displayLabel
                keyLabel: row.displayKey
                modifiers: row.displayModifiers
                counted: row.counted
                repeatCount: row.repeatCount
                availableWidth: stage.width
                readonly property real depthFactor: stage.depthScale(row.depth)
                scale: row.removing ? row.removalScale : depthFactor * fitScale
                transformOrigin: hyprcast.options.group_alignment === "right" ? Item.BottomRight :
                                 (hyprcast.options.group_alignment === "left" ? Item.BottomLeft : Item.Bottom)
                x: hyprcast.options.group_alignment === "right" ? stage.width - width :
                   (hyprcast.options.group_alignment === "left" ? 0 : (stage.width - width) / 2)
                y: row.removing ? row.removalY : stage.height - height - stage.bottomOffset(Math.min(row.depth, stage.capacity + 1)) + (1 - row.arrival) * (stage.motion ? 16 : 0)
                opacity: row.removing ? row.removalOpacity * row.departure :
                         (row.inStack ? row.arrival * Math.pow(hyprcast.options.depth_opacity, Math.max(0, row.depth - 1)) : 0)
                Behavior on y { enabled: row.ready && stage.motion && row.arrival === 1; NumberAnimation { duration: stage.duration * 0.89; easing.type: Easing.OutCubic } }
                Behavior on scale { enabled: row.ready && stage.motion; NumberAnimation { duration: stage.duration * 0.89; easing.type: Easing.OutCubic } }
                Behavior on opacity { enabled: row.ready && stage.motion && row.arrival === 1 && !row.removing; NumberAnimation { duration: stage.duration * 0.67 } }
            }
        }
    }
}
