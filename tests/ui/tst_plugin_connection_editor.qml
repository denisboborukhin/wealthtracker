import QtQuick
import QtQuick.Controls
import QtTest
import WealthTracker

TestCase {
    id: test
    name: "PluginConnectionEditor"
    width: 520
    height: 800
    visible: true
    when: windowShown

    Component {
        id: editorComponent
        PluginConnectionEditor {
            width: 460
            app: QtObject {
                property string zenPluginId: "synthetic-bank"
                property string zenPluginBundlePath: "/synthetic/bundle.js"
                property string zenPluginInstitution: "Synthetic bank"
                property bool zenPluginRunning: false
                property bool zenPluginAutoUpdate: false
                property string zenPluginLastSync: "—"
                property string zenPluginNextSync: "—"
                property string zenPluginPrompt: ""
                property bool zenPluginPromptSecret: true
                property var zenPluginFields: [
                    {key: "login", label: "Login", kind: "text", sensitive: false,
                     required: true, defaultValue: "synthetic-login"},
                    {key: "password", label: "Password", kind: "text", sensitive: true,
                     required: true, defaultValue: ""},
                    {key: "remember", label: "Remember", kind: "checkbox", sensitive: false,
                     required: false, defaultValue: "true"},
                    {key: "phone", label: "Phone", kind: "phone", sensitive: false,
                     required: false, defaultValue: ""}
                ]
                property bool acceptStart: true
                property bool acceptAnswer: true
                property string startedInstitution: ""
                property var startedPreferences: ({})
                property string answer: ""
                function startZenPlugin(institution, preferences) {
                    startedInstitution = institution
                    startedPreferences = Object.assign({}, preferences)
                    return acceptStart
                }
                function answerZenPluginPrompt(value) {
                    answer = value
                    return acceptAnswer
                }
                function setZenPluginAutoUpdate(value) { zenPluginAutoUpdate = value }
            }
        }
    }

    function editor() {
        const item = createTemporaryObject(editorComponent, test)
        verify(item !== null)
        return item
    }

    function test_defaultsAndStart() {
        const item = editor()
        const password = findChild(item, "pluginPreference_password")
        compare(password.echoMode, TextInput.Password)
        compare(findChild(item, "pluginPreference_login").echoMode, TextInput.Normal)
        compare(findChild(item, "pluginPreference_phone").inputMethodHints, Qt.ImhDigitsOnly)
        verify(findChild(item, "pluginCheckbox_remember").checked)
        verify(item.canStart)
        item.institution = "  "
        verify(!item.canStart)
        item.institution = "New institution"
        password.text = "synthetic-password"
        item.app.acceptStart = false
        item.start()
        compare(password.text, "synthetic-password")
        item.app.acceptStart = true
        item.start()
        compare(item.app.startedInstitution, "New institution")
        compare(item.app.startedPreferences.login, "synthetic-login")
        compare(item.app.startedPreferences.password, "synthetic-password")
        compare(item.app.startedPreferences.remember, true)
        compare(password.text, "")
        compare(findChild(item, "pluginPreference_login").text, "synthetic-login")
        item.start()
        compare(item.app.startedPreferences.password, "")
    }

    function test_promptAndClose() {
        const item = editor()
        item.app.zenPluginRunning = true
        item.app.zenPluginPrompt = "Synthetic prompt"
        const prompt = findChild(item, "pluginPromptField")
        verify(prompt.visible)
        compare(prompt.echoMode, TextInput.Password)
        verify(!item.canAnswer)
        prompt.text = "synthetic-answer"
        verify(item.canAnswer)
        item.app.acceptAnswer = false
        item.answerPrompt()
        compare(prompt.text, "synthetic-answer")
        item.app.acceptAnswer = true
        item.answerPrompt()
        compare(item.app.answer, "synthetic-answer")
        compare(prompt.text, "")
        item.app.zenPluginPromptSecret = false
        compare(prompt.echoMode, TextInput.Normal)
        const password = findChild(item, "pluginPreference_password")
        password.text = "synthetic-password"
        prompt.text = "synthetic-answer"
        item.clear()
        compare(prompt.text, "")
        compare(password.text, "")
        compare(item.institution, "Synthetic bank")
    }

    function test_replacedBundleDefaults() {
        const item = editor()
        item.resetPreferences()
        item.app.zenPluginFields = [
            {key: "account", label: "Account", kind: "text", sensitive: false,
             required: true, defaultValue: "new-default"},
            {key: "enabled", label: "Enabled", kind: "checkbox", sensitive: false,
             required: false, defaultValue: false}
        ]
        item.start()
        compare(item.app.startedPreferences.account, "new-default")
        compare(item.app.startedPreferences.enabled, false)
        compare(Object.keys(item.app.startedPreferences).length, 2)
    }
}
