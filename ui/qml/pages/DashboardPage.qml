import QtQuick
import QtQuick.Layouts
import "../components"

Flickable {
    contentHeight: content.implicitHeight + Theme.pageMargin * 2
    clip: true

    ColumnLayout {
        id: content
        width: parent.width - Theme.pageMargin * 2
        x: Theme.pageMargin
        y: Theme.pageMargin
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                spacing: 4
                Text {
                    text: qsTr("Обзор")
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                }
                Text {
                    text: App.demoMode
                          ? qsTr("Пример данных · июль 2026")
                          : App.hasFinancialData
                            ? qsTr("Все подключённые счета")
                            : qsTr("Подключите банк или импортируйте выписку")
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
            }
            Item { Layout.fillWidth: true }
            StatusBadge {
                visible: App.demoMode
                text: qsTr("Демо-данные")
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 900 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 12

            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Капитал")
                value: App.netWorth
                hint: App.demoMode ? qsTr("+4,2% за месяц")
                                   : qsTr("Текущий остаток")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Доходы")
                value: App.monthlyIncome
                hint: App.demoMode ? qsTr("+12,5% к июню")
                                   : qsTr("Текущий месяц")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Расходы")
                value: App.monthlySpending
                hint: App.demoMode ? qsTr("−6,8% к июню")
                                   : qsTr("Текущий месяц")
                accent: Theme.textMuted
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Сбережения")
                value: App.savingsRate
                hint: App.demoMode ? qsTr("Цель 40%")
                                   : qsTr("Доходы минус расходы")
                accent: Theme.warning
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 860 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12

            SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 286

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 4

                    Text {
                        text: qsTr("Капитал")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Последние 12 месяцев")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                    LineChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 10
                        visible: App.netWorthSeries.length > 1
                        values: App.netWorthSeries
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: App.netWorthSeries.length <= 1
                        text: qsTr("История капитала появится после нескольких снимков баланса")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 286

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    Text {
                        text: qsTr("Расходы по категориям")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    Repeater {
                        id: categoryRepeater
                        model: App.categories
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    text: model.name
                                    color: Theme.text
                                    font.pixelSize: 12
                                }
                                Text {
                                    text: model.amount
                                    color: Theme.textMuted
                                    font.pixelSize: 12
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 4
                                radius: 2
                                color: Theme.surfaceRaised
                                Rectangle {
                                    width: parent.width * model.share
                                    height: parent.height
                                    radius: 2
                                    color: model.color
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: categoryRepeater.count === 0
                        text: qsTr("Категории появятся после загрузки операций")
                        color: Theme.textMuted
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 320

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    Layout.preferredHeight: 48
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Последние операции")
                        color: Theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Сумма")
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Theme.border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Text {
                        anchors.centerIn: parent
                        visible: transactionList.count === 0
                        text: qsTr("Операций пока нет")
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }

                    ListView {
                        id: transactionList
                        anchors.fill: parent
                        model: App.transactions
                        clip: true

                        delegate: Item {
                            width: ListView.view.width
                            height: 44

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 18
                                anchors.rightMargin: 18
                                Text {
                                    Layout.preferredWidth: 88
                                    text: model.date
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: model.title
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.preferredWidth: 160
                                    text: model.category
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.preferredWidth: 105
                                    horizontalAlignment: Text.AlignRight
                                    text: model.amount
                                    color: model.income ? Theme.positive : Theme.text
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: 1
                                anchors.bottom: parent.bottom
                                color: Theme.border
                            }
                        }
                    }
                }
            }
        }
    }
}
