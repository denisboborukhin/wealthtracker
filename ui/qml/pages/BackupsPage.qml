import QtQuick
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text { text: qsTr("Резервные копии"); color: Theme.text; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { text: qsTr("Локальные зашифрованные архивы данных"); color: Theme.textMuted; font.pixelSize: 12 }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            RowLayout {
                anchors.fill: parent
                anchors.margins: 22
                spacing: 24
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Text { text: qsTr("Зашифрованная резервная копия"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Архив защищается отдельной парольной фразой и содержит проверяемый манифест версии.")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                ColumnLayout {
                    spacing: 8
                    ActionButton { Layout.fillWidth: true; text: qsTr("Создать копию"); primary: true }
                    ActionButton { Layout.fillWidth: true; text: qsTr("Восстановить") }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            radius: Theme.radius
            color: Theme.warningMuted
            Text {
                anchors.fill: parent
                anchors.margins: 16
                text: qsTr("CSV-экспорт не шифруется. Перед сохранением приложение покажет отдельное предупреждение.")
                color: Theme.warning
                font.pixelSize: 11
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.WordWrap
            }
        }

        Item { Layout.fillHeight: true }
    }
}
