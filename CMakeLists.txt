cmake_minimum_required(VERSION 3.30)

project(
    WealthTracker
    VERSION 0.1.0
    DESCRIPTION "Local-first personal finance and investment tracker"
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)
include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(Boost REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent)
find_package(sqlcipher REQUIRED CONFIG)
find_package(libsodium REQUIRED CONFIG)
find_package(xlsxio REQUIRED CONFIG)

qt_standard_project_setup(REQUIRES 6.11)

add_library(core STATIC)
target_sources(
    core
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        FILES
            src/core/decimal.cppm
            src/core/money.cppm
            src/core/models.cppm
            src/core/categorization.cppm
            src/core/analytics.cppm
            src/core/core.cppm
)
target_link_libraries(core PUBLIC Boost::headers)
enable_strict_warnings(core)
enable_sanitizers(core)

add_library(storage STATIC)
target_sources(
    storage
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        FILES
            src/storage/error.cppm
            src/storage/database.cppm
            src/storage/transaction.cppm
            src/storage/migrations.cppm
            src/storage/import_session.cppm
            src/storage/repository.cppm
            src/storage/storage.cppm
    PRIVATE
        src/storage/database.cpp
        src/storage/transaction.cpp
        src/storage/migrations.cpp
        src/storage/import_session.cpp
        src/storage/repository.cpp
)
target_link_libraries(storage PUBLIC sqlcipher::sqlcipher)
set_property(TARGET storage PROPERTY CXX_SCAN_FOR_MODULES ON)
enable_strict_warnings(storage)
enable_sanitizers(storage)

add_library(platform STATIC)
target_sources(
    platform
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        FILES
            src/platform/vault_crypto.cppm
            src/platform/credential_store.cppm
            src/platform/platform.cppm
    PRIVATE
        src/platform/vault_crypto.cpp
        src/platform/credential_store.cpp
)
target_link_libraries(platform PUBLIC libsodium::libsodium)
set_property(TARGET platform PROPERTY CXX_SCAN_FOR_MODULES ON)
if(APPLE)
    target_link_libraries(platform PRIVATE "-framework Security")
elseif(WIN32)
    target_link_libraries(platform PRIVATE Advapi32)
endif()
enable_strict_warnings(platform)
enable_sanitizers(platform)

add_library(connectors STATIC)
target_sources(
    connectors
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        FILES
            src/connectors/connectors.cppm
    PRIVATE
        src/connectors/import.cpp
        src/connectors/bank_presets.cpp
        src/connectors/bank_sessions.cpp
        src/connectors/deduplication.cpp
        src/connectors/network_policy.cpp
        src/connectors/t_invest_client.cpp
        src/connectors/cbr_fx_client.cpp
)
target_link_libraries(
    connectors
    PUBLIC
        Qt6::Core
        Qt6::Network
        Boost::headers
    PRIVATE
        xlsxio::xlsxio_read
)
set_property(TARGET connectors PROPERTY CXX_SCAN_FOR_MODULES ON)
enable_strict_warnings(connectors)
enable_sanitizers(connectors)

add_library(app STATIC)
target_sources(
    app
    PRIVATE
        include/app/AppController.hpp
        include/app/FinanceModels.hpp
        src/app/AppController.cpp
        src/app/FinanceModels.cpp
)
target_include_directories(app PUBLIC include)
target_link_libraries(
    app
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        core
        storage
        platform
        connectors
)
set_property(TARGET app PROPERTY CXX_SCAN_FOR_MODULES ON)
enable_strict_warnings(app)
enable_sanitizers(app)

qt_add_executable(wealthtracker
    MANUAL_FINALIZATION
    src/app/main.cpp
)

set_source_files_properties(
    ui/qml/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(
    wealthtracker
    URI WealthTracker
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    NO_IMPORT_SCAN
    QML_FILES
        ui/qml/Main.qml
        ui/qml/Theme.qml
        ui/qml/components/MetricCard.qml
        ui/qml/components/LineChart.qml
        ui/qml/components/DonutChart.qml
        ui/qml/components/SectionCard.qml
        ui/qml/pages/DashboardPage.qml
        ui/qml/pages/AccountsPage.qml
        ui/qml/pages/TransactionsPage.qml
        ui/qml/pages/ImportsPage.qml
        ui/qml/pages/InvestmentsPage.qml
        ui/qml/pages/CategoriesPage.qml
        ui/qml/pages/ConnectionsPage.qml
        ui/qml/pages/BackupsPage.qml
        ui/qml/pages/SettingsPage.qml
)

target_link_libraries(
    wealthtracker
    PRIVATE
        app
        Qt6::QuickControls2
        Qt6::Concurrent
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
        WIN32_EXECUTABLE TRUE
)

qt_finalize_executable(wealthtracker)

if(BUILD_TESTING)
    find_package(Catch2 3 REQUIRED CONFIG)
    include(Catch)

    add_executable(core_tests
        tests/core/money_decimal_test.cpp
        tests/core/categorization_test.cpp
        tests/core/analytics_test.cpp
    )
    target_link_libraries(core_tests PRIVATE core Catch2::Catch2WithMain)
    set_property(TARGET core_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(core_tests)
    enable_sanitizers(core_tests)
    catch_discover_tests(core_tests)

    add_executable(storage_tests tests/storage/storage_test.cpp)
    target_link_libraries(storage_tests PRIVATE storage Catch2::Catch2WithMain)
    set_property(TARGET storage_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(storage_tests)
    enable_sanitizers(storage_tests)
    catch_discover_tests(storage_tests)

    add_executable(platform_tests tests/platform/vault_crypto_test.cpp)
    target_link_libraries(platform_tests PRIVATE platform Catch2::Catch2WithMain)
    set_property(TARGET platform_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(platform_tests)
    enable_sanitizers(platform_tests)
    catch_discover_tests(platform_tests)

    add_executable(connectors_tests
        tests/connectors/import_test.cpp
        tests/connectors/deduplication_test.cpp
        tests/connectors/network_test.cpp
    )
    target_link_libraries(
        connectors_tests
        PRIVATE
            connectors
            Catch2::Catch2WithMain
    )
    set_property(TARGET connectors_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(connectors_tests)
    enable_sanitizers(connectors_tests)
    catch_discover_tests(connectors_tests)

    add_executable(app_tests tests/app/finance_models_test.cpp)
    target_link_libraries(app_tests PRIVATE app Catch2::Catch2WithMain)
    enable_strict_warnings(app_tests)
    enable_sanitizers(app_tests)
    catch_discover_tests(app_tests)
endif()

install(
    TARGETS wealthtracker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(
    FILES LICENSE THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/wealthtracker"
)

set(CPACK_PACKAGE_NAME "WealthTracker")
set(CPACK_PACKAGE_VENDOR "WealthTracker contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WealthTracker")
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_WIX_UPGRADE_GUID "9E98C80E-F69B-45BD-A7D1-544293BEB3FA")
else()
    set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)
