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
if(ANDROID)
    # All module producers and consumers must agree on Clang's thread mode.
    add_compile_options(-pthread)
endif()

find_package(Boost REQUIRED)
find_package(
    Qt6 6.11 REQUIRED
    COMPONENTS Core Gui Qml Quick QuickControls2 Network Concurrent WebView
)
find_package(sqlcipher REQUIRED CONFIG)
find_package(OpenSSL REQUIRED CONFIG)
find_package(libsodium REQUIRED CONFIG)
find_package(xlsxio REQUIRED CONFIG)
if(ANDROID)
    include(android/cmake/TLS.cmake)
endif()

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
            src/storage/balance_history.cppm
            src/storage/storage.cppm
    PRIVATE
        src/storage/database.cpp
        src/storage/transaction.cpp
        src/storage/migrations.cpp
        src/storage/import_session.cpp
        src/storage/repository.cpp
        src/storage/balance_history.cpp
)
target_link_libraries(storage PUBLIC sqlcipher::sqlcipher)
if(ANDROID)
    target_link_libraries(storage PRIVATE log)
endif()
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
        src/platform/credential_vault.cpp
)
target_link_libraries(platform PUBLIC libsodium::libsodium)
set_property(TARGET platform PROPERTY CXX_SCAN_FOR_MODULES ON)
if(APPLE)
    target_link_libraries(platform PRIVATE "-framework CoreFoundation" "-framework Security")
elseif(ANDROID)
    target_sources(platform PRIVATE src/platform/android_credential_store.cpp)
    target_link_libraries(platform PRIVATE Qt6::Core)
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
        src/connectors/zen_plugin.cpp
        src/connectors/deduplication.cpp
        src/connectors/network_policy.cpp
        src/connectors/t_invest_client.cpp
        src/connectors/official_bank_clients.cpp
        src/connectors/tbank_retail_client.cpp
        src/connectors/retail_accounts.cpp
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
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        FILES
            src/app/reporting.cppm
            src/app/crypto_wallet.cppm
            src/app/cashback.cppm
            src/app/cashback_sync.cppm
            src/app/alfa_cashback.cppm
            src/app/tbank_cashback.cppm
            src/app/balance_history_view.cppm
            src/app/transfer_matching.cppm
            src/app/bank_sync.cppm
            src/app/investment_sync.cppm
            src/app/zen_plugin_runner.cppm
            src/app/tbank_auth.cppm
            src/app/tbank_browser.cppm
            src/app/tbank_login.cppm
            src/app/alfa_browser.cppm
            src/app/alfa_login.cppm
            src/app/vtb_browser.cppm
            src/app/vtb_login.cppm
    PRIVATE
        include/app/AppController.hpp
        include/app/FinanceModels.hpp
        src/app/AppController.cpp
        src/app/FinanceModels.cpp
)
if(ANDROID)
    target_sources(app PRIVATE src/app/BrowserSessionAndroid.cpp)
endif()
if(APPLE)
    enable_language(OBJCXX)
    target_sources(app PRIVATE src/app/BrowserSession.mm)
    target_link_libraries(app PRIVATE "-framework WebKit")
endif()
target_include_directories(app PUBLIC include)
qt_add_resources(
    app
    zen_plugin_runtime
    PREFIX
        "/org/wealthtracker/runtime"
    FILES
        scripts/zen_plugin_host.mjs
)
target_link_libraries(
    app
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::WebView
        core
        storage
        platform
        connectors
)
set_property(TARGET app PROPERTY CXX_SCAN_FOR_MODULES ON)
enable_strict_warnings(app)
enable_sanitizers(app)

add_subdirectory(ui/qml)

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
    enable_catch2_compatibility(core_tests)
    enable_sanitizers(core_tests)
    catch_discover_tests(core_tests)

    add_executable(storage_tests
        tests/storage/storage_test.cpp
        tests/storage/balance_history_test.cpp
    )
    target_link_libraries(storage_tests PRIVATE storage Catch2::Catch2WithMain)
    set_property(TARGET storage_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(storage_tests)
    enable_catch2_compatibility(storage_tests)
    enable_sanitizers(storage_tests)
    catch_discover_tests(storage_tests)

    add_executable(platform_tests
        tests/platform/vault_crypto_test.cpp
        tests/platform/credential_store_test.cpp
    )
    target_link_libraries(platform_tests PRIVATE platform Catch2::Catch2WithMain)
    set_property(TARGET platform_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    enable_strict_warnings(platform_tests)
    enable_catch2_compatibility(platform_tests)
    enable_sanitizers(platform_tests)
    catch_discover_tests(platform_tests)

    add_executable(connectors_tests
        tests/connectors/test_main.cpp
        tests/connectors/import_test.cpp
        tests/connectors/deduplication_test.cpp
        tests/connectors/network_test.cpp
        tests/connectors/retail_accounts_test.cpp
        tests/connectors/zen_plugin_test.cpp
    )
    target_link_libraries(
        connectors_tests
        PRIVATE
            connectors
            Catch2::Catch2
    )
    set_property(TARGET connectors_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_compile_definitions(
        connectors_tests
        PRIVATE
            WEALTHTRACKER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    enable_strict_warnings(connectors_tests)
    enable_catch2_compatibility(connectors_tests)
    enable_sanitizers(connectors_tests)
    catch_discover_tests(connectors_tests)

    add_executable(app_tests
        tests/app/test_main.cpp
        tests/app/finance_models_test.cpp
        tests/app/reporting_test.cpp
        tests/app/balance_history_view_test.cpp
        tests/app/crypto_wallet_test.cpp
        tests/app/cashback_test.cpp
        tests/app/cashback_sync_test.cpp
        tests/app/tbank_cashback_test.cpp
        tests/app/transfer_matching_test.cpp
        tests/app/bank_connections_test.cpp
        tests/app/alfa_reliability_test.cpp
        tests/app/tbank_reliability_test.cpp
        tests/app/sync_reliability_test.cpp
        tests/app/vtb_connection_test.cpp
        tests/app/ui_contract_test.cpp
    )
    target_link_libraries(app_tests PRIVATE app Catch2::Catch2)
    set_property(TARGET app_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_compile_definitions(
        app_tests
        PRIVATE
            WEALTHTRACKER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    enable_strict_warnings(app_tests)
    enable_catch2_compatibility(app_tests)
    enable_sanitizers(app_tests)
    catch_discover_tests(app_tests)

    if(APPLE)
        add_executable(webview_tests
            tests/app/webview_test_main.cpp
            tests/app/browser_session_probe.cpp
            tests/app/webview_lifecycle_test.cpp
            tests/app/retail_refresh_test.cpp
            tests/app/cashback_webview_test.cpp
            tests/app/alfa_cashback_test.cpp
        )
        target_link_libraries(webview_tests PRIVATE app Catch2::Catch2)
        set_property(TARGET webview_tests PROPERTY CXX_SCAN_FOR_MODULES ON)
        enable_strict_warnings(webview_tests)
        enable_catch2_compatibility(webview_tests)
        enable_sanitizers(webview_tests)
        catch_discover_tests(webview_tests)
    endif()
endif()

install(
    TARGETS wealthtracker
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)
# CMake strips build rpaths before macdeployqt runs. Stage the pinned TLS
# libraries first so deployment resolves them without a system installation.
string(TOUPPER "${CMAKE_BUILD_TYPE}" dependency_configuration)
if(APPLE)
    foreach(tls_library IN ITEMS ssl crypto)
        unset(tls_runtime)
        find_file(tls_runtime
            NAMES "lib${tls_library}.3.dylib"
            PATHS ${openssl_LIB_DIRS_${dependency_configuration}}
            NO_DEFAULT_PATH NO_CACHE REQUIRED
        )
        install(FILES "${tls_runtime}" DESTINATION "WealthTracker.app/Contents/Frameworks")
    endforeach()
elseif(WIN32)
    install(DIRECTORY ${openssl_BIN_DIRS_${dependency_configuration}}/
        DESTINATION "${CMAKE_INSTALL_BINDIR}" FILES_MATCHING PATTERN "*.dll")
endif()
install(
    FILES LICENSE THIRD_PARTY_NOTICES.md conan.lock
    DESTINATION "${CMAKE_INSTALL_DATADIR}/wealthtracker"
)

get_filename_component(qt_install_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
if(EXISTS "${qt_install_prefix}/sbom")
    install(
        DIRECTORY "${qt_install_prefix}/sbom/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/wealthtracker/qt-sbom"
        FILES_MATCHING PATTERN "*.spdx"
    )
endif()

qt_generate_deploy_qml_app_script(
    TARGET wealthtracker
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    EXCLUDE_PLUGIN_TYPES sqldrivers
)
install(SCRIPT "${deploy_script}")

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
