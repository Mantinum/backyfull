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

    if (localTarget_ && localTarget_->destinationPathStdStr() != destinationPath.toStdString()) {
        delete localTarget_;
        localTarget_ = nullptr;
    }
    if (!localTarget_) {
        // Ensure LocalTarget has a way to get its configured destination path, e.g., destinationPathStdStr()
        // For now, we assume its constructor sets it and it can be compared.
        // If LocalTarget doesn't store its path in a comparable way, this logic might be flawed.
        // Let's add a getter in LocalTarget: std::string destinationPathStdStr() const { return destinationPath_; }
        localTarget_ = new LocalTarget(destinationPath.toStdString()); 
    }
    
    if (!localTarget_->beginSession()) {
        updateLog("Error: Could not begin backup session with LocalTarget.");
        QMessageBox::critical(this, tr("Backup Failed"), tr("Could not begin backup session. Check logs."));
        return;
    }

    QDir dir(sourcePath);
    if (!dir.exists()) {
        updateLog(QString("Error: Source directory '%1' does not exist.").arg(sourcePath));
        localTarget_->endSession();
        QMessageBox::critical(this, tr("Backup Failed"), tr("Source directory not found."));
        return;
    }

    dir.setFilter(QDir::Files | QDir::NoSymLinks | QDir::Readable);
    QFileInfoList list = dir.entryInfoList();
    updateLog(QString("Found %1 file(s) in the root of source directory for backup.").arg(list.size()));

    bool all_ok = true;
    for (const QFileInfo &fileInfo : list) {
        std::string sourceFilePath = fileInfo.absoluteFilePath().toStdString();
        if (localTarget_->sendFile(sourceFilePath, "")) { // Metadata is empty string for M1
            updateLog(QString("Backed up: %1").arg(fileInfo.fileName()));
        } else {
            updateLog(QString("Error backing up: %1").arg(fileInfo.fileName()));
            all_ok = false;
        }
    }

    if (!localTarget_->endSession()) {
        updateLog("Error: Could not properly end backup session with LocalTarget.");
    }

    if (all_ok && list.isEmpty() && !dir.exists()) { // Check if source became invalid during op
         updateLog("Backup may have issues: Source directory seems empty or became inaccessible.");
         QMessageBox::warning(this, tr("Backup Note"), tr("Backup process ran, but source directory is empty or no files were processed."));
    } else if (all_ok) {
        updateLog("Backup process completed successfully.");
        QMessageBox::information(this, tr("Backup Complete"), tr("Backup completed successfully."));
    } else {
        updateLog("Backup process completed with some errors. Please check the log.");
        QMessageBox::warning(this, tr("Backup Complete"), tr("Backup completed with some errors."));
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
