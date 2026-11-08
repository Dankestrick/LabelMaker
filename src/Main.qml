import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1080
    height: 780
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: "Label Maker"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; onActivated: backend.openFile() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.print() }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; context: Qt.ApplicationShortcut; onActivated: backend.redo() }

    // Right-to-left languages mirror the window. The Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Button {
                    text: qsTr("Open")
                    Material.roundedScale: theme.radius
                    onClicked: backend.openFile()
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: backend.fileName
                        color: "#f2f2f4"
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                    }

                    Label {
                        visible: backend.error !== "" || backend.status !== ""
                        text: backend.error !== "" ? backend.error : backend.status
                        color: backend.error !== "" ? "#ffb4a2" : "#9a9aa0"
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }

                    ProgressBar {
                        visible: backend.printing
                        value: backend.printProgress
                        Layout.fillWidth: true
                        Behavior on value { NumberAnimation { duration: 300 } }
                    }
                }

                Button {
                    text: backend.printing ? qsTr("Printing...") : qsTr("Print")
                    enabled: backend.canPrint
                    onClicked: backend.print()
                    Material.roundedScale: theme.radius
                }
            }

            LabelPreview {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            Label {
                visible: backend.blank
                text: qsTr("Nothing dark to print on this label.")
                color: "#9a9aa0"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            LabelControls {
                Layout.fillWidth: true
            }
        }

        HelpOverlay {
            anchors.fill: parent
            shown: win.helpVisible
            onDismissed: win.helpVisible = false
        }
    }
}
