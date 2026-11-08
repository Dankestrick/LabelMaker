import QtQuick
import QtQuick.Controls

Item {
    id: stage

    readonly property real labelAspect: backend.labelWidth / Math.max(1, backend.labelHeight)

    Rectangle {
        id: card
        anchors.centerIn: parent
        color: "white"
        radius: Math.min(4, theme.radius)
        border.width: 1
        border.color: "#3a3a40"
        width: Math.min(stage.width - 24, Math.max(120, (stage.height - 24) * labelAspect))
        height: width / labelAspect

        Image {
            anchors.fill: parent
            source: backend.revision > 0 ? "image://label/preview/" + backend.revision : ""
            cache: false
            fillMode: Image.Stretch
            smooth: false
            opacity: backend.hasLabel ? 1 : 0
            Behavior on opacity {
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.InOutQuad
                }
            }
        }

        Label {
            anchors.centerIn: parent
            width: parent.width - 32
            visible: !backend.hasLabel && backend.error === ""
            text: qsTr("Open a PDF or a picture.")
            color: "#5c5c62"
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
}
