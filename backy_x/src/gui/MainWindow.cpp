#include "gui/MainWindow.h"
#include "core/Scheduler.h"
#include "targets/LocalTarget.h" // For M1, directly using LocalTarget

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QWidget>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox> // For simple error/info messages
#include <QTime>
#include <QDir> // For path operations
#include <QDebug> // For console debug output
#include <QStandardPaths> // For default dialog paths
#include <QSettings> // For window settings
#include <QCoreApplication> // For QSettings org/app name
#include <filesystem> // For recursive directory iteration and path manipulation
#include <functional> // For std::function (if using a lambda helper)

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      sourceDirEdit_(nullptr),
      sourceDirButton_(nullptr),
      destinationDirEdit_(nullptr),
      destinationDirButton_(nullptr),
      backupTimeEdit_(nullptr),
      applyScheduleButton_(nullptr),
      runBackupButton_(nullptr),
      logDisplay_(nullptr),
      scheduler_(nullptr), // Initialized properly in the body
      localTarget_(nullptr), // Initialized later or when dest path is known
      fileDialog_(nullptr) // Initialized in setupUI
{
    // It's good practice to set OrganizationName and ApplicationName for QSettings
    // This should ideally be done once in main() of the GUI app.
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("BackyFullOrg");
    }
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("BackyFull");
    }

    scheduler_ = new Scheduler(QString(), this); // Scheduler is a child of MainWindow

    setupUI();
    loadSettings(); // Load window geometry and other UI settings

    // Connect scheduler signals
    connect(scheduler_, &Scheduler::backupTaskTriggered, this, &MainWindow::performBackup);
    connect(scheduler_, &Scheduler::taskChanged, this, &MainWindow::onTaskChanged);

    // Initialize UI from scheduler's loaded task
    onTaskChanged(); 

    updateLog("BackyFull application started.");
    updateLog("Please configure your backup source, destination, and schedule.");
}

MainWindow::~MainWindow() {
    saveSettings();
    // scheduler_ is a child, Qt handles its deletion.
    // localTarget_ might need explicit deletion if not parented.
    // If localTarget_ is created in performBackup and not parented, it should be deleted there or here.
    // Current implementation in performBackup re-creates it if destination changes,
    // but the last instance would leak if not deleted.
    delete localTarget_; 
    localTarget_ = nullptr;
    // fileDialog_ is parented to 'this', so Qt should handle it.
}

void MainWindow::setupUI() {
    setWindowTitle(tr("BackyFull - Backup Configuration"));

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QGridLayout *layout = new QGridLayout(centralWidget);

    // Source Directory
    layout->addWidget(new QLabel(tr("Source Directory:")), 0, 0);
    sourceDirEdit_ = new QLineEdit();
    sourceDirEdit_->setReadOnly(true); 
    layout->addWidget(sourceDirEdit_, 0, 1);
    sourceDirButton_ = new QPushButton(tr("Browse..."));
    connect(sourceDirButton_, &QPushButton::clicked, this, &MainWindow::selectSourceDirectory);
    layout->addWidget(sourceDirButton_, 0, 2);

    // Destination Directory
    layout->addWidget(new QLabel(tr("Destination Directory (Local):")), 1, 0);
    destinationDirEdit_ = new QLineEdit();
    destinationDirEdit_->setReadOnly(true); 
    layout->addWidget(destinationDirEdit_, 1, 1);
    destinationDirButton_ = new QPushButton(tr("Browse..."));
    connect(destinationDirButton_, &QPushButton::clicked, this, &MainWindow::selectDestinationDirectory);
    layout->addWidget(destinationDirButton_, 1, 2);

    // Backup Time
    layout->addWidget(new QLabel(tr("Daily Backup Time:")), 2, 0);
    backupTimeEdit_ = new QTimeEdit();
    backupTimeEdit_->setDisplayFormat("HH:mm");
    layout->addWidget(backupTimeEdit_, 2, 1);

    // Apply Schedule Button
    applyScheduleButton_ = new QPushButton(tr("Apply Schedule"));
    connect(applyScheduleButton_, &QPushButton::clicked, this, &MainWindow::applySchedule);
    layout->addWidget(applyScheduleButton_, 3, 0, 1, 3); 

    // Run Backup Now Button
    runBackupButton_ = new QPushButton(tr("Run Backup Now"));
    connect(runBackupButton_, &QPushButton::clicked, this, &MainWindow::runBackupNow);
    layout->addWidget(runBackupButton_, 4, 0, 1, 3); 

    // Log Display
    layout->addWidget(new QLabel(tr("Logs:")), 5, 0);
    logDisplay_ = new QTextEdit();
    logDisplay_->setReadOnly(true);
    layout->addWidget(logDisplay_, 6, 0, 1, 3); 

    layout->setRowStretch(6, 1); 

    fileDialog_ = new QFileDialog(this);
}

void MainWindow::selectSourceDirectory() {
    fileDialog_->setWindowTitle(tr("Select Source Directory"));
    fileDialog_->setFileMode(QFileDialog::Directory);
    fileDialog_->setOption(QFileDialog::ShowDirsOnly, true);
    QString initialPath = sourceDirEdit_->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : sourceDirEdit_->text();
    QString directory = fileDialog_->getExistingDirectory(this, tr("Select Source Directory"), initialPath);
    if (!directory.isEmpty()) {
        sourceDirEdit_->setText(QDir::toNativeSeparators(directory));
        updateLog(QString("Source directory selected: %1").arg(directory));
    }
}

void MainWindow::selectDestinationDirectory() {
    fileDialog_->setWindowTitle(tr("Select Destination Directory"));
    fileDialog_->setFileMode(QFileDialog::Directory);
    fileDialog_->setOption(QFileDialog::ShowDirsOnly, true);
    QString initialPath = destinationDirEdit_->text().isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation) : destinationDirEdit_->text();
    QString directory = fileDialog_->getExistingDirectory(this, tr("Select Destination Directory"), initialPath);
    if (!directory.isEmpty()) {
        destinationDirEdit_->setText(QDir::toNativeSeparators(directory));
        updateLog(QString("Destination directory selected: %1").arg(directory));
    }
}

void MainWindow::applySchedule() {
    QString sourcePath = sourceDirEdit_->text();
    QString destPath = destinationDirEdit_->text();
    QTime backupTime = backupTimeEdit_->time();

    if (sourcePath.isEmpty() || destPath.isEmpty()) {
        QMessageBox::warning(this, tr("Configuration Error"), tr("Source and destination paths cannot be empty."));
        updateLog("Error: Failed to apply schedule. Source or destination empty.");
        return;
    }
    if (!backupTime.isValid()) {
        QMessageBox::warning(this, tr("Configuration Error"), tr("The selected backup time is invalid."));
        updateLog("Error: Failed to apply schedule. Invalid time.");
        return;
    }

    scheduler_->setDailyBackupTask(sourcePath, destPath, backupTime, true); 
    updateLog(QString("Schedule applied: Backup at %1 for source '%2' to '%3'.").arg(backupTime.toString("HH:mm"), sourcePath, destPath));
    QMessageBox::information(this, tr("Schedule Updated"), tr("Backup schedule has been updated and saved."));
}

void MainWindow::runBackupNow() {
    QString sourcePath = sourceDirEdit_->text();
    QString destPath = destinationDirEdit_->text();

    if (sourcePath.isEmpty() || destPath.isEmpty()) {
        QMessageBox::warning(this, tr("Backup Error"), tr("Source and destination paths must be configured before running a backup."));
        updateLog("Error: Manual backup failed. Source or destination empty.");
        return;
    }
    
    updateLog(QString("Manual backup started: From '%1' to '%2'.").arg(sourcePath, destPath));
    performBackup(sourcePath, destPath);
}

void MainWindow::performBackup(const QString& sourcePath, const QString& destinationPath) {
    updateLog(QString("Performing backup: Source: %1, Destination: %2").arg(sourcePath, destinationPath));

    std::filesystem::path fsSourcePath(sourcePath.toStdString());
    QString sourceFolderName = QString::fromStdString(fsSourcePath.filename().string());
    if (sourceFolderName.isEmpty() || sourceFolderName == "." || sourceFolderName == "..") {
        // This case should ideally be handled by input validation before calling performBackup,
        // or use a default name like "root_backup" if sourcePath is "/"
        updateLog(QString("Error: Could not determine a valid source folder name from '%1'.").arg(sourcePath));
        QMessageBox::critical(this, tr("Backup Failed"), tr("Invalid source path. Cannot determine source folder name."));
        return;
    }
    if (!std::filesystem::is_directory(fsSourcePath)) {
         updateLog(QString("Error: Source path '%1' is not a directory.").arg(sourcePath));
         QMessageBox::critical(this, tr("Backup Failed"), tr("Source path is not a directory."));
         return;
    }


    std::filesystem::path fsDestinationPathFromUI(destinationPath.toStdString());
    std::filesystem::path backupRootForTarget = fsDestinationPathFromUI / fsSourcePath.filename();
    std::string backupRootStdStr = backupRootForTarget.string();

    if (localTarget_ && localTarget_->destinationPathStdStr() != backupRootStdStr) {
        delete localTarget_;
        localTarget_ = nullptr;
    }
    if (!localTarget_) {
        localTarget_ = new LocalTarget(backupRootStdStr);
    }
    
    if (!localTarget_->beginSession()) {
        updateLog(QString("Error: Could not begin backup session with LocalTarget at '%1'.").arg(QString::fromStdString(backupRootStdStr)));
        QMessageBox::critical(this, tr("Backup Failed"), tr("Could not begin backup session. Check logs."));
        return;
    }

    if (!std::filesystem::exists(fsSourcePath)) {
        updateLog(QString("Error: Source directory '%1' does not exist.").arg(sourcePath));
        localTarget_->endSession();
        QMessageBox::critical(this, tr("Backup Failed"), tr("Source directory not found."));
        return;
    }
    
    updateLog(QString("Starting recursive scan of source directory: %1").arg(sourcePath));
    bool all_ok = true;
    int files_processed_count = 0;

    std::function<void(const std::filesystem::path&)> recursiveCopy;
    recursiveCopy = 
       [&](const std::filesystem::path& currentPathInSource) {
       try {
           for (const auto& entry : std::filesystem::directory_iterator(currentPathInSource)) {
               std::filesystem::path fullEntryPath = entry.path();
               std::filesystem::path relativePath = std::filesystem::relative(fullEntryPath, fsSourcePath);
               std::string relativePathStr = relativePath.generic_string(); // Use generic_string for platform-independent slashes

               if (entry.is_directory()) {
                   updateLog(QString("Scanning subdirectory: %1").arg(QString::fromStdString(relativePathStr)));
                   recursiveCopy(fullEntryPath); // Recurse
               } else if (entry.is_regular_file()) {
                   files_processed_count++;
                   // For M2, LocalTarget::sendFile expects the full path of the source file
                   // and the relative path for constructing the destination.
                   if (localTarget_->sendFile(fullEntryPath.string(), relativePathStr)) {
                       updateLog(QString("Backed up: %1").arg(QString::fromStdString(relativePathStr)));
                   } else {
                       updateLog(QString("Error backing up: %1").arg(QString::fromStdString(relativePathStr)));
                       all_ok = false; 
                   }
               }
           }
       } catch (const std::filesystem::filesystem_error& e) {
            updateLog(QString("Error accessing path %1: %2").arg(QString::fromStdString(currentPathInSource.string()), QString::fromStdString(e.what())));
            all_ok = false;
       }
    };

    recursiveCopy(fsSourcePath); // Initial call to start the recursion

    if (!localTarget_->endSession()) {
        updateLog("Error: Could not properly end backup session with LocalTarget.");
        all_ok = false; // Consider this a failure as well
    }

    if (all_ok) {
        if (files_processed_count == 0) {
            updateLog(QString("Backup completed. No files were found to back up in '%1'.").arg(sourcePath));
            QMessageBox::information(this, tr("Backup Complete"), tr("Backup completed. No files were found in the source directory."));
        } else {
            updateLog(QString("Backup process completed successfully. %1 files processed.").arg(files_processed_count));
            QMessageBox::information(this, tr("Backup Complete"), tr("Backup completed successfully."));
        }
    } else {
        updateLog("Backup process completed with some errors. Please check the log.");
        QMessageBox::warning(this, tr("Backup Complete (with errors)"), tr("Backup completed with some errors. Please check the log."));
    }
}


void MainWindow::updateLog(const QString& message) {
    QString timestamp = QTime::currentTime().toString("HH:mm:ss");
    logDisplay_->append(QString("[%1] %2").arg(timestamp, message));
    qDebug() << "[GUI Log]" << message; 
}

void MainWindow::onTaskChanged() {
    sourceDirEdit_->setText(scheduler_->sourcePath());
    destinationDirEdit_->setText(scheduler_->destinationPath());
    if (scheduler_->scheduledTime().isValid()) {
        backupTimeEdit_->setTime(scheduler_->scheduledTime());
    } else {
        backupTimeEdit_->setTime(QTime(23,0)); 
    }
    updateLog("Task details updated in UI from Scheduler.");
}

void MainWindow::loadSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(600, 400); 
    }
    settings.endGroup();
}

void MainWindow::saveSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.endGroup();
}
