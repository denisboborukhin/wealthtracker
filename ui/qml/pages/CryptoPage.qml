pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app
    readonly property var wallets: app.demoMode ? [] : app.cryptoWallets

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + Theme.pageMargin * 2)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    ScrollBar.vertical: ScrollBar {
        policy: page.contentHeight > page.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: content
        width: page.width - Theme.pageMargin * 2
        x: Theme.pageMargin
        y: Theme.pageMargin
        spacing: 16

        GridLayout {
            Layout.fillWidth: true
            columns: page.width < 600 ? 1 : 2
            columnSpacing: 16
            rowSpacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Криптокошельки")
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Сети EVM · нативные монеты и токены · оценка в USDT")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
            ActionButton {
                Layout.fillWidth: page.width < 600
                text: page.app.cryptoSyncRunning ? qsTr("Обновляем…") : qsTr("Обновить всё")
                primary: true
                enabled: !page.app.demoMode && !page.app.cryptoSyncRunning
                         && page.wallets.length > 0
                onClicked: page.app.refreshCryptoWallets()
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Только просмотр публичных адресов. Адрес отправляется Rabby для получения остатков. Приватный ключ и seed-фраза не нужны. Сети обнаруживаются автоматически. Регистрация и ключ API не нужны.")
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        SectionCard {
            Layout.fillWidth: true
            implicitHeight: walletForm.implicitHeight + 32
            ColumnLayout {
                id: walletForm
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 10
                Text {
                    text: qsTr("Добавить публичный адрес")
                    color: Theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width < 600 ? 1 : 3
                    columnSpacing: 10
                    rowSpacing: 10
                    TextField {
                        id: nameField
                        Layout.fillWidth: page.width < 600
                        Layout.preferredWidth: 180
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("Название кошелька")
                        maximumLength: 80
                        color: Theme.text
                        selectByMouse: true
                        enabled: !page.app.demoMode && !page.app.cryptoSyncRunning
                        Accessible.name: qsTr("Название кошелька")
                        background: Rectangle {
                            color: Theme.surface
                            border.color: nameField.activeFocus ? Theme.primary : Theme.borderStrong
                            radius: 7
                        }
                    }
                    TextField {
                        id: addressField
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 40
                        placeholderText: qsTr("EVM-адрес: 0x…")
                        maximumLength: 42
                        color: Theme.text
                        selectByMouse: true
                        enabled: !page.app.demoMode && !page.app.cryptoSyncRunning
                        Accessible.name: qsTr("Публичный EVM-адрес")
                        background: Rectangle {
                            color: Theme.surface
                            border.color: addressField.activeFocus ? Theme.primary : Theme.borderStrong
                            radius: 7
                        }
                    }
                    ActionButton {
                        Layout.fillWidth: page.width < 600
                        text: qsTr("Добавить")
                        primary: true
                        enabled: !page.app.demoMode && !page.app.cryptoSyncRunning
                                 && nameField.text.trim().length > 0 && addressField.text.trim().length > 0
                        onClicked: {
                            if (page.app.addCryptoWallet(nameField.text.trim(), addressField.text.trim())) {
                                nameField.clear()
                                addressField.clear()
                            }
                        }
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: page.app.demoMode || page.app.cryptoStatus.length > 0
            text: page.app.demoMode ? qsTr("В демо-режиме данные криптокошельков не создаются")
                                   : page.app.cryptoStatus
            textFormat: Text.PlainText
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            visible: !page.app.demoMode && page.wallets.length === 0
            text: qsTr("Добавьте кошелёк, чтобы получать остатки во всех поддерживаемых Rabby сетях EVM")
            color: Theme.textMuted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: page.wallets
            delegate: SectionCard {
                id: walletCard
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: walletContent.implicitHeight + 32

                ColumnLayout {
                    id: walletContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 16
                    spacing: 10

                    GridLayout {
                        Layout.fillWidth: true
                        columns: page.width < 600 ? 1 : 2
                        columnSpacing: 12
                        rowSpacing: 8
                        Text {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: walletCard.modelData.name
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                        }
                        ActionButton {
                            Layout.fillWidth: page.width < 600
                            text: qsTr("Удалить кошелёк")
                            destructive: true
                            enabled: !page.app.cryptoSyncRunning
                            onClicked: page.app.removeCryptoWallet(walletCard.modelData.id)
                        }
                    }
                    TextEdit {
                        Layout.fillWidth: true
                        text: walletCard.modelData.address
                        textFormat: TextEdit.PlainText
                        readOnly: true
                        selectByMouse: true
                        color: Theme.textMuted
                        font.pixelSize: 12
                        wrapMode: TextEdit.WrapAnywhere
                        Accessible.name: qsTr("Публичный адрес кошелька")
                    }
                    Text {
                        Layout.fillWidth: true
                        text: walletCard.modelData.value
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: 24
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAnywhere
                    }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Последнее обновление: %1").arg(walletCard.modelData.lastSync || qsTr("ещё не обновлялся"))
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (walletCard.modelData.status || "").length > 0
                        text: walletCard.modelData.status || ""
                        textFormat: Text.PlainText
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: walletCard.modelData.unpricedCount > 0
                        text: qsTr("Без оценки: %1 активов (нет цены/точности или помечены как подозрительные). Они не включены в итог.")
                              .arg(walletCard.modelData.unpricedCount)
                        color: Theme.warning
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Обнаруженные сети: %1").arg((walletCard.modelData.networks || []).join(", ") || qsTr("ещё не проверены"))
                        color: Theme.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                    ActionButton {
                        id: coverageButton
                        text: qsTr("Поддерживаемые сети (%1)").arg((walletCard.modelData.supportedChains || []).length)
                        visible: (walletCard.modelData.supportedChains || []).length > 0
                        onClicked: coveragePopup.open()
                        Popup {
                            id: coveragePopup
                            parent: coverageButton
                            popupType: Popup.Item
                            y: coverageButton.height + 4
                            width: Math.min(480, page.width - 64)
                            padding: 16
                            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                            contentItem: Text {
                                text: (walletCard.modelData.supportedChains || []).join(", ")
                                textFormat: Text.PlainText
                                color: Theme.text
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                            background: Rectangle { color: Theme.surface; radius: 8; border.color: Theme.border }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: page.width >= 600 && walletCard.modelData.holdings.length > 0
                        spacing: 16
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("Актив")
                            color: Theme.textMuted
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.minimumWidth: 180
                            Layout.maximumWidth: 180
                            text: qsTr("Количество")
                            color: Theme.textMuted
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            Layout.minimumWidth: 160
                            Layout.maximumWidth: 160
                            text: qsTr("Стоимость в USDT")
                            color: Theme.textMuted
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                    Repeater {
                        model: walletCard.modelData.holdings
                        delegate: ColumnLayout {
                            id: holdingRow
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 4
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 1
                                color: Theme.border
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: page.width < 600 ? 1 : 3
                                columnSpacing: 16
                                rowSpacing: 6
                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: holdingRow.modelData.symbol + " · " + holdingRow.modelData.name + "\n" + (holdingRow.modelData.chain || "")
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: 12
                                    wrapMode: Text.WordWrap
                                }
                                Text {
                                    Layout.fillWidth: page.width < 600
                                    Layout.minimumWidth: page.width < 600 ? 0 : 180
                                    Layout.maximumWidth: page.width < 600 ? Infinity : 180
                                    text: (page.width < 600 ? qsTr("Количество: ") : "") + holdingRow.modelData.quantity
                                    textFormat: Text.PlainText
                                    color: Theme.text
                                    font.pixelSize: 12
                                    horizontalAlignment: page.width < 600 ? Text.AlignLeft : Text.AlignRight
                                    wrapMode: Text.WrapAnywhere
                                }
                                Text {
                                    Layout.fillWidth: page.width < 600
                                    Layout.minimumWidth: page.width < 600 ? 0 : 160
                                    Layout.maximumWidth: page.width < 600 ? Infinity : 160
                                    text: (page.width < 600 ? qsTr("Оценка: ") : "") + (holdingRow.modelData.priced ? holdingRow.modelData.value : qsTr("Нет цены"))
                                    textFormat: Text.PlainText
                                    color: holdingRow.modelData.priced ? Theme.text : Theme.textMuted
                                    font.pixelSize: 12
                                    horizontalAlignment: page.width < 600 ? Text.AlignLeft : Text.AlignRight
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                            TextEdit {
                                Layout.fillWidth: true
                                visible: (holdingRow.modelData.contract || "").length > 0
                                text: holdingRow.modelData.contract || ""
                                textFormat: TextEdit.PlainText
                                readOnly: true
                                selectByMouse: true
                                color: Theme.textMuted
                                font.pixelSize: 10
                                wrapMode: TextEdit.WrapAnywhere
                                Accessible.name: qsTr("Адрес контракта токена")
                            }
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: page.wallets.length > 0
            text: qsTr("Охват ограничен сетями и данными Rabby; произвольные пользовательские сети, NFT и DeFi-позиции не учитываются. Удаление кошелька сохраняет историю.")
            color: Theme.textMuted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
