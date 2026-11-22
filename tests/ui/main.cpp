#include <QtQml/QQmlExtensionPlugin>
#include <QtQuickTest/quicktest.h>

Q_IMPORT_QML_PLUGIN(WealthTrackerPlugin)

int main(int argc, char** argv) {
    return quick_test_main(argc, argv, "wealthtracker", nullptr);
}
