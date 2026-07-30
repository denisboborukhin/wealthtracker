import QtQuick
import QtQuick.Layouts
import "../components"

Flickable {
    contentHeight: content.implicitHeight + 48
    clip: true

    ColumnLayout {
        id: content
        width: parent.width - 48
        x: 24
        y: 24
        spacing: 18

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("Обзор")
                color: Theme.text
                font.pixelSize: 30
                font.weight: Font.DemiBold
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: App.demoMode
                implicitWidth: demoLabel.implicitWidth + 24
                implicitHeight: 30
                radius: 15
                color: "#263B5A"
                Text {
                    id: demoLabel
                    anchors.centerIn: parent
                    text: qsTr("Демо-данные")
                    color: Theme.primary
                    font.pixelSize: 12
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 1040 ? 4 : 2
            columnSpacing: 14
            rowSpacing: 14

            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Капитал")
                value: App.netWorth
                hint: qsTr("+4,2% за месяц")
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Доходы в июле")
                value: App.monthlyIncome
                hint: qsTr("+12,5% к июню")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Расходы в июле")
                value: App.monthlySpending
                hint: qsTr("−6,8% к июню")
                accent: Theme.negative
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Норма сбережений")
                value: App.savingsRate
                hint: qsTr("Цель 40%")
                accent: Theme.warning
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: width > 920 ? 2 : 1
            columnSpacing: 18
            rowSpacing: 18

            SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 330
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    Text {
                        text: qsTr("Динамика капитала")
                        color: Theme.text
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: qsTr("Последние 12 месяцев")
                        color: Theme.textMuted
                        font.pixelSize: 12
                    }
                    LineChart {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        values: App.netWorthSeries
                    }
                }
            }

            SectionCard {
                Layout.fillWidth: true
                Layout.preferredHeight: 330
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 18
                    ColumnLayout {
                        Layout.fillHeight: true
                        Layout.preferredWidth: 210
                        Text {
                            text: qsTr("Расходы по категориям")
                            color: Theme.text
                            font.pixelSize: 18
                            font.weight: Font.DemiBold
                        }
                        DonutChart {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            categoryModel: App.categories
                        }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: App.categories
                        spacing: 10
                        delegate: RowLayout {
                            width: ListView.view.width
                            Rectangle {
                                implicitWidth: 9
                                implicitHeight: 9
                                radius: 5
                                color: model.color
                            }
                            Text {
                                Layout.fillWidth: true
                                text: model.name
                                color: Theme.textMuted
                                font.pixelSize: 13
                            }
                            Text {
                                text: model.amount
                                color: Theme.text
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }

        SectionCard {
            Layout.fillWidth: true
            Layout.preferredHeight: 350
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 22
                Text {
                    text: qsTr("Последние операции")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: App.transactions
                    clip: true
                    delegate: Item {
                        width: ListView.view.width
                        height: 47
                        RowLayout {
                            anchors.fill: parent
                            Text {
                                Layout.preferredWidth: 100
                                text: model.date
                                color: Theme.textMuted
                                font.pixelSize: 12
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 1
                                Text { text: model.title; color: Theme.text; font.pixelSize: 14 }
                                Text { text: model.category; color: Theme.textMuted; font.pixelSize: 11 }
                            }
                            Text {
                                text: model.amount
                                color: model.income ? Theme.positive : Theme.text
                                font.pixelSize: 14
                                font.weight: Font.Medium
                            }
                        }
                    }
                }
            }
        }
    }
}
