qt_add_executable(wealthtracker
    MANUAL_FINALIZATION
    "${PROJECT_SOURCE_DIR}/src/app/main.cpp"
)

set_source_files_properties(
    Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(
    ui
    STATIC
    PLUGIN_TARGET ui_plugin
    CLASS_NAME WealthTrackerPlugin
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/WealthTracker"
    URI WealthTracker
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        Main.qml
        Theme.qml
        components/PrivacyScreen.qml
        components/MetricCard.qml
        components/BalanceHistoryPanel.qml
        components/RetailConnection.qml
        components/PluginConnectionEditor.qml
        components/RetailLoginContent.qml
        components/MobileRetailLoginContent.qml
        components/BankTokenEditor.qml
        components/ConnectionList.qml
        components/SectionCard.qml
        components/ActionButton.qml
        components/DatePickerField.qml
        components/StatusBadge.qml
        components/MinimalComboBox.qml
        components/MinimalSwitch.qml
        pages/DashboardPage.qml
        pages/AccountsPage.qml
        pages/HistoryPage.qml
        pages/TransactionsPage.qml
        pages/ImportsPage.qml
        pages/InvestmentsPage.qml
        pages/CryptoPage.qml
        pages/CashbackPage.qml
        pages/CategoriesPage.qml
        pages/ConnectionsPage.qml
        pages/SettingsPage.qml
)

target_link_libraries(
    wealthtracker
    PRIVATE
        app
        ui_plugin
        Qt6::QuickControls2
        OpenSSL::SSL
)
target_link_libraries(ui PRIVATE Qt6::QuickControls2)
add_custom_target(wealthtracker_qmllint DEPENDS ui_qmllint)

enable_strict_warnings(wealthtracker)
enable_sanitizers(wealthtracker)

if(ANDROID)
    target_compile_definitions(wealthtracker PRIVATE WT_APPLICATION_VERSION="${android_version_name}")
else()
    target_compile_definitions(wealthtracker PRIVATE WT_APPLICATION_VERSION="${PROJECT_VERSION}")
endif()

set_target_properties(
    wealthtracker
    PROPERTIES
        OUTPUT_NAME "WealthTracker"
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "WealthTracker"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.wealthtracker.desktop"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
        WIN32_EXECUTABLE TRUE
)

if(ANDROID)
    qt_add_resources(wealthtracker android_bank_ca
        PREFIX "/certificates"
        BASE "${PROJECT_SOURCE_DIR}/android/res/raw"
        FILES "${PROJECT_SOURCE_DIR}/android/res/raw/russian_trusted_root_ca.pem"
    )
    set_target_properties(wealthtracker PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "org.wealthtracker.android"
        QT_ANDROID_APP_NAME "WealthTracker"
        QT_ANDROID_VERSION_CODE "${android_version_code}"
        QT_ANDROID_VERSION_NAME "${android_version_name}"
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_COMPILE_SDK_VERSION 36
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
        QT_ANDROID_EXTRA_LIBS "${android_tls_libraries}"
    )
endif()

qt_finalize_executable(wealthtracker)
