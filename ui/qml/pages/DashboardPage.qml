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
                    text: qsTr("Все счета · июль 2026")
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
                hint: qsTr("+4,2% за месяц")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Доходы")
                value: App.monthlyIncome
                hint: qsTr("+12,5% к июню")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Расходы")
                value: App.monthlySpending
                hint: qsTr("−6,8% к июню")
                accent: Theme.textMuted
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Сбережения")
                value: App.savingsRate
                hint: qsTr("Цель 40%")
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
                        values: App.netWorthSeries
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

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
