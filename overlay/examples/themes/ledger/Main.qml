import QtQuick

Item {
    id: root
    objectName: "ledger-" + hyprcast.options.item_spacing
    anchors.fill: parent
    clip: true

    property real fadingOpacity: 1

    Rectangle {
        anchors.fill: parent
        radius: hyprcast.settings.cornerRadius
        color: hyprcast.settings.backgroundColor
        opacity: hyprcast.settings.backgroundOpacity
    }

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: hyprcast.options.item_spacing

        Row {
            id: heldRow
            width: parent.width
            height: visible ? hyprcast.settings.heldKeyHeight : 0
            spacing: hyprcast.settings.heldKeySpacing
            visible: hyprcast.settings.showHeldKeys && hyprcast.heldKeyCount > 0

            Text {
                text: "HELD"
                color: hyprcast.options.accent
                font.family: hyprcast.settings.fontFamily
                font.pixelSize: hyprcast.settings.heldFontSize
                font.weight: hyprcast.settings.fontWeight
                height: heldRow.height
                verticalAlignment: Text.AlignVCenter
            }

            Repeater {
                model: hyprcast.heldKeys

                delegate: Rectangle {
                    required property string modelData
                    width: heldLabel.implicitWidth + 12
                    height: heldRow.height
                    radius: hyprcast.settings.heldKeyRadius
                    color: hyprcast.settings.heldKeyBackground

                    Text {
                        id: heldLabel
                        anchors.centerIn: parent
                        text: modelData
                        color: hyprcast.settings.heldKeyTextColor
                        font.family: hyprcast.settings.fontFamily
                        font.pixelSize: hyprcast.settings.heldFontSize
                    }
                }
            }
        }

        Item {
            id: historyArea
            width: parent.width
            height: Math.max(0, parent.height - heldRow.height - (heldRow.visible ? parent.spacing : 0))

            ListView {
                id: activeHistory
                anchors.fill: parent
                clip: true
                spacing: hyprcast.options.item_spacing
                interactive: false
                model: hyprcast.history
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
                model: hyprcast.expiredHistory
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

    Component {
        id: historyEntry

        Item {
            required property string kind
            required property string label
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
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: label
                color: hyprcast.settings.foregroundColor
                font.family: hyprcast.settings.fontFamily
                font.pixelSize: hyprcast.settings.fontSize * 0.7
                font.weight: kind === "text" ? Font.Normal : Font.DemiBold
                elide: Text.ElideLeft
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
