import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 18
        Text {
            text: qsTr("Резервные копии")
            color: Theme.text
            font.pixelSize: 30
            font.weight: Font.DemiBold
        }
        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 190
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                Text { text: qsTr("Зашифрованная резервная копия"); color: Theme.text; font.pixelSize: 18 }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Архив защищается отдельной парольной фразой и содержит проверяемый манифест версии.")
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
                RowLayout {
                    Button { text: qsTr("Создать копию") }
                    Button { text: qsTr("Восстановить") }
                }
            }
        }
        Text {
            text: qsTr("CSV-экспорт не шифруется. Приложение покажет предупреждение перед сохранением.")
            color: Theme.warning
            wrapMode: Text.WordWrap
        }
        Item { Layout.fillHeight: true }
    }
}
