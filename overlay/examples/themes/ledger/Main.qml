import QtQuick

Item {
    id: root
    objectName: "ledger-" + hyprcast.options.item_spacing
    anchors.fill: parent
    clip: true

    property real fadingOpacity: 1
    readonly property bool hasPanelContent: hyprcast.historyCount > 0 || hyprcast.fading
    readonly property bool panelDecorationVisible: hyprcast.settings.panelVisibility === "always" ||
                                                   (hyprcast.settings.panelVisibility === "with-content" && hasPanelContent)
    readonly property real naturalHistoryHeight: Math.max(activeHistory.contentHeight, expiredHistory.contentHeight)
    readonly property real naturalPanelHeight: naturalHistoryHeight + 24 + hyprcast.settings.panelBorderWidth * 2

    Item {
        id: panelFrame
        objectName: "hyprcastLedgerPanelFrame"
        x: hyprcast.settings.anchor.indexOf("right") >= 0 ? parent.width - width :
           (hyprcast.settings.anchor.indexOf("left") >= 0 ? 0 : (parent.width - width) / 2)
        y: hyprcast.settings.anchor.indexOf("bottom") >= 0 ? parent.height - height :
           (hyprcast.settings.anchor.indexOf("top") >= 0 ? 0 : (parent.height - height) / 2)
        width: hyprcast.settings.width
        height: hyprcast.settings.dynamicSize ? Math.min(hyprcast.settings.height, Math.max(hyprcast.settings.minHeight, root.naturalPanelHeight)) : hyprcast.settings.height
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: hyprcast.settings.cornerRadius
            visible: root.panelDecorationVisible
            color: {
                const base = Qt.color(hyprcast.settings.backgroundColor)
                return Qt.rgba(base.r, base.g, base.b, base.a * hyprcast.settings.backgroundOpacity)
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: hyprcast.settings.cornerRadius
            color: "transparent"
            visible: root.panelDecorationVisible && hyprcast.settings.panelBorderWidth > 0
            border.width: hyprcast.settings.panelBorderWidth
            border.color: hyprcast.settings.panelBorderColor
        }

        Column {
            anchors.fill: parent
            anchors.margins: 12 + hyprcast.settings.panelBorderWidth
            Item {
                id: historyArea
                width: parent.width
                height: parent.height

                ListView {
                    id: activeHistory
                    anchors.fill: parent
                    clip: true
                    spacing: hyprcast.options.item_spacing
                    interactive: false
                    model: hyprcast.displayHistory
                    delegate: historyEntry
                    function followTail() {
                        if (count > 0)
                            positionViewAtEnd()
                    }
                    onCountChanged: Qt.callLater(followTail)
                    Component.onCompleted: Qt.callLater(followTail)
                }

                ListView {
                    id: expiredHistory
                    anchors.fill: parent
                    clip: true
                    spacing: hyprcast.options.item_spacing
                    interactive: false
                    model: hyprcast.expiredDisplayHistory
                    delegate: historyEntry
                    visible: hyprcast.fading
                    opacity: root.fadingOpacity
                    function followTail() {
                        if (count > 0)
                            positionViewAtEnd()
                    }
                    onCountChanged: Qt.callLater(followTail)
                    Component.onCompleted: Qt.callLater(followTail)
                }
            }
        }
    }

    Component {
        id: historyEntry

        Item {
            required property string kind
            required property string displayLabel
            required property bool counted
            required property int repeatCount
            width: ListView.view.width
            height: Math.max(20, hyprcast.settings.fontSize * 0.7)

            Rectangle {
                width: 3
                height: parent.height - 4
                anchors.verticalCenter: parent.verticalCenter
                radius: 1.5
                color: hyprcast.options.accent
                opacity: kind === "text" ? 0.55 : 1
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: countLabel.left
                anchors.rightMargin: counted ? 8 : 0
                anchors.verticalCenter: parent.verticalCenter
                text: displayLabel
                color: hyprcast.settings.foregroundColor
                font.family: kind === "text" || hyprcast.settings.symbolFontFamily.length === 0 ?
                             hyprcast.settings.fontFamily : hyprcast.settings.symbolFontFamily
                font.pixelSize: hyprcast.settings.fontSize * 0.7
                font.weight: kind === "text" ? Font.Normal : Font.DemiBold
                elide: Text.ElideLeft
            }

            Text {
                id: countLabel
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: counted
                text: "x" + repeatCount
                color: hyprcast.settings.foregroundColor
                font.family: hyprcast.settings.fontFamily
                font.pixelSize: hyprcast.settings.fontSize * 0.55
                font.weight: Font.DemiBold
            }
        }
    }

    NumberAnimation {
        id: fadeAnimation
        target: root
        property: "fadingOpacity"
        from: 1
        to: 0
        duration: hyprcast.fadeDurationMs
        easing.type: Easing.OutCubic
    }

    Connections {
        target: hyprcast

        function onFadingChanged() {
            fadeAnimation.stop()
            root.fadingOpacity = 1
            if (hyprcast.fading)
                fadeAnimation.start()
        }

        function onFadeDurationMsChanged() {
            if (hyprcast.fading) {
                fadeAnimation.stop()
                root.fadingOpacity = 1
                fadeAnimation.start()
            }
        }
    }

    Component.onCompleted: {
        if (hyprcast.fading)
            fadeAnimation.start()
    }
}
