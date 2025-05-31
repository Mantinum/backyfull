#include "gui/MainWindow.h"
#include "core/Scheduler.h"
#include "targets/LocalTarget.h"
#include "targets/SftpTarget.h"     // Added
#include "util/CredentialManager.h" // Added

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout> // Added for sftpSettingsGroupBox_
#include <QWidget>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QTime>
#include <QDir>
#include <QFileInfo> // Added for QFileInfo
#include <QDebug>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QCloseEvent> // Added
#include <filesystem>
#include <functional>
#include <map> // Added for SftpTarget config

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
      backupModeComboBox_(nullptr),
      m_localDestinationGroupBox(nullptr),
      sftpSettingsGroupBox_(nullptr),
      sftpHostLineEdit_(nullptr),
      sftpPortLineEdit_(nullptr),
      sftpUsernameLineEdit_(nullptr),
      sftpPasswordLineEdit_(nullptr),
      sftpRemotePathLineEdit_(nullptr),
      sftpSavePasswordCheckBox_(nullptr),
      scheduler_(nullptr),
      localTarget_(nullptr),
      sftpTarget_(nullptr),
      m_credentialManager(nullptr),
      fileDialog_(nullptr)
{
    if (QCoreApplication::organizationName().isEmpty()) {
        QCoreApplication::setOrganizationName("BackyFullOrg");
    }
    if (QCoreApplication::applicationName().isEmpty()) {
        QCoreApplication::setApplicationName("BackyFull");
    }
    
    m_credentialManager = std::unique_ptr<CredentialManager>(createPlatformCredentialManager());
    scheduler_ = new Scheduler(QString(), this); 

    setupUI();      // Create UI elements first
    loadSettings(); // Then load settings which might affect UI state (like combo box index)

    // Connect scheduler signals
    connect(scheduler_, &Scheduler::backupTaskTriggered, this, &MainWindow::handleScheduledBackup);
    connect(scheduler_, &Scheduler::taskChanged, this, &MainWindow::onTaskChanged);

    // Set initial UI state based on loaded settings (especially combo box)
    if (backupModeComboBox_) { // backupModeComboBox_ is setup in setupUI()
        onBackupModeChanged(backupModeComboBox_->currentIndex());
    }
    onTaskChanged(); // Reflect scheduler's initial state (which also loads from its own settings)

    updateLog("BackyFull application started.");
    updateLog("Please configure your backup source, destination/SFTP, and schedule.");
}

MainWindow::~MainWindow() {
    // saveSettings(); // Done in closeEvent
    delete localTarget_; 
    localTarget_ = nullptr;
    delete sftpTarget_; 
    sftpTarget_ = nullptr;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUI() {
    setWindowTitle(tr("BackyFull - Backup Configuration"));

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    // Main vertical layout for the central widget
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    // --- Backup Mode Selection ---
    QHBoxLayout *modeLayout = new QHBoxLayout(); // Use QHBoxLayout for label and combobox
    modeLayout->addWidget(new QLabel(tr("Backup Mode:")));
    backupModeComboBox_ = new QComboBox();
    backupModeComboBox_->addItem(tr("Local Backup"));
    backupModeComboBox_->addItem(tr("SFTP Backup"));
    modeLayout->addWidget(backupModeComboBox_);
    modeLayout->addStretch(); // Add stretch to push combobox to the left
    mainLayout->addLayout(modeLayout);


    // --- Common Settings (Source Directory) ---
    QGroupBox *sourceGroupBox = new QGroupBox(tr("Source Configuration"));
    QGridLayout *sourceLayout = new QGridLayout(sourceGroupBox);
    sourceLayout->addWidget(new QLabel(tr("Source Directory:")), 0, 0);
    sourceDirEdit_ = new QLineEdit();
    sourceDirEdit_->setReadOnly(true);
    sourceLayout->addWidget(sourceDirEdit_, 0, 1);
    sourceDirButton_ = new QPushButton(tr("Browse..."));
    connect(sourceDirButton_, &QPushButton::clicked, this, &MainWindow::selectSourceDirectory);
    sourceLayout->addWidget(sourceDirButton_, 0, 2);
    mainLayout->addWidget(sourceGroupBox);

    // --- Local Destination Settings ---
    m_localDestinationGroupBox = new QGroupBox(tr("Local Destination Configuration")); // Assign to member
    QGridLayout *localDestLayout = new QGridLayout(m_localDestinationGroupBox);
    localDestLayout->addWidget(new QLabel(tr("Destination Directory (Local):")), 0, 0);
    destinationDirEdit_ = new QLineEdit();
    destinationDirEdit_->setReadOnly(true);
    localDestLayout->addWidget(destinationDirEdit_, 0, 1);
    destinationDirButton_ = new QPushButton(tr("Browse..."));
    connect(destinationDirButton_, &QPushButton::clicked, this, &MainWindow::selectDestinationDirectory);
    localDestLayout->addWidget(destinationDirButton_, 0, 2);
    mainLayout->addWidget(m_localDestinationGroupBox);


    // --- SFTP Settings GroupBox ---
    sftpSettingsGroupBox_ = new QGroupBox(tr("SFTP Configuration"));
    QFormLayout *sftpFormLayout = new QFormLayout(sftpSettingsGroupBox_);

    sftpHostLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Host:")), sftpHostLineEdit_);

    sftpPortLineEdit_ = new QLineEdit();
    sftpPortLineEdit_->setText("22"); // Default port
    sftpPortLineEdit_->setValidator(new QIntValidator(1, 65535, this)); // Basic port validation
    sftpFormLayout->addRow(new QLabel(tr("SFTP Port:")), sftpPortLineEdit_);

    sftpUsernameLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Username:")), sftpUsernameLineEdit_);

    sftpPasswordLineEdit_ = new QLineEdit();
    sftpPasswordLineEdit_->setEchoMode(QLineEdit::Password);
    sftpFormLayout->addRow(new QLabel(tr("SFTP Password:")), sftpPasswordLineEdit_);
    
    sftpSavePasswordCheckBox_ = new QCheckBox(tr("Save password securely"));
    sftpFormLayout->addRow(sftpSavePasswordCheckBox_); // QFormLayout handles checkbox layout well

    sftpRemotePathLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Remote Path:")), sftpRemotePathLineEdit_);
    
    mainLayout->addWidget(sftpSettingsGroupBox_);
    // sftpSettingsGroupBox_->setVisible(false); // Visibility handled by onBackupModeChanged
    connect(backupModeComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onBackupModeChanged);


    // --- Scheduling and Controls ---
    QGroupBox *scheduleGroupBox = new QGroupBox(tr("Scheduling & Controls"));
    QGridLayout *scheduleLayout = new QGridLayout(scheduleGroupBox);

    scheduleLayout->addWidget(new QLabel(tr("Daily Backup Time:")), 0, 0);
    backupTimeEdit_ = new QTimeEdit();
    backupTimeEdit_->setDisplayFormat("HH:mm");
    scheduleLayout->addWidget(backupTimeEdit_, 0, 1, 1, 2); // Span 2 columns for time edit

    applyScheduleButton_ = new QPushButton(tr("Apply Schedule"));
    connect(applyScheduleButton_, &QPushButton::clicked, this, &MainWindow::applySchedule);
    scheduleLayout->addWidget(applyScheduleButton_, 1, 0, 1, 3);

    runBackupButton_ = new QPushButton(tr("Run Backup Now"));
    connect(runBackupButton_, &QPushButton::clicked, this, &MainWindow::runBackupNow);
    scheduleLayout->addWidget(runBackupButton_, 2, 0, 1, 3);
    mainLayout->addWidget(scheduleGroupBox);

    // --- Log Display ---
    mainLayout->addWidget(new QLabel(tr("Logs:")));
    logDisplay_ = new QTextEdit();
    logDisplay_->setReadOnly(true);
    mainLayout->addWidget(logDisplay_);
    mainLayout->setStretchFactor(logDisplay_, 1); // Make log display expand

    fileDialog_ = new QFileDialog(this); // Keep this for browse dialogs
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
    QTime backupTime = backupTimeEdit_->time();

    if (sourcePath.isEmpty()) {
        QMessageBox::warning(this, tr("Configuration Error"), tr("Source path cannot be empty."));
        updateLog("Error: Failed to apply schedule. Source path empty.");
        return;
    }
     if (!backupTime.isValid()) {
        QMessageBox::warning(this, tr("Configuration Error"), tr("The selected backup time is invalid."));
        updateLog("Error: Failed to apply schedule. Invalid time.");
        return;
    }

    QString effectiveDestPath; // Used for scheduler and logging
    bool sftpMode = (backupModeComboBox_->currentIndex() == 1); // 0 for Local, 1 for SFTP

    if (sftpMode) {
        if (sftpHostLineEdit_->text().isEmpty() || sftpUsernameLineEdit_->text().isEmpty() || sftpRemotePathLineEdit_->text().isEmpty()) {
             QMessageBox::warning(this, tr("Configuration Error"), tr("SFTP Host, Username, and Remote Path cannot be empty for SFTP mode."));
             updateLog("Error: Failed to apply schedule for SFTP. Required fields missing.");
             return;
        }
        // For SFTP, the "destination path" for the scheduler is more conceptual.
        // It combines host and remote path for uniqueness if needed by scheduler logic.
        effectiveDestPath = QString("sftp://%1%2").arg(sftpHostLineEdit_->text(), sftpRemotePathLineEdit_->text());
        
        // CredentialManager interaction during "Apply Schedule" mirrors saveSettings logic
        if (m_credentialManager) {
            QString serviceName = QString("sftp_%1_%2").arg(sftpHostLineEdit_->text()).arg(sftpPortLineEdit_->text().toInt());
            QString qUsername = sftpUsernameLineEdit_->text();
            QString qPassword = sftpPasswordLineEdit_->text();
            if (sftpSavePasswordCheckBox_->isChecked()) {
                if (!qPassword.isEmpty()) {
                    updateLog("SFTP Schedule: Storing password via CredentialManager.");
                    m_credentialManager->storeSecret(serviceName, qUsername, qPassword);
                }
            } else {
                updateLog("SFTP Schedule: 'Save Password' not checked. Removing any stored password.");
                m_credentialManager->deleteSecret(serviceName, qUsername);
            }
        }
    } else { // Local Backup
        effectiveDestPath = destinationDirEdit_->text();
        if (effectiveDestPath.isEmpty()) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Destination path cannot be empty for local backup."));
            updateLog("Error: Failed to apply schedule for Local Backup. Destination path empty.");
            return;
        }
    }

    // Update scheduler; scheduler needs to know if it's an SFTP task for its persistence.
    if (sftpMode) {
        scheduler_->setDailyBackupTask(sourcePath, effectiveDestPath, backupTime, true, 
                                       sftpMode, 
                                       sftpHostLineEdit_->text(), 
                                       sftpPortLineEdit_->text().toInt(), 
                                       sftpUsernameLineEdit_->text(), 
                                       sftpRemotePathLineEdit_->text());
    } else {
        scheduler_->setDailyBackupTask(sourcePath, effectiveDestPath, backupTime, true, 
                                       sftpMode); // Pass default/empty SFTP params
    }
    updateLog(QString("Schedule applied: Backup at %1 for source '%2' to '%3' (SFTP Mode: %4).")
                  .arg(backupTime.toString("HH:mm"), sourcePath, effectiveDestPath, sftpMode ? "Yes" : "No"));
    QMessageBox::information(this, tr("Schedule Updated"), tr("Backup schedule has been updated and saved."));
}

void MainWindow::runBackupNow() {
    QString sourcePath = sourceDirEdit_->text();
    if (sourcePath.isEmpty()) {
        QMessageBox::warning(this, tr("Backup Error"), tr("Source path must be configured."));
        updateLog("Error: Manual backup failed. Source path empty.");
        return;
    }

    bool sftpMode = (backupModeComboBox_->currentIndex() == 1);
    IStorageTarget* currentTarget = nullptr;

    delete localTarget_; localTarget_ = nullptr; // Clear previous targets
    delete sftpTarget_; sftpTarget_ = nullptr;

    if (sftpMode) {
        updateLog("SFTP Mode selected for manual backup.");
        if (sftpHostLineEdit_->text().isEmpty() || sftpUsernameLineEdit_->text().isEmpty() || sftpRemotePathLineEdit_->text().isEmpty()) {
             QMessageBox::warning(this, tr("Backup Error"), tr("SFTP Host, Username, and Remote Path must be configured."));
             updateLog("Error: Manual SFTP backup failed. Required SFTP fields missing.");
             return;
        }

        std::map<std::string, std::string> sftpConfig;
        sftpConfig["host"] = sftpHostLineEdit_->text().toStdString();
        sftpConfig["port"] = sftpPortLineEdit_->text().toStdString();
        sftpConfig["username"] = sftpUsernameLineEdit_->text().toStdString();
        sftpConfig["remoteBasePath"] = sftpRemotePathLineEdit_->text().toStdString();

        QString qPasswordFromField = sftpPasswordLineEdit_->text();
        if (!qPasswordFromField.isEmpty()) {
            sftpConfig["password"] = qPasswordFromField.toStdString();
            updateLog("SFTP: Using password from field for this session.");
        }
        // SftpTarget's constructor handles CredentialManager interaction based on whether "password" is in its config.
        // If "password" is in config, SftpTarget's CredentialManager logic will store it if it's configured to do so (which it is).
        // If "password" is NOT in config, SftpTarget's CredentialManager logic will try to retrieve it.
        // The sftpSavePasswordCheckBox_ primarily influences what *MainWindow* saves via its own QSettings and CredentialManager calls,
        // SftpTarget itself has its own internal logic for when it receives a password.

        sftpTarget_ = new SftpTarget(sftpConfig); // SftpTarget handles its own credential manager logic internally now
        currentTarget = sftpTarget_;
        updateLog(QString("Manual SFTP backup started: From '%1' to host '%2'.").arg(sourcePath, QString::fromStdString(sftpConfig["host"])));

    } else { // Local Backup
        updateLog("Local Mode selected for manual backup.");
        QString destPath = destinationDirEdit_->text();
        if (destPath.isEmpty()) {
            QMessageBox::warning(this, tr("Backup Error"), tr("Destination path must be configured for local backup."));
            updateLog("Error: Manual backup failed. Local destination path empty.");
            return;
        }
        
        std::filesystem::path fsSourcePath(sourcePath.toStdString());
        std::filesystem::path fsDestinationPathFromUI(destPath.toStdString());
        std::filesystem::path backupRootForTarget = fsDestinationPathFromUI / fsSourcePath.filename();
        
        localTarget_ = new LocalTarget(backupRootForTarget.string());
        currentTarget = localTarget_;
        updateLog(QString("Manual Local backup started: From '%1' to '%2'.").arg(sourcePath, destPath));
    }

    if (!currentTarget) {
        QMessageBox::critical(this, tr("Backup Error"), tr("Failed to initialize backup target."));
        updateLog("Error: Manual backup failed. Target initialization failed.");
        return;
    }
    
    performBackupInternal(sourcePath, currentTarget);
}

// Renamed from performBackup and takes IStorageTarget*
void MainWindow::performBackupInternal(const QString& sourcePath, IStorageTarget* target) {
    if (!target) {
        updateLog("Error: performBackupInternal called with null target.");
        QMessageBox::critical(this, tr("Backup Failed"), tr("Internal error: Backup target not specified."));
        return;
    }
    updateLog(QString("Performing backup using active target: Source: %1").arg(sourcePath));

    std::filesystem::path fsSourcePath(sourcePath.toStdString());

    // Define baseDirForLambda for relative path calculation
    QFileInfo initialSourceInfo(QString::fromStdString(fsSourcePath.string()));
    QString baseDirForLambda = initialSourceInfo.absolutePath();
    // Example: sourcePath = /tmp/foo, baseDirForLambda = /tmp
    //          sourcePath = /foo (root folder), baseDirForLambda = /

    if (!std::filesystem::is_directory(fsSourcePath)) {
         updateLog(QString("Error: Source path '%1' is not a directory.").arg(sourcePath));
         QMessageBox::critical(this, tr("Backup Failed"), tr("Source path is not a directory."));
         return;
    }
    
    if (!target->beginSession()) {
        updateLog(QString("Error: Could not begin backup session with target. Check target logs."));
        QMessageBox::critical(this, tr("Backup Failed"), tr("Could not begin backup session. Check logs."));
        return;
    }

    if (!std::filesystem::exists(fsSourcePath)) {
        updateLog(QString("Error: Source directory '%1' does not exist.").arg(sourcePath));
        target->endSession(); // Attempt to clean up session
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
               // IMPORTANT: SftpTarget expects remoteRelativePath as second argument to sendFile.
               // LocalTarget also expects relativePath.
               // OLD:
               // std::filesystem::path relativePathFs = std::filesystem::relative(fullEntryPath, fsSourcePath);
               // std::string relativePathStr = relativePathFs.generic_string();

               // NEW (refined):
               QString qFullEntryPath_new = QString::fromStdString(fullEntryPath.string());
               // baseDirForLambda is defined outside lambda as parent of initial sourcePath
               // e.g. sourcePath = /tmp/foo, baseDirForLambda = /tmp
               //      qFullEntryPath_new = /tmp/foo/file.txt
               //      baseDirForLambda.length() = 4
               //      qFullEntryPath_new.mid(5) results in "foo/file.txt"
               // if sourcePath = /foo (root level folder), baseDirForLambda = /
               //      qFullEntryPath_new = /foo/file.txt
               //      baseDirForLambda.length() = 1
               //      qFullEntryPath_new.mid(1) results in "foo/file.txt" (if baseDirForLambda == "/")
               std::string relativePathStr = qFullEntryPath_new.mid(baseDirForLambda.length() + (baseDirForLambda == "/" ? 0 : 1)).toStdString();

               if (entry.is_directory()) {
                   updateLog(QString("Scanning subdirectory: %1").arg(QString::fromStdString(relativePathStr)));
                   recursiveCopy(fullEntryPath); 
               } else if (entry.is_regular_file()) {
                   files_processed_count++;
                   if (target->sendFile(fullEntryPath.string(), relativePathStr)) { // Pass correct paths
                       updateLog(QString("Backed up: %1").arg(QString::fromStdString(relativePathStr)));
                   } else {
                       updateLog(QString("Error backing up: %1. Check target logs.").arg(QString::fromStdString(relativePathStr)));
                       all_ok = false; 
                   }
               }
           }
       } catch (const std::filesystem::filesystem_error& e) {
            updateLog(QString("Error accessing path %1: %2").arg(QString::fromStdString(currentPathInSource.string()), QString::fromStdString(e.what())));
            all_ok = false;
       }
    };

    recursiveCopy(fsSourcePath); 

    if (!target->endSession()) {
        updateLog("Error: Could not properly end backup session with target.");
        all_ok = false; 
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
    
    // If scheduler was in SFTP mode, reflect this in UI.
    // Scheduler's destinationPath might be the composite SFTP path or just the local path.
    if (scheduler_->isSftpMode()) {
        backupModeComboBox_->setCurrentText(tr("SFTP Backup")); // Or find by index
        // SFTP fields (host, port, etc.) are loaded from their own QSettings group.
        // destinationDirEdit_ (for local) should be empty or reflect last local path.
        // The actual SFTP "destination" is composed from SFTP fields.
    } else {
        backupModeComboBox_->setCurrentText(tr("Local Backup"));
        destinationDirEdit_->setText(scheduler_->destinationPath());
    }
    
    if (scheduler_->scheduledTime().isValid()) {
        backupTimeEdit_->setTime(scheduler_->scheduledTime());
    } else {
        // Provide a default time if scheduler's time is not valid (e.g., first run)
        backupTimeEdit_->setTime(QTime(23, 0)); 
    }
    // Ensure UI visibility matches the potentially updated mode from scheduler
    onBackupModeChanged(backupModeComboBox_->currentIndex());
    updateLog("Task details updated in UI from Scheduler state.");
}

void MainWindow::onBackupModeChanged(int index) {
    bool sftpSelected = (backupModeComboBox_->itemText(index) == tr("SFTP Backup"));

    if (m_localDestinationGroupBox) m_localDestinationGroupBox->setVisible(!sftpSelected);
    if (sftpSettingsGroupBox_) sftpSettingsGroupBox_->setVisible(sftpSelected);
    
    updateLog(QString("Backup mode changed. SFTP Selected: %1").arg(sftpSelected ? "Yes" : "No"));
}

void MainWindow::loadSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");

    const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(800, 700); // Adjusted default size for more fields
    }
    
    // Load backup mode. Scheduler also loads its own settings including source/dest/time/sftpMode.
    // MainWindow settings primarily drive the UI elements not directly tied to a single scheduled task.
    backupModeComboBox_->setCurrentIndex(settings.value("backupModeIndex", 0).toInt());
    // sourceDirEdit_->setText(settings.value("lastSourcePath", "").toString()); // Or let Scheduler handle this
    // destinationDirEdit_->setText(settings.value("lastLocalDestPath", "").toString()); // For local mode

    settings.endGroup(); // MainWindow group

    settings.beginGroup("SFTP");
    sftpHostLineEdit_->setText(settings.value("host", "").toString());
    sftpPortLineEdit_->setText(settings.value("port", "22").toString());
    sftpUsernameLineEdit_->setText(settings.value("username", "").toString());
    sftpRemotePathLineEdit_->setText(settings.value("remotePath", "").toString());
    sftpSavePasswordCheckBox_->setChecked(settings.value("savePassword", false).toBool());
    // Password itself is NOT loaded into sftpPasswordLineEdit_ from QSettings.
    // SftpTarget's constructor will attempt to retrieve it from CredentialManager if not provided.
    settings.endGroup(); // SFTP group
    
    updateLog("Settings loaded.");
    // onBackupModeChanged() will be called after this from constructor to set initial visibility
}

void MainWindow::saveSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("backupModeIndex", backupModeComboBox_->currentIndex());
    // settings.setValue("lastSourcePath", sourceDirEdit_->text()); // Scheduler handles task specific paths
    // settings.setValue("lastLocalDestPath", destinationDirEdit_->text());
    settings.endGroup(); // MainWindow group

    settings.beginGroup("SFTP");
    settings.setValue("host", sftpHostLineEdit_->text());
    settings.setValue("port", sftpPortLineEdit_->text());
    settings.setValue("username", sftpUsernameLineEdit_->text());
    settings.setValue("remotePath", sftpRemotePathLineEdit_->text());
    settings.setValue("savePassword", sftpSavePasswordCheckBox_->isChecked());
    settings.endGroup(); // SFTP group

    // Handle credential storage based on current SFTP UI state if SFTP mode is active
    if (backupModeComboBox_->currentIndex() == 1) { // SFTP Mode
        QString host = sftpHostLineEdit_->text();
        QString port = sftpPortLineEdit_->text();
        QString username = sftpUsernameLineEdit_->text();
        QString passwordInField = sftpPasswordLineEdit_->text();

        if (!host.isEmpty() && !port.isEmpty() && !username.isEmpty() && m_credentialManager) {
            QString serviceName = QString("sftp_%1_%2").arg(host).arg(port.toInt());
            if (sftpSavePasswordCheckBox_->isChecked()) {
                // Only store/update if password field is not empty.
                // If field is empty, user might want to rely on previously stored password.
                if (!passwordInField.isEmpty()) {
                    updateLog("Saving SFTP password to CredentialManager due to 'Save Password' checked and password field non-empty.");
                    m_credentialManager->storeSecret(serviceName, username, passwordInField);
                } else {
                    updateLog("SFTP 'Save Password' is checked, but password field is empty. No change to stored password at this time.");
                }
            } else { 
                // If "Save Password" is not checked, delete any existing password for this service/user.
                updateLog("SFTP 'Save Password' is not checked. Removing any stored password from CredentialManager for this service.");
                m_credentialManager->deleteSecret(serviceName, username);
            }
        }
    }
    updateLog("Settings saved.");
}

void MainWindow::handleScheduledBackup(const QString& sourcePath, const QString& destinationOrIdentifier) {
    updateLog(QString("Scheduled backup triggered by Scheduler: Source '%1', Dest/ID '%2'")
                  .arg(sourcePath, destinationOrIdentifier));

    bool sftpMode = scheduler_->isSftpMode();
    IStorageTarget* currentTarget = nullptr;

    delete localTarget_; localTarget_ = nullptr;
    delete sftpTarget_; sftpTarget_ = nullptr;

    if (sftpMode) {
        updateLog("Scheduled task is SFTP Mode.");
        std::map<std::string, std::string> sftpConfig;
        sftpConfig["host"] = scheduler_->sftpHost().toStdString();
        sftpConfig["port"] = QString::number(scheduler_->sftpPort()).toStdString();
        sftpConfig["username"] = scheduler_->sftpUsername().toStdString();
        sftpConfig["remoteBasePath"] = scheduler_->sftpRemotePath().toStdString();
        // Password is NOT included here; SftpTarget will use CredentialManager to retrieve it.
        
        sftpTarget_ = new SftpTarget(sftpConfig);
        currentTarget = sftpTarget_;
        updateLog(QString("Scheduled SFTP backup: From '%1' to host '%2'")
                      .arg(sourcePath, scheduler_->sftpHost()));
    } else {
        updateLog("Scheduled task is Local Mode.");
        // destinationOrIdentifier is the actual local destination path for local mode
        if (destinationOrIdentifier.isEmpty()) {
            updateLog("Error: Scheduled local backup triggered but destination path is empty in Scheduler.");
            QMessageBox::critical(this, tr("Scheduled Backup Error"), tr("Destination path for scheduled local backup is missing."));
            return;
        }
        
        std::filesystem::path fsSourcePath(sourcePath.toStdString());
        std::filesystem::path fsDestinationPathFromScheduler(destinationOrIdentifier.toStdString());
        // For local target, destinationPath in scheduler is the root backup dir (e.g. /path/to/backup_folder/SourceDirName)
        // So, LocalTarget constructor just needs this path.
        localTarget_ = new LocalTarget(fsDestinationPathFromScheduler.string());
        currentTarget = localTarget_;
        updateLog(QString("Scheduled Local backup: From '%1' to '%2'")
                      .arg(sourcePath, destinationOrIdentifier));
    }

    if (!currentTarget) {
        QMessageBox::critical(this, tr("Scheduled Backup Error"), tr("Failed to initialize backup target for scheduled task."));
        updateLog("Error: Scheduled backup failed. Target initialization failed.");
        return;
    }
    
    performBackupInternal(sourcePath, currentTarget);
}
