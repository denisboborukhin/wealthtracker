pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

FocusScope {
    id: privacyScreen
    signal unlockRequested()
    focus: visible
    z: 100

    onVisibleChanged: {
        if (visible)
            returnToDataButton.forceActiveFocus()
    }
    Keys.onPressed: function(event) {
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.background
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
    }

    Rectangle {
        objectName: "privacyCard"
        anchors.centerIn: parent
        width: Math.min(440, parent.width - 48)
        height: privacyContent.implicitHeight + 56
        radius: 12
        color: Theme.surface
        border.color: Theme.border

        ColumnLayout {
            id: privacyContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 28
            spacing: 14
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Экран приватности включён")
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Суммы и операции скрыты. Текущие входы и синхронизации остановлены, сохранённые данные не удалены.")
                color: Theme.textMuted
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Это не блокировка приложения: для возврата пароль не требуется.")
                color: Theme.textMuted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            ActionButton {
                id: returnToDataButton
                objectName: "returnToDataButton"
                Layout.fillWidth: true
                text: qsTr("Вернуться к данным")
                primary: true
                onClicked: privacyScreen.unlockRequested()
            }
        }
    }
}
