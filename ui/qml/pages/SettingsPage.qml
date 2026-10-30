pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app

    contentWidth: width
    contentHeight: content.implicitHeight + Theme.pageMargin * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: content
        x: Theme.pageMargin
        y: Theme.pageMargin
        width: page.width - Theme.pageMargin * 2
        spacing: 20

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: qsTr("Настройки")
                color: Theme.text
                font.pixelSize: 26
                font.weight: Font.DemiBold
            }
            Text {
                text: qsTr("Локальное поведение приложения")
                color: Theme.textMuted
                font.pixelSize: 12
            }
        }

        SectionCard {
            Layout.fillWidth: true
            implicitHeight: Math.max(142, demoContent.implicitHeight + 40)

            ColumnLayout {
                id: demoContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 20

                Text {
                    text: qsTr("Данные для знакомства")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }

                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: qsTr("Демо-режим"); color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                        Text { Layout.fillWidth: true; text: qsTr("Показывать пример данных вместо пустого состояния"); color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.WordWrap }
                    }
                    MinimalSwitch {
                        accessibleName: qsTr("Демо-режим")
                        checked: page.app.demoMode
                        onToggled: checked ? page.app.loadDemoData() : page.app.clearDemoData()
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            implicitHeight: Math.max(142, privacyContent.implicitHeight + 40)
            GridLayout {
                id: privacyContent
                columns: page.width < 600 ? 1 : 2
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                columnSpacing: 20
                rowSpacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 5
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Экран приватности")
                        color: Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Скрывает суммы и операции в окне и останавливает текущие входы и синхронизации.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Для возврата пароль не нужен. Данные не удаляются — это не блокировка приложения.")
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
                ActionButton {
                    Layout.fillWidth: page.width < 600
                    text: qsTr("Скрыть данные")
                    onClicked: page.app.lock()
                }
            }
        }

    }
}
