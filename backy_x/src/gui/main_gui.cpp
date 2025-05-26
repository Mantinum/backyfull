#include "gui/MainWindow.h" // Adjust path if necessary
#include <QApplication>
#include <QCoreApplication> // For setting Org/App name

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Set OrganizationName and ApplicationName for QSettings to work predictably
    // This is important for Scheduler and MainWindow settings.
    QCoreApplication::setOrganizationName("BackyFullOrg"); // Replace with your actual org name
    QCoreApplication::setApplicationName("BackyFull");    // Replace with your actual app name

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
