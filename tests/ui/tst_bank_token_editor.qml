import QtQuick
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "BankTokenEditor"
    width: 400
    height: 400
    visible: true
    when: windowShown

    Component { id: editorComponent; BankTokenEditor {} }

    function test_placeholders_data() {
        return [
            {tag: "alfa", id: "alfa-api", placeholder: "Access token Альфа-Банка"},
            {tag: "business", id: "tbank-business", placeholder: "T-API токен"},
            {tag: "modul", id: "modulbank", placeholder: "API-токен или sandboxtoken"},
            {tag: "invest", id: "t-invest", placeholder: "Read-only токен Т-Инвест"}
        ]
    }

    function test_placeholders(data) {
        const editor = createTemporaryObject(editorComponent, test,
            {width: 320, connectorId: data.id, connected: false})
        verify(editor !== null)
        const field = findChild(editor, "bankTokenField")
        verify(field !== null)
        compare(field.placeholderText, data.placeholder)
        compare(field.echoMode, TextInput.Password)
        verify(field.selectByMouse)
        verify(editor.visible)
        verify(!editor.canSave)
    }

    function test_clearAndConnectionState() {
        const editor = createTemporaryObject(editorComponent, test,
            {width: 320, connectorId: "t-invest", connected: false})
        verify(editor !== null)
        const field = findChild(editor, "bankTokenField")
        editor.token = "  "
        verify(!editor.canSave)
        editor.token = "synthetic-readonly-token"
        compare(field.text, editor.token)
        verify(editor.canSave)
        editor.connected = true
        verify(!editor.visible)
        verify(!editor.canSave)
        editor.clear()
        compare(editor.token, "")
        compare(field.text, "")
        editor.connected = false
        verify(editor.visible)
        verify(!editor.canSave)
    }
}
