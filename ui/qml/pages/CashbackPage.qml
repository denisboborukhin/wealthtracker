pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app
    readonly property var banks: app.cashbackBanks
    readonly property var months: app.cashbackMonths
    readonly property var bankIds: ["tbank", "alfa", "vtb", "ozon"]
    readonly property bool editable: !app.locked && !app.demoMode
    property bool manualEditorOpen: false

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + Theme.pageMargin * 2)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    ScrollBar.vertical: ScrollBar {}

    Connections {
        target: page.app
        function onDemoModeChanged() {
            if (page.app.demoMode) {
                categoryField.clear()
                percentField.clear()
            }
        }
    }

    ColumnLayout {
        id: content
        x: Theme.pageMargin
        y: Theme.pageMargin
        width: page.width - Theme.pageMargin * 2
        spacing: 16

        GridLayout {
            Layout.fillWidth: true
            columns: page.width > 600 ? 3 : 1
            ColumnLayout {
                Layout.fillWidth: true
                Layout.columnSpan: 1
                Text {
                    text: qsTr("Кэшбэк")
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Выбранные категории · этот и следующий месяц")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
            ActionButton {
                Layout.fillWidth: page.width < 600
                text: page.manualEditorOpen ? qsTr("Скрыть форму") : qsTr("Записать вручную")
                enabled: page.editable
                onClicked: page.manualEditorOpen = !page.manualEditorOpen
            }
            ActionButton {
                Layout.fillWidth: page.width < 600
                text: qsTr("Обновить банки")
                enabled: page.editable && page.banks.some(bank => bank.connected && !bank.syncRunning && !bank.cashbackRunning)
                onClicked: page.app.refreshCashbackBanks()
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Выбранные категории запрашиваются вместе с обновлением Т-Банка, Альфа-Банка, ВТБ и Ozon Банка. Источник и время обновления указаны отдельно для каждого месяца. Если банк не передал категории, доступны ручные записи.")
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        SectionCard {
            visible: page.manualEditorOpen
            Layout.fillWidth: true
            implicitHeight: editor.implicitHeight + 32
            ColumnLayout {
                id: editor
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 10

                Text {
                    text: qsTr("Записать выбранную категорию")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width > 500 ? 2 : 1
                    rowSpacing: 10
                    columnSpacing: 10
                    MinimalComboBox {
                        id: bankField
                        objectName: "cashbackBank"
                        Layout.fillWidth: true
                        model: [qsTr("Т-Банк"), qsTr("Альфа-Банк"), qsTr("ВТБ"), qsTr("Ozon Банк")]
                        enabled: page.editable
                        Accessible.name: qsTr("Банк для кэшбэка")
                    }
                    MinimalComboBox {
                        id: monthField
                        objectName: "cashbackMonth"
                        property string selectedMonth: ""
                        Layout.fillWidth: true
                        model: page.months.map(month => month.label)
                        onActivated: selectedMonth = page.months[currentIndex].id
                        onModelChanged: {
                            const selected = page.months.findIndex(month => month.id === selectedMonth)
                            currentIndex = Math.max(0, selected)
                            selectedMonth = page.months[currentIndex].id
                        }
                        enabled: page.editable
                        Accessible.name: qsTr("Месяц кэшбэка")
                    }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width > 500 ? 3 : 1
                    rowSpacing: 10
                    columnSpacing: 10
                    TextField {
                        id: categoryField
                        objectName: "cashbackCategory"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("Категория, например Супермаркеты")
                        maximumLength: 100
                        enabled: page.editable
                        selectByMouse: true
                        color: Theme.text
                        Accessible.name: qsTr("Категория кэшбэка")
                        background: Rectangle {
                            radius: 7
                            color: Theme.surface
                            border.color: categoryField.activeFocus ? Theme.primary : Theme.borderStrong
                        }
                    }
                    TextField {
                        id: percentField
                        objectName: "cashbackPercent"
                        Layout.preferredWidth: 105
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("Процент")
                        maximumLength: 6
                        enabled: page.editable
                        selectByMouse: true
                        color: Theme.text
                        Accessible.name: qsTr("Процент кэшбэка")
                        background: Rectangle {
                            radius: 7
                            color: Theme.surface
                            border.color: percentField.activeFocus ? Theme.primary : Theme.borderStrong
                        }
                    }
                    ActionButton {
                        text: qsTr("Сохранить")
                        primary: true
                        enabled: page.editable && bankField.currentIndex >= 0 && monthField.currentIndex >= 0
                                 && categoryField.text.trim().length > 0 && percentField.text.trim().length > 0
                        onClicked: {
                            if (page.app.saveCashbackCategory(page.bankIds[bankField.currentIndex],
                                    page.months[monthField.currentIndex].id, categoryField.text, percentField.text)) {
                                categoryField.clear()
                                percentField.clear()
                            }
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Ручные записи — резервный вариант. Полученные из банка категории имеют приоритет. Та же категория в том же банке и месяце обновляет ручную запись.")
                    color: Theme.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: page.app.demoMode || page.app.cashbackError.length > 0
            text: page.app.demoMode ? qsTr("Личные категории скрыты в демо-режиме") : page.app.cashbackError
            textFormat: Text.PlainText
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: page.banks
            delegate: SectionCard {
                id: bankCard
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: bankContent.implicitHeight + 32

                ColumnLayout {
                    id: bankContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 12
                    Text {
                        text: bankCard.modelData.name
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: !bankCard.modelData.connected ? qsTr("Банк не подключён · записи доступны без входа")
                            : bankCard.modelData.syncRunning ? qsTr("Обновляем остатки банка…")
                            : qsTr("Остатки банка обновлены: %1").arg(bankCard.modelData.lastSync || qsTr("нет данных"))
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: bankCard.modelData.cashbackStatus
                            + (bankCard.modelData.cashbackLastSync ? " · " + bankCard.modelData.cashbackLastSync : "")
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: bankCard.modelData.connected && bankCard.modelData.syncStatus.length > 0
                        text: bankCard.modelData.syncStatus
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.width > 760 ? 2 : 1
                        columnSpacing: 24
                        rowSpacing: 16
                        Repeater {
                            model: page.months
                            delegate: ColumnLayout {
                                id: monthColumn
                                required property var modelData
                                required property int index
                                readonly property var rows: index === 0 ? bankCard.modelData.current : bankCard.modelData.next
                                readonly property bool fromBank: index === 0 ? bankCard.modelData.currentFromBank : bankCard.modelData.nextFromBank
                                readonly property bool selectionRequired: index === 0 ? bankCard.modelData.currentSelectionRequired : bankCard.modelData.nextSelectionRequired
                                readonly property string lastSync: (index === 0 ? bankCard.modelData.currentLastSync : bankCard.modelData.nextLastSync) || ""
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: 8
                                Text {
                                    text: monthColumn.modelData.label
                                    color: Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.Medium
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: monthColumn.fromBank ? qsTr("Из банка · %1").arg(monthColumn.lastSync) : qsTr("Вручную · не подтверждено банком")
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: true
                                    visible: monthColumn.rows.length === 0
                                    text: monthColumn.selectionRequired ? qsTr("В банке доступен выбор категорий. Подтверждённый выбор не получен.")
                                        : monthColumn.fromBank ? qsTr("Банк не передал выбранных категорий")
                                        : qsTr("Категории не записаны")
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Repeater {
                                    model: monthColumn.rows
                                    delegate: RowLayout {
                                        id: categoryRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: categoryRow.modelData.name
                                            textFormat: Text.PlainText
                                            color: Theme.text
                                            font.pixelSize: 12
                                            wrapMode: Text.WrapAnywhere
                                        }
                                        Text {
                                            text: categoryRow.modelData.percent
                                            color: Theme.primary
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }
                                        ActionButton {
                                            text: qsTr("Удалить")
                                            visible: !monthColumn.fromBank
                                            enabled: page.editable
                                            Accessible.name: qsTr("Удалить категорию %1").arg(categoryRow.modelData.name)
                                            onClicked: page.app.removeCashbackCategory(bankCard.modelData.id,
                                                monthColumn.modelData.id, categoryRow.modelData.name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
