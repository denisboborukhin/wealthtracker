import QtQuick
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
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text { text: qsTr("Импорт"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { text: qsTr("Добавление операций из банковской выписки"); color: Theme.textMuted; font.pixelSize: 12 }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 190

            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 28

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text {
                        text: qsTr("Выберите файл выписки")
                        color: Theme.text
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Сначала приложение покажет найденные счета, операции и предупреждения. Файл не покидает компьютер.")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        lineHeight: 1.3
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: "CSV · XLSX · OFX · QIF · 1C · PDF"
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                ActionButton {
                    text: qsTr("Выбрать файл")
                    primary: true
                    onClicked: statementDialog.open()
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 88
            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: qsTr("Состояние импорта"); color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                    Text { Layout.fillWidth: true; text: App.importStatus; color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
