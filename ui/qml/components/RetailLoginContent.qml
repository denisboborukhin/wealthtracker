pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: content
    required property bool loginActive
    required property bool mobileLogin
    required property bool tbank
    required property string status
    required property var webView
    spacing: 10

    Text {
        Layout.fillWidth: true
        objectName: "retailLoginStatus"
        textFormat: Text.PlainText
        text: content.status.length > 0
              ? content.status
              : qsTr("Нажмите «Войти», чтобы открыть официальную страницу банка внутри приложения.")
        color: Theme.text
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 340
        visible: content.loginActive
        color: Theme.surface
        radius: 8
        border.color: Theme.border
        clip: true

        WindowContainer {
            objectName: "desktopBankWindow"
            anchors.fill: parent
            anchors.margins: 1
            visible: content.loginActive && !content.mobileLogin
            window: content.loginActive && !content.mobileLogin
                    ? content.webView : null
        }
    }

    Text {
        Layout.fillWidth: true
        visible: content.loginActive
        text: content.tbank
              ? qsTr("Телефон и код обрабатывает официальная страница банка. WealthTracker получает только идентификатор API-сессии. На macOS браузерные данные сохраняются до отключения банка.")
              : qsTr("Телефон и код обрабатывает официальная страница банка. WealthTracker не читает cookies или поля входа и получает только ограниченный снимок финансовых данных.")
        color: Theme.textMuted
        font.pixelSize: 10
        wrapMode: Text.WordWrap
    }
}
