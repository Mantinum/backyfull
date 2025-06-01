#include "gui/MainWindow.h"
#include "core/Scheduler.h"
#include "targets/LocalTarget.h"
#include "targets/SftpTarget.h"
#include "targets/GcsTarget.h"      // Added for GCS Target
#include "util/CredentialManager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QWidget>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QTime>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QSettings>
#include <QCoreApplication>
#include <QCloseEvent>
#include <filesystem>
#include <functional>
#include <map>

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
      gcsSettingsGroupBox_(nullptr),
      gcsBucketNameLineEdit_(nullptr),
      gcsAccountIdentifierLineEdit_(nullptr),
      gcsConnectButton_(nullptr),
      gcsTestConnectionButton_(nullptr), // Initialize new GCS Test Connection button
      gcsAuthStatusLabel_(nullptr),
      scheduler_(nullptr),
      localTarget_(nullptr),
      sftpTarget_(nullptr),
      gcsTarget_(nullptr),            // Initialize GCS target member
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

    setupUI();
    loadSettings();

    connect(scheduler_, &Scheduler::backupTaskTriggered, this, &MainWindow::handleScheduledBackup);
    connect(scheduler_, &Scheduler::taskChanged, this, &MainWindow::onTaskChanged);

    if (backupModeComboBox_) {
        onBackupModeChanged(backupModeComboBox_->currentIndex());
    }
    onTaskChanged();

    updateLog("BackyFull application started.");
    updateLog("Please configure your backup source, destination/SFTP, and schedule.");
}

MainWindow::~MainWindow() {
    delete localTarget_; 
    localTarget_ = nullptr;
    delete sftpTarget_; 
    sftpTarget_ = nullptr;
    delete gcsTarget_;
    gcsTarget_ = nullptr;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::setupUI() {
    setWindowTitle(tr("BackyFull - Backup Configuration"));

    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    
    QHBoxLayout *modeLayout = new QHBoxLayout();
    modeLayout->addWidget(new QLabel(tr("Backup Mode:")));
    backupModeComboBox_ = new QComboBox();
    backupModeComboBox_->addItem(tr("Local Backup"));
    backupModeComboBox_->addItem(tr("SFTP Backup"));
    backupModeComboBox_->addItem(tr("Google Cloud Storage"));
    modeLayout->addWidget(backupModeComboBox_);
    modeLayout->addStretch();
    mainLayout->addLayout(modeLayout);

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

    m_localDestinationGroupBox = new QGroupBox(tr("Local Destination Configuration"));
    QGridLayout *localDestLayout = new QGridLayout(m_localDestinationGroupBox);
    localDestLayout->addWidget(new QLabel(tr("Destination Directory (Local):")), 0, 0);
    destinationDirEdit_ = new QLineEdit();
    destinationDirEdit_->setReadOnly(true);
    localDestLayout->addWidget(destinationDirEdit_, 0, 1);
    destinationDirButton_ = new QPushButton(tr("Browse..."));
    connect(destinationDirButton_, &QPushButton::clicked, this, &MainWindow::selectDestinationDirectory);
    localDestLayout->addWidget(destinationDirButton_, 0, 2);
    mainLayout->addWidget(m_localDestinationGroupBox);

    sftpSettingsGroupBox_ = new QGroupBox(tr("SFTP Configuration"));
    QFormLayout *sftpFormLayout = new QFormLayout(sftpSettingsGroupBox_);
    sftpHostLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Host:")), sftpHostLineEdit_);
    sftpPortLineEdit_ = new QLineEdit();
    sftpPortLineEdit_->setText("22");
    sftpPortLineEdit_->setValidator(new QIntValidator(1, 65535, this));
    sftpFormLayout->addRow(new QLabel(tr("SFTP Port:")), sftpPortLineEdit_);
    sftpUsernameLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Username:")), sftpUsernameLineEdit_);
    sftpPasswordLineEdit_ = new QLineEdit();
    sftpPasswordLineEdit_->setEchoMode(QLineEdit::Password);
    sftpFormLayout->addRow(new QLabel(tr("SFTP Password:")), sftpPasswordLineEdit_);
    sftpSavePasswordCheckBox_ = new QCheckBox(tr("Save password securely"));
    sftpFormLayout->addRow(sftpSavePasswordCheckBox_);
    sftpRemotePathLineEdit_ = new QLineEdit();
    sftpFormLayout->addRow(new QLabel(tr("SFTP Remote Path:")), sftpRemotePathLineEdit_);
    mainLayout->addWidget(sftpSettingsGroupBox_);

    gcsSettingsGroupBox_ = new QGroupBox(tr("Google Cloud Storage Configuration"));
    QFormLayout *gcsFormLayout = new QFormLayout(gcsSettingsGroupBox_);
    gcsBucketNameLineEdit_ = new QLineEdit();
    gcsFormLayout->addRow(new QLabel(tr("GCS Bucket Name:")), gcsBucketNameLineEdit_);
    gcsAccountIdentifierLineEdit_ = new QLineEdit();
    gcsFormLayout->addRow(new QLabel(tr("GCS Account Identifier:")), gcsAccountIdentifierLineEdit_);
    gcsConnectButton_ = new QPushButton(tr("Connect to Google Account"));
    gcsFormLayout->addRow(gcsConnectButton_);
    connect(gcsConnectButton_, &QPushButton::clicked, this, &MainWindow::onGcsConnectButtonClicked);
    gcsAuthStatusLabel_ = new QLabel(tr("Status: Not Authenticated"));
    gcsFormLayout->addRow(gcsAuthStatusLabel_);
    gcsTestConnectionButton_ = new QPushButton(tr("Test Connection")); // Instantiate and add
    gcsFormLayout->addRow(gcsTestConnectionButton_);
    connect(gcsTestConnectionButton_, &QPushButton::clicked, this, &MainWindow::onGcsTestConnectionClicked);
    mainLayout->addWidget(gcsSettingsGroupBox_);

    connect(backupModeComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onBackupModeChanged);

    QGroupBox *scheduleGroupBox = new QGroupBox(tr("Scheduling & Controls"));
    QGridLayout *scheduleLayout = new QGridLayout(scheduleGroupBox);
    scheduleLayout->addWidget(new QLabel(tr("Daily Backup Time:")), 0, 0);
    backupTimeEdit_ = new QTimeEdit();
    backupTimeEdit_->setDisplayFormat("HH:mm");
    scheduleLayout->addWidget(backupTimeEdit_, 0, 1, 1, 2);
    applyScheduleButton_ = new QPushButton(tr("Apply Schedule"));
    connect(applyScheduleButton_, &QPushButton::clicked, this, &MainWindow::applySchedule);
    scheduleLayout->addWidget(applyScheduleButton_, 1, 0, 1, 3);
    runBackupButton_ = new QPushButton(tr("Run Backup Now"));
    connect(runBackupButton_, &QPushButton::clicked, this, &MainWindow::runBackupNow);
    scheduleLayout->addWidget(runBackupButton_, 2, 0, 1, 3);
    mainLayout->addWidget(scheduleGroupBox);

    mainLayout->addWidget(new QLabel(tr("Logs:")));
    logDisplay_ = new QTextEdit();
    logDisplay_->setReadOnly(true);
    mainLayout->addWidget(logDisplay_);
    mainLayout->setStretchFactor(logDisplay_, 1);

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

    QString effectiveDestPathOrIdentifier;
    QString currentModeText = backupModeComboBox_->currentText();
    bool localMode = (currentModeText == tr("Local Backup"));
    bool sftpMode = (currentModeText == tr("SFTP Backup"));
    bool gcsMode = (currentModeText == tr("Google Cloud Storage"));

    if (localMode) {
        effectiveDestPathOrIdentifier = destinationDirEdit_->text();
        if (effectiveDestPathOrIdentifier.isEmpty()) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("Destination path cannot be empty for local backup."));
            updateLog("Error: Failed to apply schedule for Local Backup. Destination path empty.");
            return;
        }
        scheduler_->setDailyBackupTask(sourcePath, effectiveDestPathOrIdentifier, backupTime, true,
                                       false, QString(),      // sftpHost is QString
                                       0, QString(), QString(), false, // isGcsMode is bool
                                       QString(), QString());
        updateLog(QString("Schedule applied for Local Backup: %1 to %2 at %3")
                      .arg(sourcePath, effectiveDestPathOrIdentifier, backupTime.toString("HH:mm")));

    } else if (sftpMode) {
        if (sftpHostLineEdit_->text().isEmpty() || sftpUsernameLineEdit_->text().isEmpty() || sftpRemotePathLineEdit_->text().isEmpty()) {
             QMessageBox::warning(this, tr("Configuration Error"), tr("SFTP Host, Username, and Remote Path cannot be empty for SFTP mode."));
             updateLog("Error: Failed to apply schedule for SFTP. Required fields missing.");
             return;
        }
        effectiveDestPathOrIdentifier = QString("sftp://%1%2").arg(sftpHostLineEdit_->text(), sftpRemotePathLineEdit_->text());
        
        if (m_credentialManager) {
            QString serviceName = QString("sftp_%1_%2").arg(sftpHostLineEdit_->text()).arg(sftpPortLineEdit_->text().toInt());
            QString qUsername = sftpUsernameLineEdit_->text();
            QString qPassword = sftpPasswordLineEdit_->text();
            if (sftpSavePasswordCheckBox_->isChecked()) {
                if (!qPassword.isEmpty()) m_credentialManager->storeSecret(serviceName, qUsername, qPassword);
            } else {
                m_credentialManager->deleteSecret(serviceName, qUsername);
            }
        }
        scheduler_->setDailyBackupTask(sourcePath, effectiveDestPathOrIdentifier, backupTime, true,
                                       true, sftpHostLineEdit_->text(), // sftpHost
                                       sftpPortLineEdit_->text().toInt(), 
                                       sftpUsernameLineEdit_->text(), 
                                       sftpRemotePathLineEdit_->text(),
                                       false, QString(), QString()); // isGcsMode, gcsBucketName, gcsObjectPrefix
        updateLog(QString("Schedule applied for SFTP Backup: %1 to %2 at %3")
                      .arg(sourcePath, effectiveDestPathOrIdentifier, backupTime.toString("HH:mm")));

    } else if (gcsMode) {
        QString bucketName = gcsBucketNameLineEdit_->text();
        QString accountId = gcsAccountIdentifierLineEdit_->text();
        if (bucketName.isEmpty() || accountId.isEmpty()) {
            QMessageBox::warning(this, tr("Configuration Error"), tr("GCS Bucket Name and Account Identifier cannot be empty for GCS mode."));
            updateLog("Error: Failed to apply schedule for GCS. Bucket Name or Account ID missing.");
            return;
        }
        effectiveDestPathOrIdentifier = QString("gcs://%1").arg(bucketName);
        scheduler_->setDailyBackupTask(sourcePath, effectiveDestPathOrIdentifier, backupTime, true,
                                       false, QString(),      // sftpHost is QString
                                       0, QString(), QString(), true, // isGcsMode is bool
                                       bucketName, accountId);
        updateLog(QString("Schedule applied for GCS Backup: %1 to bucket '%2' (Account: %3) at %4")
                      .arg(sourcePath, bucketName, accountId, backupTime.toString("HH:mm")));
    } else {
        QMessageBox::critical(this, tr("Internal Error"), tr("Unknown backup mode selected."));
        updateLog("Error: Apply schedule failed. Unknown backup mode.");
        return;
    }

    QMessageBox::information(this, tr("Schedule Updated"), tr("Backup schedule has been updated and saved."));
}

void MainWindow::runBackupNow() {
    QString sourcePath = sourceDirEdit_->text();
    if (sourcePath.isEmpty()) {
        QMessageBox::warning(this, tr("Backup Error"), tr("Source path must be configured."));
        updateLog("Error: Manual backup failed. Source path empty.");
        return;
    }

    QString currentModeText = backupModeComboBox_->currentText();
    bool localMode = (currentModeText == tr("Local Backup"));
    bool sftpMode = (currentModeText == tr("SFTP Backup"));
    bool gcsMode = (currentModeText == tr("Google Cloud Storage"));

    IStorageTarget* currentTarget = nullptr;
    delete localTarget_; localTarget_ = nullptr;
    delete sftpTarget_; sftpTarget_ = nullptr;
    delete gcsTarget_; gcsTarget_ = nullptr;

    if (localMode) {
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

    } else if (sftpMode) {
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
        if (!qPasswordFromField.isEmpty()) sftpConfig["password"] = qPasswordFromField.toStdString();
        sftpTarget_ = new SftpTarget(sftpConfig);
        currentTarget = sftpTarget_;
        updateLog(QString("Manual SFTP backup started: From '%1' to host '%2'.").arg(sourcePath, QString::fromStdString(sftpConfig["host"])));

    } else if (gcsMode) {
        updateLog("GCS Mode selected for manual backup.");
        QString bucketName = gcsBucketNameLineEdit_->text();
        QString accountId = gcsAccountIdentifierLineEdit_->text();
        if (bucketName.isEmpty() || accountId.isEmpty()) {
            QMessageBox::warning(this, tr("Backup Error"), tr("GCS Bucket Name and Account Identifier must be configured."));
            updateLog("Error: Manual GCS backup failed. Bucket Name or Account ID missing.");
            return;
        }
        std::map<std::string, std::string> gcsConfig;
        gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
        gcsConfig["gcs_account_identifier"] = accountId.toStdString();
        gcsConfig["gcs_object_prefix"] = "";

        delete this->gcsTarget_;
        this->gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());
        currentTarget = this->gcsTarget_;
        updateLog(QString("Manual GCS backup started: From '%1' to GCS Bucket '%2' (Account: '%3')")
                      .arg(sourcePath, bucketName, accountId));
    } else {
        QMessageBox::critical(this, tr("Internal Error"), tr("Unknown backup mode selected for manual backup."));
        updateLog("Error: Manual backup failed. Unknown backup mode.");
        return;
    }

    if (!currentTarget) {
        QMessageBox::critical(this, tr("Backup Error"), tr("Failed to initialize backup target."));
        updateLog("Error: Manual backup failed. Target initialization failed.");
        return;
    }
    
    performBackupInternal(sourcePath, currentTarget);
}

void MainWindow::onGcsConnectButtonClicked() {
    updateLog("GCS Connect button clicked.");
    QString bucketName = gcsBucketNameLineEdit_->text();
    QString accountId = gcsAccountIdentifierLineEdit_->text();

    if (bucketName.isEmpty() || accountId.isEmpty()) {
        QMessageBox::warning(this, tr("GCS Configuration Error"),
                             tr("GCS Bucket Name and Account Identifier must be provided before connecting."));
        updateLog("GCS Connect: Bucket Name or Account Identifier missing.");
        return;
    }

    std::map<std::string, std::string> gcsConfig;
    gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
    gcsConfig["gcs_account_identifier"] = accountId.toStdString();

    delete gcsTarget_;
    gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());

    gcsAuthStatusLabel_->setText(tr("Status: Authenticating..."));
    updateLog(QString("GCS Connect: Attempting authentication for account '%1' with bucket '%2'.").arg(accountId, bucketName));

    if (gcsTarget_->initiateOAuthAndStoreToken()) {
        gcsAuthStatusLabel_->setText(tr("Status: Authentication Successful for %1").arg(accountId));
        updateLog(QString("GCS Connect: Authentication successful for account '%1'.").arg(accountId));

        QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
        settings.beginGroup("GCS");
        settings.setValue("gcs_last_authenticated_account", accountId);
        settings.endGroup();
        updateLog(QString("GCS Connect: Stored '%1' as last authenticated account.").arg(accountId));

    } else {
        gcsAuthStatusLabel_->setText(tr("Status: Authentication Failed. Check logs."));
        updateLog(QString("GCS Connect: Authentication failed for account '%1'. Error: %2")
                      .arg(accountId, QString::fromStdString(gcsTarget_->getLastError())));
        QMessageBox::critical(this, tr("GCS Authentication Failed"),
                              tr("Could not authenticate with Google Cloud Storage. Error: %1")
                              .arg(QString::fromStdString(gcsTarget_->getLastError())));
    }
    delete gcsTarget_;
    gcsTarget_ = nullptr;
}

void MainWindow::onGcsTestConnectionClicked() {
    updateLog("GCS Test Connection button clicked.");
    QString bucketName = gcsBucketNameLineEdit_->text();
    QString accountId = gcsAccountIdentifierLineEdit_->text();

    if (bucketName.isEmpty() || accountId.isEmpty()) {
        QMessageBox::warning(this, tr("GCS Configuration Error"),
                             tr("GCS Bucket Name and Account Identifier must be provided before testing connection."));
        updateLog("GCS Test Connection: Bucket Name or Account Identifier missing.");
        return;
    }

    std::map<std::string, std::string> gcsConfig;
    gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
    gcsConfig["gcs_account_identifier"] = accountId.toStdString();

    delete gcsTarget_;
    gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());

    gcsAuthStatusLabel_->setText(tr("Status: Testing Connection..."));
    updateLog(QString("GCS Test Connection: Attempting for account '%1' with bucket '%2'.").arg(accountId, bucketName));

    std::string testErrorMsg;
    if (gcsTarget_->testConnection(testErrorMsg)) {
        QMessageBox::information(this, tr("GCS Connection Test"), tr("Connection Successful!"));
        updateLog(QString("GCS Test Connection: Successful for account '%1'.").arg(accountId));

        QString lastAuthAccount = QSettings().value("GCS/gcs_last_authenticated_account").toString();
        if (!lastAuthAccount.isEmpty() && lastAuthAccount == accountId) {
            gcsAuthStatusLabel_->setText(tr("Status: Authenticated as %1").arg(accountId));
        } else {
            // If test passed but not explicitly "Connected" via button, or token expired and test used a new one implicitly
            gcsAuthStatusLabel_->setText(tr("Status: Connection test passed."));
        }
    } else {
        QMessageBox::warning(this, tr("GCS Connection Test"), tr("Connection Failed: %1").arg(QString::fromStdString(testErrorMsg)));
        updateLog(QString("GCS Test Connection: Failed for account '%1'. Error: %2").arg(accountId, QString::fromStdString(testErrorMsg)));
        if (testErrorMsg.find("OAuth") != std::string::npos || testErrorMsg.find("token") != std::string::npos ||
            testErrorMsg.find("permission") != std::string::npos || testErrorMsg.find("denied") != std::string::npos ||
            testErrorMsg.find("authenticate") != std::string::npos) { // Added generic auth term
            gcsAuthStatusLabel_->setText(tr("Status: Auth/Permission issue during test."));
        } else {
            gcsAuthStatusLabel_->setText(tr("Status: Connection test failed."));
        }
    }

    delete gcsTarget_;
    gcsTarget_ = nullptr;
}

void MainWindow::performBackupInternal(const QString& sourcePath, IStorageTarget* target) {
    if (!target) {
        updateLog("Error: performBackupInternal called with null target.");
        QMessageBox::critical(this, tr("Backup Failed"), tr("Internal error: Backup target not specified."));
        return;
    }
    updateLog(QString("Performing backup using active target: Source: %1").arg(sourcePath));

    std::filesystem::path fsSourcePath(sourcePath.toStdString());

    QFileInfo initialSourceInfo(QString::fromStdString(fsSourcePath.string()));
    QString baseDirForLambda = initialSourceInfo.absolutePath();

    if (!std::filesystem::is_directory(fsSourcePath)) {
         updateLog(QString("Error: Source path '%1' is not a directory.").arg(sourcePath));
         QMessageBox::critical(this, tr("Backup Failed"), tr("Source path is not a directory."));
         return;
    }
    
    if (!target->beginSession()) {
        std::string specificError;
        GcsTarget* currentGcsTarget = dynamic_cast<GcsTarget*>(target);
        if (currentGcsTarget) {
            specificError = currentGcsTarget->getLastError();
        }
        QString errorDetails = QString::fromStdString(specificError);
        if (errorDetails.isEmpty()) errorDetails = tr("Check target logs for more information.");

        updateLog(QString("Error: Could not begin backup session. Target error: %1").arg(errorDetails));
        QMessageBox::critical(this, tr("Backup Failed"), tr("Could not begin backup session. Details: %1").arg(errorDetails));
        return;
    }

    if (!std::filesystem::exists(fsSourcePath)) {
        updateLog(QString("Error: Source directory '%1' does not exist.").arg(sourcePath));
        target->endSession();
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
               QString qFullEntryPath_new = QString::fromStdString(fullEntryPath.string());
               std::string relativePathStr = qFullEntryPath_new.mid(baseDirForLambda.length() + (baseDirForLambda == "/" ? 0 : 1)).toStdString();

               if (entry.is_directory()) {
                   updateLog(QString("Scanning subdirectory: %1").arg(QString::fromStdString(relativePathStr)));
                   recursiveCopy(fullEntryPath); 
               } else if (entry.is_regular_file()) {
                   files_processed_count++;
                   if (target->sendFile(fullEntryPath.string(), relativePathStr)) {
                       updateLog(QString("Backed up: %1").arg(QString::fromStdString(relativePathStr)));
                   } else {
                       std::string specificError;
                       GcsTarget* currentGcsTarget = dynamic_cast<GcsTarget*>(target);
                       if (currentGcsTarget) {
                           specificError = currentGcsTarget->getLastError();
                       }
                       QString errorDetails = QString::fromStdString(specificError);
                       if (errorDetails.isEmpty()) errorDetails = tr("Check target logs for specifics.");

                       updateLog(QString("Error backing up: %1. Target error: %2").arg(QString::fromStdString(relativePathStr), errorDetails));
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
        std::string specificError;
        GcsTarget* currentGcsTarget = dynamic_cast<GcsTarget*>(target);
        if (currentGcsTarget) {
            specificError = currentGcsTarget->getLastError();
        }
        QString errorDetails = QString::fromStdString(specificError);
        if (errorDetails.isEmpty()) errorDetails = tr("Check target logs.");

        updateLog(QString("Error: Could not properly end backup session with target. Target error: %1").arg(errorDetails));
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
    
    if (scheduler_->isGcsMode()) {
        backupModeComboBox_->setCurrentText(tr("Google Cloud Storage"));
        gcsBucketNameLineEdit_->setText(scheduler_->gcsBucketName());
        gcsAccountIdentifierLineEdit_->setText(scheduler_->gcsAccountIdentifier());
    } else if (scheduler_->isSftpMode()) {
        backupModeComboBox_->setCurrentText(tr("SFTP Backup"));
    } else {
        backupModeComboBox_->setCurrentText(tr("Local Backup"));
        destinationDirEdit_->setText(scheduler_->destinationPath());
    }
    
    if (scheduler_->scheduledTime().isValid()) {
        backupTimeEdit_->setTime(scheduler_->scheduledTime());
    } else {
        backupTimeEdit_->setTime(QTime(23, 0)); 
    }
    onBackupModeChanged(backupModeComboBox_->currentIndex());
    updateLog("Task details updated in UI from Scheduler state.");
}

void MainWindow::onBackupModeChanged(int index) {
    QString currentModeText = backupModeComboBox_->itemText(index);
    bool localSelected = (currentModeText == tr("Local Backup"));
    bool sftpSelected = (currentModeText == tr("SFTP Backup"));
    bool gcsSelected = (currentModeText == tr("Google Cloud Storage"));

    if (m_localDestinationGroupBox) m_localDestinationGroupBox->setVisible(localSelected);
    if (sftpSettingsGroupBox_) sftpSettingsGroupBox_->setVisible(sftpSelected);
    if (gcsSettingsGroupBox_) gcsSettingsGroupBox_->setVisible(gcsSelected);
    
    updateLog(QString("Backup mode changed. Local: %1, SFTP: %2, GCS: %3")
                  .arg(localSelected ? "Yes" : "No", sftpSelected ? "Yes" : "No", gcsSelected ? "Yes" : "No"));
}

void MainWindow::loadSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");

    const QByteArray geometry = settings.value("geometry", QByteArray()).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(800, 700);
    }
    
    backupModeComboBox_->setCurrentIndex(settings.value("backupModeIndex", 0).toInt());
    settings.endGroup();

    settings.beginGroup("SFTP");
    sftpHostLineEdit_->setText(settings.value("host", "").toString());
    sftpPortLineEdit_->setText(settings.value("port", "22").toString());
    sftpUsernameLineEdit_->setText(settings.value("username", "").toString());
    sftpRemotePathLineEdit_->setText(settings.value("remotePath", "").toString());
    sftpSavePasswordCheckBox_->setChecked(settings.value("savePassword", false).toBool());
    settings.endGroup();

    settings.beginGroup("GCS");
    gcsBucketNameLineEdit_->setText(settings.value("gcs_bucket_name", "").toString());
    gcsAccountIdentifierLineEdit_->setText(settings.value("gcs_account_identifier", "").toString());
    QString lastAuthAccount = settings.value("gcs_last_authenticated_account", "").toString();
    if (!lastAuthAccount.isEmpty() && lastAuthAccount == gcsAccountIdentifierLineEdit_->text()) {
        gcsAuthStatusLabel_->setText(tr("Status: Authenticated as %1").arg(lastAuthAccount));
    } else {
        gcsAuthStatusLabel_->setText(tr("Status: Not Authenticated"));
    }
    settings.endGroup();
    
    updateLog("Settings loaded.");
}

void MainWindow::saveSettings() {
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("backupModeIndex", backupModeComboBox_->currentIndex());
    settings.endGroup();

    settings.beginGroup("SFTP");
    settings.setValue("host", sftpHostLineEdit_->text());
    settings.setValue("port", sftpPortLineEdit_->text());
    settings.setValue("username", sftpUsernameLineEdit_->text());
    settings.setValue("remotePath", sftpRemotePathLineEdit_->text());
    settings.setValue("savePassword", sftpSavePasswordCheckBox_->isChecked());
    settings.endGroup();

    settings.beginGroup("GCS");
    settings.setValue("gcs_bucket_name", gcsBucketNameLineEdit_->text());
    settings.setValue("gcs_account_identifier", gcsAccountIdentifierLineEdit_->text());
    // gcs_last_authenticated_account is saved only on successful connect
    settings.endGroup();

    if (backupModeComboBox_->currentText() == tr("SFTP Backup")) {
        QString host = sftpHostLineEdit_->text();
        QString port = sftpPortLineEdit_->text();
        QString username = sftpUsernameLineEdit_->text();
        QString passwordInField = sftpPasswordLineEdit_->text();

        if (!host.isEmpty() && !port.isEmpty() && !username.isEmpty() && m_credentialManager) {
            QString serviceName = QString("sftp_%1_%2").arg(host).arg(port.toInt());
            if (sftpSavePasswordCheckBox_->isChecked()) {
                if (!passwordInField.isEmpty()) {
                    m_credentialManager->storeSecret(serviceName, username, passwordInField);
                }
            } else { 
                m_credentialManager->deleteSecret(serviceName, username);
            }
        }
    }
    updateLog("Settings saved.");
}

void MainWindow::handleScheduledBackup(const QString& sourcePath, const QString& destinationOrIdentifier) {
    updateLog(QString("Scheduled backup triggered by Scheduler: Source '%1', Dest/ID '%2'")
                  .arg(sourcePath, destinationOrIdentifier));

    IStorageTarget* currentTarget = nullptr;
    delete localTarget_; localTarget_ = nullptr;
    delete sftpTarget_; sftpTarget_ = nullptr;
    delete gcsTarget_; gcsTarget_ = nullptr;

    if (scheduler_->isGcsMode()) {
        updateLog("Scheduled task is GCS Mode.");
        QString bucketName = scheduler_->gcsBucketName();
        QString accountId = scheduler_->gcsObjectPrefix();

        if (bucketName.isEmpty() || accountId.isEmpty()) {
            updateLog("Error: Scheduled GCS backup but bucket name or account ID (from scheduler's gcsObjectPrefix) is missing in Scheduler.");
            QMessageBox::critical(this, tr("Scheduled Backup Error"), tr("GCS configuration for scheduled backup is incomplete (bucket or account ID)."));
            return;
        }

        std::map<std::string, std::string> gcsConfig;
        gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
        gcsConfig["gcs_account_identifier"] = accountId.toStdString();
        gcsConfig["gcs_object_prefix"] = "";

        delete this->gcsTarget_;
        this->gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());
        currentTarget = this->gcsTarget_;
        updateLog(QString("Scheduled GCS backup started: From '%1' to GCS Bucket '%2' (Account: '%3')")
                      .arg(sourcePath, bucketName, accountId));

    } else if (scheduler_->isSftpMode()) {
        updateLog("Scheduled task is SFTP Mode.");
        std::map<std::string, std::string> sftpConfig;
        sftpConfig["host"] = scheduler_->sftpHost().toStdString();
        sftpConfig["port"] = QString::number(scheduler_->sftpPort()).toStdString();
        sftpConfig["username"] = scheduler_->sftpUsername().toStdString();
        sftpConfig["remoteBasePath"] = scheduler_->sftpRemotePath().toStdString();
        sftpTarget_ = new SftpTarget(sftpConfig);
        currentTarget = sftpTarget_;
        updateLog(QString("Scheduled SFTP backup: From '%1' to host '%2'")
                      .arg(sourcePath, scheduler_->sftpHost()));
    } else {
        updateLog("Scheduled task is Local Mode.");
        if (destinationOrIdentifier.isEmpty()) {
            updateLog("Error: Scheduled local backup triggered but destination path is empty in Scheduler.");
            QMessageBox::critical(this, tr("Scheduled Backup Error"), tr("Destination path for scheduled local backup is missing."));
            return;
        }
        std::filesystem::path fsSourcePath(sourcePath.toStdString());
        std::filesystem::path fsDestinationPathFromScheduler(destinationOrIdentifier.toStdString());
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
