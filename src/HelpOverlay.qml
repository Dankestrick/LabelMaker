import QtQuick
import QtQuick.Controls

Rectangle {
    id: overlay
    property bool shown: false
    signal dismissed()

    color: "#e60e0e10"
    opacity: shown ? 1 : 0
    visible: shown || opacity > 0
    Behavior on opacity {
        NumberAnimation {
            duration: 300
            easing.type: Easing.InOutQuad
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.dismissed()
    }

    Column {
        anchors.centerIn: parent
        spacing: 10

        Label {
            text: qsTr("Shortcuts")
            color: theme.accent
            font.pixelSize: 22
        }

        Row {
            spacing: 18
            Label { text: "O"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Open a file"); color: "#d8d8dc" }
        }
        Row {
            spacing: 18
            Label { text: "Space"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Print the label"); color: "#d8d8dc" }
        }
        Row {
            spacing: 18
            Label { text: "?"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Show shortcuts"); color: "#d8d8dc" }
        }
        Row {
            spacing: 18
            Label { text: "Ctrl+Z"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Undo"); color: "#d8d8dc" }
        }
        Row {
            spacing: 18
            Label { text: "Ctrl+Shift+Z"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Redo"); color: "#d8d8dc" }
        }
        Row {
            spacing: 18
            Label { text: "Q"; color: "#ffffff"; width: 120 }
            Label { text: qsTr("Quit"); color: "#d8d8dc" }
        }
    }
}
