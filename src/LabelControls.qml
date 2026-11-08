import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ColumnLayout {
    spacing: 10

    // Read so a language switch rewrites the numbers.
    readonly property string language: translator.language

    GridLayout {
        columns: 3
        columnSpacing: 16
        rowSpacing: 8
        Layout.fillWidth: true

        Label {
            text: qsTr("Label size")
            color: "#c8c8cc"
        }

        ComboBox {
            Layout.fillWidth: true
            Material.roundedScale: theme.radius
            model: [
                qsTr("4 × 6 in"),
                qsTr("4 × 4 in"),
                qsTr("4 × 3 in"),
                qsTr("4 × 2 in"),
                qsTr("3 × 2 in"),
                qsTr("2 × 1 in"),
                qsTr("50 × 30 mm"),
                qsTr("Custom")
            ]
            currentIndex: backend.sizeIndex
            onActivated: (index) => backend.sizeIndex = index
        }

        Label {
            text: qsTr("%1 × %2 dots").arg(backend.labelWidth).arg(backend.labelHeight)
            color: "#9a9aa0"
        }

        Label {
            text: qsTr("Fit")
            color: "#c8c8cc"
        }

        ComboBox {
            Layout.fillWidth: true
            Material.roundedScale: theme.radius
            model: [qsTr("Shipping label"), qsTr("Fit whole page"), qsTr("Fill label")]
            currentIndex: backend.fit
            onActivated: (index) => backend.fit = index
        }

        Item { width: 1; height: 1 }

        Label {
            text: qsTr("Darkness")
            color: "#c8c8cc"
        }

        Slider {
            Layout.fillWidth: true
            from: 1
            to: 15
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: backend.darkness
            onMoved: backend.darkness = Math.round(value)
        }

        Label {
            text: backend.darkness.toLocaleString(Qt.locale(language), "f", 0)
            color: "#f2f2f4"
        }

        Label {
            text: qsTr("Photo mode")
            color: "#c8c8cc"
        }

        Switch {
            checked: backend.photo
            Material.roundedScale: theme.radius
            onToggled: {
                if (checked !== backend.photo)
                    backend.photo = checked
            }
        }

        Label {
            text: backend.photo ? qsTr("Dithered") : qsTr("Crisp")
            color: "#9a9aa0"
        }

        Label {
            text: qsTr("Copies")
            color: "#c8c8cc"
        }

        SpinBox {
            from: 1
            to: 99
            value: backend.copies
            editable: true
            Material.roundedScale: theme.radius
            onValueModified: backend.copies = value
        }

        Item { width: 1; height: 1 }
    }

    RowLayout {
        visible: backend.sizeIndex === 7
        spacing: 12

        Label { text: qsTr("Width"); color: "#c8c8cc" }
        SpinBox {
            from: 10
            to: 120
            value: Math.round(backend.customWidthMm)
            editable: true
            Material.roundedScale: theme.radius
            onValueModified: backend.customWidthMm = value
        }
        Label { text: qsTr("mm"); color: "#9a9aa0" }

        Label { text: qsTr("Height"); color: "#c8c8cc" }
        SpinBox {
            from: 10
            to: 400
            value: Math.round(backend.customHeightMm)
            editable: true
            Material.roundedScale: theme.radius
            onValueModified: backend.customHeightMm = value
        }
        Label { text: qsTr("mm"); color: "#9a9aa0" }
    }

    RowLayout {
        visible: backend.pageCount > 1
        spacing: 12

        Label { text: qsTr("Page"); color: "#c8c8cc" }
        SpinBox {
            from: 1
            to: Math.max(1, backend.pageCount)
            value: backend.page
            editable: true
            Material.roundedScale: theme.radius
            onValueModified: backend.page = value
        }
        Label {
            text: qsTr("of %1").arg(backend.pageCount)
            color: "#9a9aa0"
        }
    }
}
