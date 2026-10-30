cmake_minimum_required(VERSION 3.30)
project(BankViewProbe LANGUAGES CXX)
find_package(Qt6 6.11 REQUIRED COMPONENTS Quick QuickControls2 WebView)
qt_standard_project_setup(REQUIRES 6.11)
qt_add_executable(view_probe main.cpp)
qt_add_qml_module(view_probe URI ViewProbe QML_FILES Main.qml)
target_link_libraries(view_probe PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::WebView)
set_target_properties(view_probe PROPERTIES
    QT_ANDROID_PACKAGE_NAME "org.wealthtracker.android.probe"
    QT_ANDROID_APP_NAME "Bank View Rendering Test"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 36
    QT_ANDROID_COMPILE_SDK_VERSION 36
    QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
)
