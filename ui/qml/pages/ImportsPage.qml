import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"

Item {
    FileDialog {
        id: statementDialog
        title: qsTr("Выберите банковскую выписку")
        nameFilters: [
            qsTr("Выписки (*.csv *.xlsx *.ofx *.qif *.txt *.pdf)"),
            qsTr("Все файлы (*)")
        ]
        onAccepted: App.previewImport(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Text {
            text: qsTr("Импорт")
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 230
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 48, 620)
                spacing: 14
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Добавьте выписку")
                    color: Theme.text
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Файл обрабатывается только на этом компьютере. Перед импортом вы увидите операции и предупреждения.")
                    color: Theme.textMuted
                    font.pixelSize: 13
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Выбрать файл")
                    onClicked: statementDialog.open()
                }
            }
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 90
            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                Text {
                    Layout.fillWidth: true
                    text: App.importStatus
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                Rectangle {
                    implicitWidth: formatText.implicitWidth + 20
                    implicitHeight: 30
                    radius: 15
                    color: Theme.surfaceRaised
                    Text {
                        id: formatText
                        anchors.centerIn: parent
                        text: "CSV · XLSX · OFX · QIF · PDF"
                        color: Theme.primary
                        font.pixelSize: 11
                    }
                }
            }
        }
        Item { Layout.fillHeight: true }
    }
}
