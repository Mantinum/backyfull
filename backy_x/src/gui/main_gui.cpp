#include "gui/MainWindow.h" // Adjust path if necessary
#include <QApplication>
#include <QFile>
#include <QCoreApplication> // For setting Org/App name
#include <curl/curl.h>      // For libcurl global init/cleanup
#include <iostream>         // For std::cout

int main(int argc, char *argv[]) {
    // Initialize libcurl globally
    if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
        std::cerr << "Error: Failed to initialize libcurl globally in GUI." << std::endl;
        // Depending on the application, you might want to exit or handle this error
        return 1; // Indicate an error
    }
    std::cout << "libcurl global init/cleanup managed in GUI." << std::endl;

    QApplication app(argc, argv);

    QFile styleFile("assets/style/backyfull.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        app.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }

    // Set OrganizationName and ApplicationName for QSettings to work predictably
    // This is important for Scheduler and MainWindow settings.
    QCoreApplication::setOrganizationName("BackyFullOrg"); // Replace with your actual org name
    QCoreApplication::setApplicationName("BackyFull");    // Replace with your actual app name

    MainWindow mainWindow;
    mainWindow.show();

    int result = app.exec();

    // Cleanup libcurl globally
    curl_global_cleanup();
    std::cout << "libcurl global cleanup finished in GUI." << std::endl; // Optional: confirm cleanup

    return result;
}
