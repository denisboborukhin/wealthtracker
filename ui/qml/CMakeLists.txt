qt_add_executable(wealthtracker
    MANUAL_FINALIZATION
    "${PROJECT_SOURCE_DIR}/src/app/main.cpp"
)

set_source_files_properties(
    Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(
    wealthtracker
    URI WealthTracker
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        Main.qml
        Theme.qml
        components/MetricCard.qml
        components/LineChart.qml
        components/BalanceHistoryPanel.qml
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
        pages/CategoriesPage.qml
        pages/ConnectionsPage.qml
        pages/SettingsPage.qml
)

target_link_libraries(
    wealthtracker
    PRIVATE
        app
        Qt6::QuickControls2
        Qt6::Concurrent
        OpenSSL::SSL
)
enable_strict_warnings(wealthtracker)
enable_sanitizers(wealthtracker)

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

qt_finalize_executable(wealthtracker)
