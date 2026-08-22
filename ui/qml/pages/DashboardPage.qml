pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Flickable {
    id: page
    required property var app
    readonly property real recentOperationsMinimumHeight: 182
    readonly property real recentOperationsPreferredHeight: Math.max(
        recentOperationsMinimumHeight,
        height - Theme.pageMargin * 2 - dashboardHeader.implicitHeight
        - reportingPeriodCard.implicitHeight - metricGrid.implicitHeight
        - summaryGrid.implicitHeight - content.spacing * 4)

    function applyReportingFrom(value) {
        const selected = reportingFromPicker.parsedDate(value)
        const currentTo = reportingToPicker.parsedDate(page.app.reportingTo)
        page.app.setReportingPeriod(value,
                                    selected !== null && currentTo !== null
                                    && selected > currentTo
                                    ? value : page.app.reportingTo)
    }

    function applyReportingTo(value) {
        const selected = reportingToPicker.parsedDate(value)
        const currentFrom = reportingFromPicker.parsedDate(page.app.reportingFrom)
        page.app.setReportingPeriod(selected !== null && currentFrom !== null
                                    && selected < currentFrom
                                    ? value : page.app.reportingFrom,
                                    value)
    }

    contentWidth: width
    contentHeight: Math.max(height, content.implicitHeight + Theme.pageMargin * 2)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    ScrollBar.vertical: ScrollBar {
        policy: page.contentHeight > page.height
                ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
    }

    ColumnLayout {
        id: content
        width: parent.width - Theme.pageMargin * 2
        x: Theme.pageMargin
        y: Theme.pageMargin
        spacing: 16

        RowLayout {
            id: dashboardHeader
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 4
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Обзор")
                    color: Theme.text
                    font.pixelSize: 26
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: page.app.demoMode
                          ? qsTr("Пример данных · июль 2026")
                          : page.app.hasFinancialData
                            ? qsTr("Все подключённые счета")
                            : qsTr("Подключите банк или импортируйте выписку")
                    color: Theme.textMuted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            StatusBadge {
                visible: page.app.demoMode
                text: qsTr("Демо-данные")
            }
        }

        SectionCard {
            id: reportingPeriodCard
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: reportingPeriodLayout.implicitHeight + 24

            ColumnLayout {
                id: reportingPeriodLayout
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 14
                    rightMargin: 14
                }
                spacing: 7

                GridLayout {
                    Layout.fillWidth: true
                    columns: page.width > 760 ? 6 : 2
                    columnSpacing: 8
                    rowSpacing: 8

                    Text {
                        text: qsTr("С")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    DatePickerField {
                        id: reportingFromPicker
                        Layout.fillWidth: page.width <= 760
                        value: page.app.reportingFrom
                        accessibleName: qsTr("Начало периода")
                        enabled: !page.app.demoMode
                        onDateSelected: function(selectedValue) {
                            page.applyReportingFrom(selectedValue)
                        }
                    }
                    Text {
                        text: qsTr("По")
                        color: Theme.text
                        font.pixelSize: 12
                        font.weight: Font.Medium
                    }
                    DatePickerField {
                        id: reportingToPicker
                        Layout.fillWidth: page.width <= 760
                        value: page.app.reportingTo
                        accessibleName: qsTr("Конец периода")
                        enabled: !page.app.demoMode
                        onDateSelected: function(selectedValue) {
                            page.applyReportingTo(selectedValue)
                        }
                    }
                    ActionButton {
                        Layout.columnSpan: page.width > 760 ? 1 : 2
                        text: qsTr("Последний месяц")
                        enabled: !page.app.demoMode
                        onClicked: page.app.resetReportingPeriod()
                    }
                    Item {
                        Layout.fillWidth: true
                        visible: page.width > 760
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: page.app.reportingPeriodError.length > 0
                    text: page.app.reportingPeriodError
                    color: Theme.negative
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        GridLayout {
            id: metricGrid
            Layout.fillWidth: true
            columns: page.width > 900 ? 4 : 2
            columnSpacing: 12
            rowSpacing: 12

            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Капитал")
                value: page.app.netWorth
                hint: page.app.demoMode ? qsTr("+4,2% за месяц")
                                   : qsTr("Текущий остаток")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Доходы")
                value: page.app.reportIncome
                hint: qsTr("За выбранный период")
                accent: Theme.positive
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Расходы")
                value: page.app.reportSpending
                hint: qsTr("За выбранный период")
                accent: Theme.textMuted
            }
            MetricCard {
                Layout.fillWidth: true
                title: qsTr("Сбережения")
                value: page.app.savings
                hint: qsTr("Доходы минус расходы")
                accent: Theme.warning
            }
        }

        GridLayout {
            id: summaryGrid
            Layout.fillWidth: true
            columns: page.width > 860 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 12

            SectionCard {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredHeight: 270

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
                        visible: page.app.netWorthSeries.length > 1
                        values: page.app.netWorthSeries
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: page.app.netWorthSeries.length <= 1
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
                Layout.minimumWidth: 0
                Layout.preferredHeight: 270

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
                    Text {
                        text: page.app.reportingPeriodLabel
                        color: Theme.textMuted
                        font.pixelSize: 11
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Text {
                            anchors.centerIn: parent
                            visible: categoryList.count === 0
                            text: qsTr("Категории появятся после загрузки операций")
                            color: Theme.textMuted
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.WordWrap
                        }

                        ListView {
                            id: categoryList
                            anchors.fill: parent
                            visible: count > 0
                            model: page.app.categories
                            clip: true
                            spacing: 7
                            boundsBehavior: Flickable.StopAtBounds
                            reuseItems: true
                            ScrollBar.vertical: ScrollBar {
                                policy: categoryList.contentHeight > categoryList.height
                                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                            }

                            delegate: Item {
                                id: categoryRow
                                required property string name
                                required property string amount
                                required property real share
                                required property string color

                                width: ListView.view.width
                                height: 35

                                ColumnLayout {
                                    anchors.fill: parent
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 10
                                        Text {
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 0
                                            text: categoryRow.name
                                            color: Theme.text
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            Layout.minimumWidth: 72
                                            Layout.preferredWidth: 112
                                            Layout.maximumWidth: 132
                                            horizontalAlignment: Text.AlignRight
                                            text: categoryRow.amount
                                            color: Theme.textMuted
                                            font.pixelSize: 12
                                            elide: Text.ElideLeft
                                        }
                                    }
                                    Rectangle {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 4
                                        radius: 2
                                        color: Theme.surfaceRaised
                                        clip: true
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(1, categoryRow.share))
                                            height: parent.height
                                            radius: 2
                                            color: categoryRow.color
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        SectionCard {
            id: recentOperationsCard
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.minimumHeight: page.recentOperationsMinimumHeight
            Layout.preferredHeight: page.recentOperationsPreferredHeight

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
                        text: qsTr("Операции за период")
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
                        model: page.app.transactions
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        reuseItems: true
                        ScrollBar.vertical: ScrollBar {
                            policy: transactionList.contentHeight > transactionList.height
                                    ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                        }

                        delegate: Item {
                            id: recentTransaction
                            required property string date
                            required property string title
                            required property string category
                            required property string amount
                            required property bool income
                            required property bool transfer

                            width: ListView.view.width
                            height: 44

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 18
                                anchors.rightMargin: 18
                                Text {
                                    Layout.preferredWidth: 88
                                    Layout.minimumWidth: 72
                                    text: recentTransaction.date
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: recentTransaction.title
                                    color: Theme.text
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.preferredWidth: 160
                                    Layout.minimumWidth: 92
                                    Layout.maximumWidth: 160
                                    text: recentTransaction.category
                                    color: Theme.textMuted
                                    font.pixelSize: 11
                                    elide: Text.ElideRight
                                }
                                Text {
                                    Layout.preferredWidth: 105
                                    Layout.minimumWidth: 84
                                    Layout.maximumWidth: 105
                                    horizontalAlignment: Text.AlignRight
                                    text: recentTransaction.amount
                                    color: recentTransaction.transfer
                                           ? Theme.textMuted
                                           : recentTransaction.income
                                             ? Theme.positive : Theme.text
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
