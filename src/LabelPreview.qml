import QtQuick
import QtQuick.Controls

Item {
    id: stage

    readonly property real labelAspect: backend.labelWidth / Math.max(1, backend.labelHeight)

    // Held while the preview turns, so the new render shows up after the spin.
    property int shownRevision: 0
    property real twist: 0
    property bool spinning: false
    property bool waiting: false
    property int heldRevision: 0
    property string heldFile: ""

    function spinQuarter() {
        if (!backend.hasLabel)
            return
        if (!spinning) {
            spinning = true
            heldRevision = shownRevision
            heldFile = backend.fileName
            twist = 0
            spinAnim.from = 0
            spinAnim.to = 90
            spinAnim.start()
            return
        }
        const target = spinAnim.to + 90
        const from = twist
        spinAnim.stop()
        waiting = false
        spinAnim.from = from
        spinAnim.to = target
        spinAnim.start()
    }

    function finishSpin() {
        waiting = false
        spinning = false
        twist = 0
        shownRevision = backend.revision
    }

    function cancelSpin() {
        spinAnim.stop()
        waiting = false
        spinning = false
        twist = 0
        shownRevision = backend.revision
    }

    function spinSettled() {
        if (!spinning)
            return
        if (backend.revision !== heldRevision)
            finishSpin()
        else
            waiting = true
    }

    Component.onCompleted: shownRevision = backend.revision

    Connections {
        target: backend
        function onChanged() {
            if (!stage.spinning) {
                stage.shownRevision = backend.revision
                return
            }
            if (backend.fileName !== stage.heldFile) {
                stage.cancelSpin()
                return
            }
            if (stage.waiting && backend.revision !== stage.heldRevision)
                stage.finishSpin()
        }
    }

    NumberAnimation {
        id: spinAnim
        target: stage
        property: "twist"
        duration: 300
        easing.type: Easing.InOutQuad
        onFinished: stage.spinSettled()
    }

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
            source: stage.shownRevision > 0 ? "image://label/preview/" + stage.shownRevision : ""
            cache: false
            fillMode: Image.Stretch
            smooth: stage.spinning
            rotation: stage.twist
            transformOrigin: Item.Center
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
