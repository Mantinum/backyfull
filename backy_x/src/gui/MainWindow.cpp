#include "gui/MainWindow.h"
#include "core/Scheduler.h"
#include "targets/GcsTarget.h" // Added for GCS Target
#include "targets/LocalTarget.h"
#include "targets/SftpTarget.h"
#include "util/CredentialManager.h"
#include "gui/FileViewerWidget.h"
#include "gui/WatchManager.h"
#include "gui/MainWindowViewModel.h"
#include "ui_MainWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QTabWidget>
#include <QProgressBar>
#include <QIntValidator>
#include <QSizePolicy>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>
#include <functional>
#include <map>
// Added for File Viewer
#include <QDockWidget>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), sourceDirEdit_(nullptr), sourceDirButton_(nullptr),
      destinationDirEdit_(nullptr), destinationDirButton_(nullptr),
      backupTimeEdit_(nullptr), addTimeButton_(nullptr),
      timeListWidget_(nullptr), removeTimeButton_(nullptr),
      runBackupButton_(nullptr), scheduleSummaryLabel_(nullptr),
      logDisplay_(nullptr), backupModeComboBox_(nullptr),
      backupModeStackedWidget_(nullptr), backupProgressBar_(nullptr),
      m_localDestinationGroupBox(nullptr), sftpSettingsGroupBox_(nullptr),
      sftpHostLineEdit_(nullptr), sftpPortLineEdit_(nullptr),
      sftpUsernameLineEdit_(nullptr), sftpPasswordLineEdit_(nullptr),
      sftpRemotePathLineEdit_(nullptr), sftpSavePasswordCheckBox_(nullptr),
      sftpConnectToggleButton_(nullptr), gcsSettingsGroupBox_(nullptr),
      gcsBucketNameLineEdit_(nullptr), gcsAccountIdentifierLineEdit_(nullptr),
      gcsConnectButton_(nullptr),
      gcsTestConnectionButton_(
          nullptr), // Initialize new GCS Test Connection button
      gcsAuthStatusLabel_(nullptr), gcsConnectToggleButton_(nullptr),
      // File Viewer UI Elements
      fileViewerDockWidget_(nullptr), fileViewerWidget_(nullptr),
      watchToggleCheckBox_(nullptr), watchStatusLabel_(nullptr),
      watchManager_(nullptr),
      // Core components
      scheduler_(nullptr), localTarget_(nullptr), sftpTarget_(nullptr),
      gcsTarget_(nullptr), // Initialize GCS target member
      m_credentialManager(nullptr),
      // Helper for file dialogs
      fileDialog_(nullptr) {
  if (QCoreApplication::organizationName().isEmpty()) {
    QCoreApplication::setOrganizationName("BackyFullOrg");
  }
  if (QCoreApplication::applicationName().isEmpty()) {
    QCoreApplication::setApplicationName("BackyFull");
  }

  m_credentialManager =
      std::unique_ptr<CredentialManager>(createPlatformCredentialManager());
  scheduler_ = new Scheduler(QString(), this);

  watchManager_ = new WatchManager(this);
  connect(watchManager_, &WatchManager::triggered, this,
          &MainWindow::handleWatchTriggered);

  viewModel_ = new MainWindowViewModel(this);

  setupUI();
  loadSettings();

  connect(scheduler_, &Scheduler::backupTaskTriggered, this,
          &MainWindow::handleScheduledBackup);
  connect(scheduler_, &Scheduler::taskChanged, this,
          &MainWindow::onTaskChanged);

  if (backupModeComboBox_ && backupModeStackedWidget_) {
    onBackupModeChanged(backupModeComboBox_->currentIndex());
  }
  onTaskChanged();

  updateLog("BackyFull application started.");
  updateLog(
      "Please configure your backup source, destination/SFTP, and schedule.");
}

MainWindow::~MainWindow() {
  delete ui;
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
  ui->setupUi(this);

  QListWidget *navList = findChild<QListWidget *>("navList");
  QStackedWidget *pages = findChild<QStackedWidget *>("pages");
  if (navList && pages) {
    connect(navList, &QListWidget::currentRowChanged, pages,
            &QStackedWidget::setCurrentIndex);
  }

  fileDialog_ = new QFileDialog(this);
}

void MainWindow::selectSourceDirectory() {
  fileDialog_->setWindowTitle(tr("Select Source Directory"));
  fileDialog_->setFileMode(QFileDialog::Directory);
  fileDialog_->setOption(QFileDialog::ShowDirsOnly, true);
  QString initialPath =
      sourceDirEdit_->text().isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
          : sourceDirEdit_->text();
  QString directory = fileDialog_->getExistingDirectory(
      this, tr("Select Source Directory"), initialPath);
  if (!directory.isEmpty()) {
    sourceDirEdit_->setText(QDir::toNativeSeparators(directory));
    updateLog(QString("Source directory selected: %1").arg(directory));
  }
}

void MainWindow::selectDestinationDirectory() {
  fileDialog_->setWindowTitle(tr("Select Destination Directory"));
  fileDialog_->setFileMode(QFileDialog::Directory);
  fileDialog_->setOption(QFileDialog::ShowDirsOnly, true);
  QString initialPath =
      destinationDirEdit_->text().isEmpty()
          ? QStandardPaths::writableLocation(QStandardPaths::HomeLocation)
          : destinationDirEdit_->text();
  QString directory = fileDialog_->getExistingDirectory(
      this, tr("Select Destination Directory"), initialPath);
  if (!directory.isEmpty()) {
    destinationDirEdit_->setText(QDir::toNativeSeparators(directory));
    updateLog(QString("Destination directory selected: %1").arg(directory));
  }
}

void MainWindow::onAddBackupTimeClicked() {
  QTime t = backupTimeEdit_->time();
  if (!t.isValid())
    return;
  QStringList dayNames;
  QStringList dayNums;
  for (int i = 0; i < dayButtons_.size(); ++i) {
    if (dayButtons_[i]->isChecked()) {
      dayNames << dayButtons_[i]->text();
      dayNums << QString::number(i + 1); // Qt day numbers
    }
  }
  QString dataString = t.toString("HH:mm");
  if (!dayNums.isEmpty())
    dataString += "|" + dayNums.join(',');

  for (int i = 0; i < timeListWidget_->count(); ++i) {
    if (timeListWidget_->item(i)->data(Qt::UserRole).toString() == dataString)
      return; // duplicate
  }

  QString display = t.toString("HH:mm");
  if (!dayNames.isEmpty())
    display += " (" + dayNames.join(',') + ")";
  else
    display += tr(" (All)");
  QString srcDisp = shortenPathForDisplay(sourceDirEdit_->text());
  QString destDisp = shortenPathForDisplay(currentDestinationForDisplay());
  if (!srcDisp.isEmpty() && !destDisp.isEmpty()) {
    display += QString(" | %1 \u2192 %2").arg(srcDisp, destDisp);
  }
  QListWidgetItem *item = new QListWidgetItem(display);
  item->setData(Qt::UserRole, dataString);
  timeListWidget_->addItem(item);
  for (QAbstractButton *btn : dayButtons_)
    btn->setChecked(false);
  updateScheduleFromUI();
}

void MainWindow::onRemoveBackupTimeClicked() {
  const QList<QListWidgetItem *> items = timeListWidget_->selectedItems();
  for (QListWidgetItem *item : items) {
    QString data = item->data(Qt::UserRole).toString();
    if (data.startsWith("WATCH|")) {
      QString path = data.mid(QStringLiteral("WATCH|").length());
      watchManager_->removeEntry(path);
    }
    delete item;
  }
  watchStatusLabel_->setText(
      tr("%1 dossier(s) surveill\u00e9(s)").arg(watchManager_->entries().size()));
  updateScheduleFromUI();
}

void MainWindow::onAddWatchEntry() {
  QString dir = sourceDirEdit_->text();
  if (dir.isEmpty()) {
    QMessageBox::warning(this, tr("Configuration Error"),
                         tr("Source path cannot be empty."));
    return;
  }
  QString modeText = backupModeComboBox_->currentText();
  bool localMode = (modeText == tr("Local Backup"));
  bool sftpMode = (modeText == tr("SFTP Backup"));
  bool gcsMode = (modeText == tr("Google Cloud Storage"));

  WatchEntry entry;
  entry.source = dir;
  if (localMode) {
    entry.destination = destinationDirEdit_->text();
    if (entry.destination.isEmpty()) {
      QMessageBox::warning(this, tr("Configuration Error"),
                           tr("Destination path cannot be empty for local "
                              "backup."));
      return;
    }
  } else if (sftpMode) {
    if (sftpHostLineEdit_->text().isEmpty() ||
        sftpUsernameLineEdit_->text().isEmpty() ||
        sftpRemotePathLineEdit_->text().isEmpty()) {
      QMessageBox::warning(this, tr("Configuration Error"),
                           tr("SFTP Host, Username, and Remote Path cannot be "
                              "empty."));
      return;
    }
    entry.isSftpMode = true;
    entry.sftpHost = sftpHostLineEdit_->text();
    entry.sftpPort = sftpPortLineEdit_->text().toInt();
    entry.sftpUsername = sftpUsernameLineEdit_->text();
    entry.sftpRemotePath = sftpRemotePathLineEdit_->text();
  } else if (gcsMode) {
    if (gcsBucketNameLineEdit_->text().isEmpty() ||
        gcsAccountIdentifierLineEdit_->text().isEmpty()) {
      QMessageBox::warning(this, tr("Configuration Error"),
                           tr("GCS Bucket Name and Account Identifier cannot "
                              "be empty."));
      return;
    }
    entry.isGcsMode = true;
    entry.gcsBucketName = gcsBucketNameLineEdit_->text();
    entry.gcsAccountId = gcsAccountIdentifierLineEdit_->text();
  }

  for (const WatchEntry &e : watchManager_->entries()) {
    if (e.source == dir) {
      QMessageBox::information(this, tr("Already Watching"),
                               tr("This directory is already being watched."));
      return;
    }
  }
  watchManager_->addEntry(entry);

  QString display = QString::fromUtf8("\xF0\x9F\x91\x81 ") +
                    tr(" Monitoring | %1 \u2192 %2")
                        .arg(shortenPathForDisplay(dir),
                             currentDestinationForDisplay());
  QListWidgetItem *item = new QListWidgetItem(display);
  QFont f = item->font();
  f.setItalic(true);
  item->setFont(f);
  item->setData(Qt::UserRole, QStringLiteral("WATCH|") + dir);
  timeListWidget_->addItem(item);

  watchStatusLabel_->setText(
      tr("%1 dossier(s) surveill\u00e9(s)").arg(watchManager_->entries().size()));
  updateScheduleSummary();
  adjustHeightToScreen();
}

void MainWindow::handleWatchTriggered(const WatchEntry &e) {
  updateLog(tr("Modification d\u00e9tect\u00e9e dans %1, lancement de la sauvegarde.").arg(e.source));
  if (e.isGcsMode) {
    std::map<std::string, std::string> cfg;
    cfg["gcs_bucket_name"] = e.gcsBucketName.toStdString();
    cfg["gcs_account_identifier"] = e.gcsAccountId.toStdString();
    cfg["gcs_object_prefix"] = "";
    GcsTarget *t = new GcsTarget(cfg, m_credentialManager.get());
    performBackupInternal(e.source, t);
    delete t;
  } else if (e.isSftpMode) {
    std::map<std::string, std::string> cfg;
    cfg["host"] = e.sftpHost.toStdString();
    cfg["port"] = QString::number(e.sftpPort).toStdString();
    cfg["username"] = e.sftpUsername.toStdString();
    cfg["remoteBasePath"] = e.sftpRemotePath.toStdString();
    SftpTarget *t = new SftpTarget(cfg);
    performBackupInternal(e.source, t);
    delete t;
  } else {
    LocalTarget *t = new LocalTarget(e.destination.toStdString());
    performBackupInternal(e.source, t);
    delete t;
  }
}

void MainWindow::onWatchToggleChanged(bool checked) {
  if (checked) {
    watchManager_->enable();
    refreshWatchEntriesDisplay();
    if (watchManager_->entries().isEmpty())
      onAddWatchEntry();
  } else {
    watchManager_->disable();
    for (int i = timeListWidget_->count() - 1; i >= 0; --i) {
      QListWidgetItem *item = timeListWidget_->item(i);
      if (item->data(Qt::UserRole).toString().startsWith("WATCH|")) {
        delete timeListWidget_->takeItem(i);
      }
    }
    watchStatusLabel_->setText(tr("Monitoring off"));
    updateScheduleSummary();
  }
}


void MainWindow::updateScheduleFromUI() {
  QString sourcePath = sourceDirEdit_->text();
  QList<ScheduleEntry> entries;
  for (int i = 0; i < timeListWidget_->count(); ++i) {
    QString data = timeListWidget_->item(i)->data(Qt::UserRole).toString();
    if (data.startsWith("WATCH|"))
      continue;
    QStringList parts = data.split('|');
    QTime t = QTime::fromString(parts.value(0), "HH:mm");
    if (!t.isValid())
      continue;
    ScheduleEntry se;
    se.time = t;
    if (parts.size() > 1) {
      QStringList ds = parts[1].split(',');
      for (const QString &dsItem : ds) {
        bool ok = false;
        int val = dsItem.toInt(&ok);
        if (ok)
          se.days.insert(static_cast<Qt::DayOfWeek>(val));
      }
    }
    entries.append(se);
  }

  if (sourcePath.isEmpty() || entries.isEmpty()) {
    scheduler_->setDailyBackupTask("", "", {}, false, false, QString(), 0,
                                  QString(), QString(), false, QString(),
                                  QString());
    return;
  }

  QString destIdentifier;
  QString modeText = backupModeComboBox_->currentText();
  bool localMode = (modeText == tr("Local Backup"));
  bool sftpMode = (modeText == tr("SFTP Backup"));
  bool gcsMode = (modeText == tr("Google Cloud Storage"));

  if (localMode) {
    destIdentifier = destinationDirEdit_->text();
    if (destIdentifier.isEmpty())
      return;
    scheduler_->setDailyBackupTask(sourcePath, destIdentifier, entries, true,
                                   false, QString(), 0, QString(), QString(),
                                   false, QString(), QString());
  } else if (sftpMode) {
    if (sftpHostLineEdit_->text().isEmpty() ||
        sftpUsernameLineEdit_->text().isEmpty() ||
        sftpRemotePathLineEdit_->text().isEmpty())
      return;
    scheduler_->setDailyBackupTask(
        sourcePath,
        QString("sftp://%1%2")
            .arg(sftpHostLineEdit_->text(), sftpRemotePathLineEdit_->text()),
        entries, true, true, sftpHostLineEdit_->text(),
        sftpPortLineEdit_->text().toInt(), sftpUsernameLineEdit_->text(),
        sftpRemotePathLineEdit_->text(), false, QString(), QString());
  } else if (gcsMode) {
    if (gcsBucketNameLineEdit_->text().isEmpty() ||
        gcsAccountIdentifierLineEdit_->text().isEmpty())
      return;
    scheduler_->setDailyBackupTask(
        sourcePath, QString("gcs://%1").arg(gcsBucketNameLineEdit_->text()),
        entries, true, false, QString(), 0, QString(), QString(), true,
        gcsBucketNameLineEdit_->text(),
        gcsAccountIdentifierLineEdit_->text());
  }
  updateScheduleSummary();
}

void MainWindow::refreshWatchEntriesDisplay() {
  if (!timeListWidget_)
    return;
  for (int i = timeListWidget_->count() - 1; i >= 0; --i) {
    QListWidgetItem *item = timeListWidget_->item(i);
    if (item->data(Qt::UserRole).toString().startsWith("WATCH|")) {
      delete timeListWidget_->takeItem(i);
    }
  }
  for (const WatchEntry &e : watchManager_->entries()) {
    QString destDisp;
    if (e.isSftpMode) {
      destDisp = QString("%1:%2").arg(e.sftpHost, e.sftpRemotePath);
    } else if (e.isGcsMode) {
      destDisp = QString("gcs://%1").arg(e.gcsBucketName);
    } else {
      destDisp = e.destination;
    }
    destDisp = shortenPathForDisplay(destDisp);
    QString display = QString::fromUtf8("\xF0\x9F\x91\x81 ") +
                      tr(" Monitoring | %1 \u2192 %2")
                          .arg(shortenPathForDisplay(e.source), destDisp);
    QListWidgetItem *item = new QListWidgetItem(display);
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    item->setData(Qt::UserRole, QStringLiteral("WATCH|") + e.source);
    timeListWidget_->addItem(item);
  }
  if (watchManager_->entries().isEmpty())
    watchStatusLabel_->setText(tr("Monitoring off"));
  else
    watchStatusLabel_->setText(
        tr("%1 dossier(s) surveill\u00e9(s)")
            .arg(watchManager_->entries().size()));
  updateScheduleSummary();
}


void MainWindow::runBackupNow() {
  QString sourcePath = sourceDirEdit_->text();
  if (sourcePath.isEmpty()) {
    QMessageBox::warning(this, tr("Backup Error"),
                         tr("Source path must be configured."));
    updateLog("Error: Manual backup failed. Source path empty.");
    return;
  }

  QString currentModeText = backupModeComboBox_->currentText();
  bool localMode = (currentModeText == tr("Local Backup"));
  bool sftpMode = (currentModeText == tr("SFTP Backup"));
  bool gcsMode = (currentModeText == tr("Google Cloud Storage"));

  if (backupProgressBar_) {
    backupProgressBar_->setRange(0, 0);
    backupProgressBar_->setVisible(true);
  }

  IStorageTarget *backupOperationTarget =
      nullptr; // Use a local variable for the backup operation
  // Do NOT delete this->sftpTarget_ or this->gcsTarget_ here as they are used
  // by the viewer. this->localTarget_ is not used by a viewer, so it can be
  // managed/deleted if it was from a previous runBackupNow. For simplicity and
  // consistency, we'll create temporary targets for all modes in this function.
  // If this->localTarget_ was used by a previous runBackupNow, it will be
  // orphaned if not deleted. However, the current design deletes it in the
  // destructor or if runBackupNow is called again for local. Let's ensure it's
  // cleared if it's specific to this one-off backup. A cleaner approach is to
  // always use temporary for runBackupNow for all types.
  if (this->localTarget_) { // If it's from a previous runBackupNow call
                            // specifically for local.
    delete this->localTarget_;
    this->localTarget_ = nullptr;
  }

  if (localMode) {
    updateLog("Local Mode selected for manual backup.");
    QString destPath = destinationDirEdit_->text();
    if (destPath.isEmpty()) {
      QMessageBox::warning(
          this, tr("Backup Error"),
          tr("Destination path must be configured for local backup."));
      updateLog("Error: Manual backup failed. Local destination path empty.");
      return;
    }
    std::filesystem::path fsSourcePath(sourcePath.toStdString());
    std::filesystem::path fsDestinationPathFromUI(destPath.toStdString());
    std::filesystem::path backupRootForTarget =
        fsDestinationPathFromUI / fsSourcePath.filename();
    LocalTarget *tempLocalTarget =
        new LocalTarget(backupRootForTarget.string()); // Temporary instance
    backupOperationTarget = tempLocalTarget;
    updateLog(QString("Manual Local backup started: From '%1' to '%2'.")
                  .arg(sourcePath, destPath));

  } else if (sftpMode) {
    updateLog("SFTP Mode selected for manual backup.");
    if (sftpHostLineEdit_->text().isEmpty() ||
        sftpUsernameLineEdit_->text().isEmpty() ||
        sftpRemotePathLineEdit_->text().isEmpty()) {
      QMessageBox::warning(
          this, tr("Backup Error"),
          tr("SFTP Host, Username, and Remote Path must be configured."));
      updateLog(
          "Error: Manual SFTP backup failed. Required SFTP fields missing.");
      return;
    }
    std::map<std::string, std::string> sftpConfig;
    sftpConfig["host"] = sftpHostLineEdit_->text().toStdString();
    sftpConfig["port"] = sftpPortLineEdit_->text().toStdString();
    sftpConfig["username"] = sftpUsernameLineEdit_->text().toStdString();
    sftpConfig["remoteBasePath"] =
        sftpRemotePathLineEdit_->text().toStdString();
    QString qPasswordFromField = sftpPasswordLineEdit_->text();
    if (!qPasswordFromField.isEmpty())
      sftpConfig["password"] = qPasswordFromField.toStdString();
    SftpTarget *tempSftpTarget =
        new SftpTarget(sftpConfig); // Temporary instance
    backupOperationTarget = tempSftpTarget;
    updateLog(QString("Manual SFTP backup started: From '%1' to host '%2'.")
                  .arg(sourcePath, QString::fromStdString(sftpConfig["host"])));

  } else if (gcsMode) {
    updateLog("GCS Mode selected for manual backup.");
    QString bucketName = gcsBucketNameLineEdit_->text();
    QString accountId = gcsAccountIdentifierLineEdit_->text();
    if (bucketName.isEmpty() || accountId.isEmpty()) {
      QMessageBox::warning(
          this, tr("Backup Error"),
          tr("GCS Bucket Name and Account Identifier must be configured."));
      updateLog("Error: Manual GCS backup failed. Bucket Name or Account ID "
                "missing.");
      return;
    }
    std::map<std::string, std::string> gcsConfig;
    gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
    gcsConfig["gcs_account_identifier"] = accountId.toStdString();
    gcsConfig["gcs_object_prefix"] = ""; // For backups, prefix might be source
                                         // folder name, handled by target.

    GcsTarget *tempGcsTarget = new GcsTarget(
        gcsConfig, m_credentialManager.get()); // Temporary instance
    backupOperationTarget = tempGcsTarget;
    updateLog(QString("Manual GCS backup started: From '%1' to GCS Bucket '%2' "
                      "(Account: '%3')")
                  .arg(sourcePath, bucketName, accountId));
  } else {
    QMessageBox::critical(
        this, tr("Internal Error"),
        tr("Unknown backup mode selected for manual backup."));
    updateLog("Error: Manual backup failed. Unknown backup mode.");
    return;
  }

  if (!backupOperationTarget) { // Check the local variable
    QMessageBox::critical(this, tr("Backup Error"),
                          tr("Failed to initialize backup target."));
    updateLog("Error: Manual backup failed. Target initialization failed.");
    return;
  }

  performBackupInternal(sourcePath, backupOperationTarget);

  // Clean up the temporary target used for this backup operation
  // performBackupInternal calls endSession on the target.
  delete backupOperationTarget;
  backupOperationTarget = nullptr;
  if (backupProgressBar_)
    backupProgressBar_->setVisible(false);
  statusBar()->showMessage(tr("Last backup: OK at %1")
                               .arg(QTime::currentTime().toString("HH:mm")));
}

void MainWindow::onGcsConnectButtonClicked() {
  updateLog(
      tr("GCS 'Log in to Google Drive' button clicked (OAuth process)."));
  QString bucketName = gcsBucketNameLineEdit_->text();
  QString accountId = gcsAccountIdentifierLineEdit_->text();

  if (bucketName.isEmpty() || accountId.isEmpty()) {
    QMessageBox::warning(this, tr("GCS Configuration Error"),
                         tr("GCS Bucket Name and Account Identifier must be "
                            "provided before connecting for OAuth."));
    updateLog(tr("GCS OAuth: Bucket Name or Account Identifier missing."));
    return;
  }

  std::map<std::string, std::string> gcsConfig;
  gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
  gcsConfig["gcs_account_identifier"] = accountId.toStdString();

  GcsTarget tempGcsTargetForOAuth(
      gcsConfig,
      m_credentialManager.get()); // Create a temporary target for OAuth

  gcsAuthStatusLabel_->setText(tr("Status: Authenticating..."));
  updateLog(QString("GCS OAuth: Attempting authentication for account '%1' "
                    "(bucket '%2' context for token).")
                .arg(accountId, bucketName));

  if (tempGcsTargetForOAuth
          .initiateOAuthAndStoreToken()) { // Use the temporary target
    gcsAuthStatusLabel_->setText(
        tr("Status: Authentication Successful for %1").arg(accountId));
    updateLog(QString("GCS OAuth: Authentication successful for account '%1'.")
                  .arg(accountId));

    QSettings settings(QCoreApplication::organizationName(),
                       QCoreApplication::applicationName());
    settings.beginGroup("GCS");
    settings.setValue("gcs_last_authenticated_account", accountId);
    settings.endGroup();
    updateLog(QString("GCS OAuth: Stored '%1' as last authenticated account.")
                  .arg(accountId));

    // IMPORTANT: DO NOT automatically connect for listing or browseRemotePath
    // here. User must click the new gcsConnectToggleButton_ for that. Also, DO
    // NOT change gcsConnectToggleButton_ text here.

  } else {
    gcsAuthStatusLabel_->setText(
        tr("Status: Authentication Failed. Check logs."));
    QString gcsError =
        QString::fromStdString(tempGcsTargetForOAuth.getLastError());
    updateLog(
        QString("GCS OAuth: Authentication failed for account '%1'. Error: %2")
            .arg(accountId, gcsError));
    QMessageBox::critical(this, tr("GCS Authentication Failed"),
                          tr("Could not authenticate with Google Cloud Storage "
                             "for account '%1'. Error: %2")
                              .arg(accountId, gcsError));

    // Reset UI related to listing, as it cannot proceed if OAuth fails
    if (gcsConnectToggleButton_) { // Ensure button exists
      gcsConnectToggleButton_->setText(tr("Connect"));
    }
    currentRemotePath_ = "/";
    if (fileViewerWidget_) {
      fileViewerWidget_->browsePath(currentRemotePath_);
    }
    // QSettings settings(QCoreApplication::organizationName(),
    // QCoreApplication::applicationName());
    // settings.remove("GCS/gcs_last_authenticated_account");
  }
  // tempGcsTargetForOAuth goes out of scope here.
  // The main gcsTarget_ (for listing) is managed by onGcsConnectToggleClicked.
}

void MainWindow::onSftpConnectToggleClicked() {
  if (sftpTarget_) {
    updateLog(tr("SFTP 'Disconnect' (viewer) button clicked. Closing viewer session."));
    sftpTarget_->endSession();
    delete sftpTarget_;
    sftpTarget_ = nullptr;
    fileViewerWidget_->setSftpTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    if (sftpConnectToggleButton_)
      sftpConnectToggleButton_->setText(tr("Connect"));
    updateLog(tr("SFTP viewer session closed."));
    return;
  }

  updateLog(tr("SFTP 'Connect' (viewer) button clicked. Attempting to open viewer session."));
  std::map<std::string, std::string> cfg{
      {"host", sftpHostLineEdit_->text().toStdString()},
      {"port", sftpPortLineEdit_->text().toStdString()},
      {"username", sftpUsernameLineEdit_->text().toStdString()},
      {"remoteBasePath", sftpRemotePathLineEdit_->text().toStdString()}
  };

  if (cfg["host"].empty() || cfg["username"].empty()) {
    QMessageBox::warning(this, tr("SFTP Configuration"),
                         tr("SFTP Host and Username are required to connect for listing."));
    if (sftpConnectToggleButton_)
      sftpConnectToggleButton_->setText(tr("Connect"));
    currentRemotePath_ = "/";
    fileViewerWidget_->setSftpTarget(nullptr);
    fileViewerWidget_->browsePath(currentRemotePath_);
    updateLog(tr("SFTP viewer connection failed: Host or Username empty."));
    return;
  }

  sftpTarget_ = new SftpTarget(cfg);
  if (!sftpTarget_->beginSession()) {
    QString err_msg = QString::fromStdString(sftpTarget_->getLastError());
    QMessageBox::critical(this, tr("SFTP Error"),
                          tr("SFTP viewer connection failed: %1")
                              .arg(err_msg.isEmpty() ? tr("Unknown error") : err_msg));
    updateLog(tr("SFTP viewer connection failed: %1")
                  .arg(err_msg.isEmpty() ? tr("Unknown error") : err_msg));
    delete sftpTarget_;
    sftpTarget_ = nullptr;
    if (sftpConnectToggleButton_)
      sftpConnectToggleButton_->setText(tr("Connect"));
    fileViewerWidget_->setSftpTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    return;
  }

  if (sftpConnectToggleButton_)
    sftpConnectToggleButton_->setText(tr("Disconnect"));
  fileViewerWidget_->setSftpTarget(sftpTarget_);
  currentRemotePath_ = "/";
  fileViewerWidget_->browsePath(currentRemotePath_);
  updateLog(tr("SFTP viewer session opened."));
}

void MainWindow::onGcsConnectToggleClicked() {
  if (gcsTarget_) { // Connected for listing -> Disconnect
    updateLog(tr("GCS 'Disconnect' button clicked. Closing session for listing."));
    gcsTarget_->endSession();
    delete gcsTarget_;
    gcsTarget_ = nullptr;
    fileViewerWidget_->setGcsTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    gcsConnectToggleButton_->setText(tr("Connect"));
    updateLog(tr("GCS session closed for listing. OAuth status is separate."));
    return;
  }

  // Not connected for listing -> Connect
  updateLog(tr(
      "GCS 'Connect' button clicked. Attempting to open session for listing."));

  QString accountIdFromUI = gcsAccountIdentifierLineEdit_->text();
  QString bucketNameFromUI = gcsBucketNameLineEdit_->text();

  if (accountIdFromUI.isEmpty() || bucketNameFromUI.isEmpty()) {
    QMessageBox::warning(this, tr("GCS Configuration"),
                         tr("GCS Account Identifier and Bucket Name are "
                            "required to connect for listing."));
    updateLog(tr(
        "GCS connection for listing failed: Account ID or Bucket Name empty."));
    // Ensure UI is consistent
    gcsConnectToggleButton_->setText(tr("Connect"));
    fileViewerWidget_->setGcsTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    return;
  }

  // Check if OAuth has been done for this account
  QSettings settings(QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
  QString lastAuthAccount =
      settings.value("GCS/gcs_last_authenticated_account").toString();
  // A more robust check might involve GcsTarget itself having a method to check
  // current auth status or MainWindow having a flag like
  // `m_gcsOAuthSuccessfulForCurrentUser`
  if (lastAuthAccount != accountIdFromUI) {
    QMessageBox::warning(
        this, tr("GCS Authentication Required"),
        tr("Please use the 'Log in to Google Drive' button to authenticate "
           "for account '%1' before attempting to list files.")
            .arg(accountIdFromUI));
    updateLog(tr("GCS connection for listing aborted: Account '%1' not "
                 "authenticated via 'Log in to Google Drive' button, or "
                 "does not match last authenticated user ('%2').")
                  .arg(accountIdFromUI, lastAuthAccount));
    // Ensure UI is consistent
    gcsConnectToggleButton_->setText(tr("Connect"));
    fileViewerWidget_->setGcsTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    return;
  }

  // Check if token is still valid (GcsTarget might do this in beginSession)
  // For now, we assume if lastAuthAccount matches, we can proceed.
  // GcsTarget::beginSession() should fail if token is invalid/expired.

  std::map<std::string, std::string> gcsConfig;
  gcsConfig["gcs_bucket_name"] = bucketNameFromUI.toStdString();
  gcsConfig["gcs_account_identifier"] = accountIdFromUI.toStdString();
  // gcs_object_prefix for listing is implicitly handled by browseRemotePath
  // starting at "/" relative to bucket root.

  // Ensure no previous gcsTarget_ instance is active from other operations
  // (e.g. backup) This toggle button should manage its own gcsTarget_ instance
  // for listing. If a backup is running with gcsTarget_, this could interfere.
  // For simplicity now, we assume this gcsTarget_ is primarily for listing.
  // A more advanced setup might use separate target instances or a ref-counted
  // session.
  delete gcsTarget_;
  gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());

  if (!gcsTarget_->beginSession()) { // beginSession should verify token and
                                     // connectivity
    QString err = tr("GCS Connect for listing failed: %1")
                      .arg(QString::fromStdString(gcsTarget_->getLastError()));
    QMessageBox::critical(this, tr("GCS Error"), err);
    updateLog(err);
    delete gcsTarget_;
    gcsTarget_ = nullptr;
    // Ensure UI is consistent
    gcsConnectToggleButton_->setText(tr("Connect"));
    fileViewerWidget_->setGcsTarget(nullptr);
    currentRemotePath_ = "/";
    fileViewerWidget_->browsePath(currentRemotePath_);
    // Potentially update gcsAuthStatusLabel_ if error indicates auth failure,
    // but the plan says this button doesn't directly change it.
    // However, if beginSession fails due to auth, the user should know.
    // Perhaps GcsTarget::getLastError() can distinguish auth errors.
    // For now, keeping gcsAuthStatusLabel_ unchanged by this button directly.
    return;
  }

  gcsConnectToggleButton_->setText(tr("Disconnect"));
  fileViewerWidget_->setGcsTarget(gcsTarget_);
  currentRemotePath_ = "/";
  fileViewerWidget_->browsePath(currentRemotePath_);
  updateLog(tr("GCS session opened successfully for listing. Root directory "
               "displayed for bucket '%1'.")
                .arg(bucketNameFromUI));
}

void MainWindow::onGcsTestConnectionClicked() {
  updateLog("GCS Test Connection button clicked.");
  QString bucketName = gcsBucketNameLineEdit_->text();
  QString accountId = gcsAccountIdentifierLineEdit_->text();

  if (bucketName.isEmpty() || accountId.isEmpty()) {
    QMessageBox::warning(this, tr("GCS Configuration Error"),
                         tr("GCS Bucket Name and Account Identifier must be "
                            "provided before testing connection."));
    updateLog(
        "GCS Test Connection: Bucket Name or Account Identifier missing.");
    return;
  }

  std::map<std::string, std::string> gcsConfig;
  gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
  gcsConfig["gcs_account_identifier"] = accountId.toStdString();

  delete gcsTarget_;
  gcsTarget_ = new GcsTarget(gcsConfig, m_credentialManager.get());

  gcsAuthStatusLabel_->setText(tr("Status: Testing Connection..."));
  updateLog(
      QString(
          "GCS Test Connection: Attempting for account '%1' with bucket '%2'.")
          .arg(accountId, bucketName));

  std::string testErrorMsg;
  if (gcsTarget_->testConnection(testErrorMsg)) {
    QMessageBox::information(this, tr("GCS Connection Test"),
                             tr("Connection Successful!"));
    updateLog(QString("GCS Test Connection: Successful for account '%1'.")
                  .arg(accountId));

    QString lastAuthAccount =
        QSettings().value("GCS/gcs_last_authenticated_account").toString();
    if (!lastAuthAccount.isEmpty() && lastAuthAccount == accountId) {
      gcsAuthStatusLabel_->setText(
          tr("Status: Authenticated as %1").arg(accountId));
    } else {
      // If test passed but not explicitly "Connected" via button, or token
      // expired and test used a new one implicitly
      gcsAuthStatusLabel_->setText(tr("Status: Connection test passed."));
    }
  } else {
    QMessageBox::warning(
        this, tr("GCS Connection Test"),
        tr("Connection Failed: %1").arg(QString::fromStdString(testErrorMsg)));
    updateLog(QString("GCS Test Connection: Failed for account '%1'. Error: %2")
                  .arg(accountId, QString::fromStdString(testErrorMsg)));
    if (testErrorMsg.find("OAuth") != std::string::npos ||
        testErrorMsg.find("token") != std::string::npos ||
        testErrorMsg.find("permission") != std::string::npos ||
        testErrorMsg.find("denied") != std::string::npos ||
        testErrorMsg.find("authenticate") !=
            std::string::npos) { // Added generic auth term
      gcsAuthStatusLabel_->setText(
          tr("Status: Auth/Permission issue during test."));
    } else {
      gcsAuthStatusLabel_->setText(tr("Status: Connection test failed."));
    }
  }

  delete gcsTarget_;
  gcsTarget_ = nullptr;
}

void MainWindow::performBackupInternal(const QString &sourcePath,
                                       IStorageTarget *target) {
  if (!target) {
    updateLog("Error: performBackupInternal called with null target.");
    QMessageBox::critical(this, tr("Backup Failed"),
                          tr("Internal error: Backup target not specified."));
    return;
  }
  updateLog(QString("Performing backup using active target: Source: %1")
                .arg(sourcePath));

  std::filesystem::path fsSourcePath(sourcePath.toStdString());

  QFileInfo initialSourceInfo(QString::fromStdString(fsSourcePath.string()));
  QString baseDirForLambda = initialSourceInfo.absolutePath();

  if (!std::filesystem::is_directory(fsSourcePath)) {
    updateLog(
        QString("Error: Source path '%1' is not a directory.").arg(sourcePath));
    QMessageBox::critical(this, tr("Backup Failed"),
                          tr("Source path is not a directory."));
    return;
  }

  if (!target->beginSession()) {
    std::string specificError;
    GcsTarget *currentGcsTarget = dynamic_cast<GcsTarget *>(target);
    if (currentGcsTarget) {
      specificError = currentGcsTarget->getLastError();
    }
    QString errorDetails = QString::fromStdString(specificError);
    if (errorDetails.isEmpty())
      errorDetails = tr("Check target logs for more information.");

    updateLog(QString("Error: Could not begin backup session. Target error: %1")
                  .arg(errorDetails));
    QMessageBox::critical(
        this, tr("Backup Failed"),
        tr("Could not begin backup session. Details: %1").arg(errorDetails));
    return;
  }

  if (!std::filesystem::exists(fsSourcePath)) {
    updateLog(QString("Error: Source directory '%1' does not exist.")
                  .arg(sourcePath));
    target->endSession();
    QMessageBox::critical(this, tr("Backup Failed"),
                          tr("Source directory not found."));
    return;
  }

  updateLog(QString("Starting recursive scan of source directory: %1")
                .arg(sourcePath));
  bool all_ok = true;
  int files_processed_count = 0;

  std::function<void(const std::filesystem::path &)> recursiveCopy;
  recursiveCopy = [&](const std::filesystem::path &currentPathInSource) {
    try {
      for (const auto &entry :
           std::filesystem::directory_iterator(currentPathInSource)) {
        std::filesystem::path fullEntryPath = entry.path();
        QString qFullEntryPath_new =
            QString::fromStdString(fullEntryPath.string());
        std::string relativePathStr =
            qFullEntryPath_new
                .mid(baseDirForLambda.length() +
                     (baseDirForLambda == "/" ? 0 : 1))
                .toStdString();

        if (entry.is_directory()) {
          updateLog(QString("Scanning subdirectory: %1")
                        .arg(QString::fromStdString(relativePathStr)));
          recursiveCopy(fullEntryPath);
        } else if (entry.is_regular_file()) {
          ++files_processed_count;

          // Fabrique un FileMetadata minimal
          FileMetadata meta;
          meta.name = relativePathStr;
          meta.size = entry.file_size();
          auto ftime = entry.last_write_time();
          auto sctp =
              std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                  ftime - decltype(ftime)::clock::now() +
                  std::chrono::system_clock::now());
          meta.modificationTime = sctp;

          if (target->sendFile(fullEntryPath.string(), meta)) {
            updateLog(QString("Backed up: %1")
                          .arg(QString::fromStdString(relativePathStr)));
          } else {
            all_ok = false; // Set all_ok to false on sendFile failure

            std::string err_msg;
            // Attempt to get specific error message using dynamic_cast
            if (auto sftpTarget = dynamic_cast<SftpTarget *>(target)) {
              err_msg = sftpTarget->getLastError();
            } else if (auto gcsTarget = dynamic_cast<GcsTarget *>(target)) {
              err_msg = gcsTarget->getLastError();
            } else if (auto localTarget = dynamic_cast<LocalTarget *>(target)) {
              // Assuming LocalTarget might also have getLastError() or a
              // similar mechanism If LocalTarget does not have getLastError(),
              // this will need adjustment For now, let's assume it might return
              // a generic error if getLastError() isn't present. err_msg =
              // localTarget->getLastError(); // Uncomment if LocalTarget has
              // this
              if (err_msg.empty())
                err_msg = "File operation failed with LocalTarget.";
            } else {
              err_msg = "Unknown target type or error during sendFile.";
            }
            if (err_msg.empty()) { // Fallback if getLastError() returned empty
              err_msg = "target->sendFile() failed with no specific error "
                        "message provided by the target.";
            }

            updateLog(QString("Error backing up: %1. Error: %2")
                          .arg(QString::fromStdString(relativePathStr))
                          .arg(QString::fromStdString(err_msg)));

            QMessageBox::critical(
                this, "Backup Error",
                QString("Failed to back up: %1\nError: %2")
                    .arg(QString::fromStdString(relativePathStr))
                    .arg(QString::fromStdString(err_msg)));
            return; // Stop backup if one file fails (as per original logic)
          }
        }
      }
    } catch (const std::filesystem::filesystem_error &e) {
      updateLog(QString("Error accessing path %1: %2")
                    .arg(QString::fromStdString(currentPathInSource.string()),
                         QString::fromStdString(e.what())));
      all_ok = false; // This is already correct
    }
  };

  recursiveCopy(fsSourcePath);

  if (!target->endSession()) {
    all_ok = false; // Ensure all_ok is set if endSession fails

    std::string specificError;
    // Retrieve error from specific target type for endSession failure
    if (auto sftpTarget = dynamic_cast<SftpTarget *>(target)) {
      specificError = sftpTarget->getLastError();
    } else if (auto gcsTarget = dynamic_cast<GcsTarget *>(target)) {
      specificError = gcsTarget->getLastError();
    } else if (auto localTarget = dynamic_cast<LocalTarget *>(target)) {
      // Assuming LocalTarget might also have getLastError()
      // specificError = localTarget->getLastError(); // Uncomment if
      // LocalTarget has this
      if (specificError.empty())
        specificError = "Failed to end session with LocalTarget.";
    } else {
      specificError = "Unknown target type or error during endSession.";
    }
    if (specificError.empty()) {
      specificError = "target->endSession() failed with no specific error "
                      "message provided by the target.";
    }

    QString errorDetails = QString::fromStdString(specificError);
    updateLog(QString("Error: Could not properly end backup session with "
                      "target. Target error: %1")
                  .arg(errorDetails));
    // No QMessageBox here as the final summary will indicate errors.
  }

  if (all_ok) {
    if (files_processed_count == 0) {
      updateLog(
          QString("Backup completed. No files were found to back up in '%1'.")
              .arg(sourcePath));
      QMessageBox::information(
          this, tr("Backup Complete"),
          tr("Backup completed. No files were found in the source directory."));
    } else {
      updateLog(
          QString("Backup process completed successfully. %1 files processed.")
              .arg(files_processed_count));
      QMessageBox::information(this, tr("Backup Complete"),
                               tr("Backup completed successfully."));
    }
  } else {
    updateLog(
        "Backup process completed with some errors. Please check the log.");
    QMessageBox::warning(
        this, tr("Backup Complete (with errors)"),
        tr("Backup completed with some errors. Please check the log."));
  }
}

void MainWindow::updateLog(const QString &message) {
  QString timestamp = QTime::currentTime().toString("HH:mm:ss");
  logDisplay_->append(QString("[%1] %2").arg(timestamp, message));
  qDebug() << "[GUI Log]" << message;
}

void MainWindow::onTaskChanged() {
  sourceDirEdit_->setText(scheduler_->sourcePath());

  if (scheduler_->isGcsMode()) {
    backupModeComboBox_->setCurrentIndex(2);
    gcsBucketNameLineEdit_->setText(scheduler_->gcsBucketName());
    gcsAccountIdentifierLineEdit_->setText(scheduler_->gcsAccountIdentifier());
  } else if (scheduler_->isSftpMode()) {
    backupModeComboBox_->setCurrentIndex(1);
  } else {
    backupModeComboBox_->setCurrentIndex(0);
    destinationDirEdit_->setText(scheduler_->destinationPath());
  }

  timeListWidget_->clear();
  QList<ScheduleEntry> entries = scheduler_->scheduleEntries();
  if (!entries.isEmpty()) {
    for (const ScheduleEntry &se : entries) {
      QString data = se.time.toString("HH:mm");
      QString display = se.time.toString("HH:mm");
      if (!se.days.isEmpty()) {
        QStringList names;
        QStringList nums;
        const QStringList dayLabels = {tr("Mon"), tr("Tue"), tr("Wed"),
                                       tr("Thu"), tr("Fri"), tr("Sat"),
                                       tr("Sun")};
        for (Qt::DayOfWeek d : se.days) {
          names << dayLabels[d - 1];
          nums << QString::number(int(d));
        }
        display += " (" + names.join(',') + ")";
        data += "|" + nums.join(',');
      } else {
        display += tr(" (All)");
      }
      QString srcDisp = shortenPathForDisplay(scheduler_->sourcePath());
      QString destDisp;
      if (scheduler_->isSftpMode()) {
        destDisp = QString("%1:%2")
                       .arg(scheduler_->sftpHost(), scheduler_->sftpRemotePath());
      } else if (scheduler_->isGcsMode()) {
        destDisp = QString("gcs://%1").arg(scheduler_->gcsBucketName());
      } else {
        destDisp = scheduler_->destinationPath();
      }
      destDisp = shortenPathForDisplay(destDisp);
      if (!srcDisp.isEmpty() && !destDisp.isEmpty()) {
        display += QString(" | %1 \u2192 %2").arg(srcDisp, destDisp);
      }
      QListWidgetItem *item = new QListWidgetItem(display);
      item->setData(Qt::UserRole, data);
      timeListWidget_->addItem(item);
    }
    backupTimeEdit_->setTime(entries.first().time);
  } else {
    backupTimeEdit_->setTime(QTime(23, 0));
  }
  refreshWatchEntriesDisplay();
  onBackupModeChanged(backupModeComboBox_->currentIndex());
  updateLog("Task details updated in UI from Scheduler state.");
  updateScheduleSummary();
}

void MainWindow::onBackupModeChanged(int index) {
  if (backupModeStackedWidget_)
    backupModeStackedWidget_->setCurrentIndex(index);
  QString currentModeText = backupModeComboBox_->itemText(index);
  bool localSelected = (currentModeText == tr("Local Backup"));
  bool sftpSelected = (currentModeText == tr("SFTP Backup"));
  bool gcsSelected = (currentModeText == tr("Google Cloud Storage"));

  updateLog(QString("Backup mode changing. Selected: %1").arg(currentModeText));

  // End active SFTP listing session if switching away from SFTP mode
  if (!sftpSelected && sftpTarget_) {
    updateLog(tr(
        "Switched away from SFTP mode. Closing active SFTP listing session."));
    sftpTarget_->endSession();
    delete sftpTarget_;
    sftpTarget_ = nullptr;
    if (sftpConnectToggleButton_) { // Ensure button exists
      sftpConnectToggleButton_->setText(tr("Connect"));
    }
  }

  // End active GCS listing session if switching away from GCS mode
  if (!gcsSelected && gcsTarget_) {
    updateLog(
        tr("Switched away from GCS mode. Closing active GCS listing session."));
    gcsTarget_->endSession(); // Call directly
    delete gcsTarget_;
    gcsTarget_ = nullptr;
    if (gcsConnectToggleButton_) { // Ensure button exists
      gcsConnectToggleButton_->setText(tr("Connect"));
    }
    // Note: gcsAuthStatusLabel_ is NOT changed here. OAuth state persists.
  }

  // Always reset file viewer UI elements on any mode change
  if (fileViewerWidget_) {
    fileViewerWidget_->setSftpTarget(nullptr);
    fileViewerWidget_->setGcsTarget(nullptr);
    fileViewerWidget_->browsePath("/");
  }
  currentRemotePath_ = "/";


  // Show/hide file viewer group box (only for remote modes)
  bool remoteModeSelected = (sftpSelected || gcsSelected);
  if (fileViewerDockWidget_) {
    fileViewerDockWidget_->setVisible(remoteModeSelected);
  }

  // IMPORTANT: Automatic connection and browsing logic has been removed.
  // The user now explicitly clicks the "Connect" button for SFTP/GCS listing.

  updateLog(
      QString(
          "Backup mode changed to: %1. UI adjusted. File viewer visible: %2.")
          .arg(currentModeText)
          .arg(remoteModeSelected ? "Yes" : "No"));
  if (centralWidget() && centralWidget()->layout())
    centralWidget()->layout()->invalidate();
  adjustHeightToScreen();
}

void MainWindow::loadSettings() {
  QSettings settings(QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
  settings.beginGroup("MainWindow");

  const QByteArray geometry =
      settings.value("geometry", QByteArray()).toByteArray();
  if (!geometry.isEmpty()) {
    restoreGeometry(geometry);
  } else {
    resize(800, 700);
  }

  backupModeComboBox_->setCurrentIndex(
      settings.value("backupModeIndex", 0).toInt());
  if (backupModeStackedWidget_)
    backupModeStackedWidget_->setCurrentIndex(backupModeComboBox_->currentIndex());
  settings.endGroup();

  settings.beginGroup("WatchEntries");
  int watchCount = settings.beginReadArray("entries");
  for (int i = 0; i < watchCount; ++i) {
    settings.setArrayIndex(i);
    WatchEntry e;
    e.source = settings.value("source").toString();
    e.destination = settings.value("destination").toString();
    e.isSftpMode = settings.value("isSftp", false).toBool();
    e.isGcsMode = settings.value("isGcs", false).toBool();
    e.sftpHost = settings.value("sftpHost").toString();
    e.sftpPort = settings.value("sftpPort", 22).toInt();
    e.sftpUsername = settings.value("sftpUser").toString();
    e.sftpRemotePath = settings.value("sftpPath").toString();
    e.gcsBucketName = settings.value("gcsBucket").toString();
    e.gcsAccountId = settings.value("gcsAccount").toString();
    if (!e.source.isEmpty()) {
      watchManager_->addEntry(e);
    }
  }
  settings.endArray();
  settings.endGroup();
  refreshWatchEntriesDisplay();
  if (watchToggleCheckBox_) {
    bool blocked = watchToggleCheckBox_->blockSignals(true);
    watchToggleCheckBox_->setChecked(!watchManager_->entries().isEmpty());
    watchToggleCheckBox_->blockSignals(blocked);
    if (!watchManager_->entries().isEmpty())
      watchManager_->enable();
  }

  settings.beginGroup("SFTP");
  sftpHostLineEdit_->setText(settings.value("host", "").toString());
  sftpPortLineEdit_->setText(settings.value("port", "22").toString());
  sftpUsernameLineEdit_->setText(settings.value("username", "").toString());
  sftpRemotePathLineEdit_->setText(settings.value("remotePath", "").toString());
  sftpSavePasswordCheckBox_->setChecked(
      settings.value("savePassword", false).toBool());
  settings.endGroup();

  settings.beginGroup("GCS");
  gcsBucketNameLineEdit_->setText(
      settings.value("gcs_bucket_name", "").toString());
  gcsAccountIdentifierLineEdit_->setText(
      settings.value("gcs_account_identifier", "").toString());
  QString lastAuthAccount =
      settings.value("gcs_last_authenticated_account", "").toString();
  if (!lastAuthAccount.isEmpty() &&
      lastAuthAccount == gcsAccountIdentifierLineEdit_->text()) {
    gcsAuthStatusLabel_->setText(
        tr("Status: Authenticated as %1").arg(lastAuthAccount));
  } else {
    gcsAuthStatusLabel_->setText(tr("Status: Not Authenticated"));
  }
  settings.endGroup();

  updateLog("Settings loaded.");
  adjustHeightToScreen();
}

void MainWindow::saveSettings() {
  QSettings settings(QCoreApplication::organizationName(),
                     QCoreApplication::applicationName());
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
  settings.setValue("gcs_account_identifier",
                    gcsAccountIdentifierLineEdit_->text());
  // gcs_last_authenticated_account is saved only on successful connect
  settings.endGroup();

  settings.beginGroup("WatchEntries");
  settings.beginWriteArray("entries");
  const auto &entries = watchManager_->entries();
  for (int i = 0; i < entries.size(); ++i) {
    settings.setArrayIndex(i);
    const WatchEntry &e = entries[i];
    settings.setValue("source", e.source);
    settings.setValue("destination", e.destination);
    settings.setValue("isSftp", e.isSftpMode);
    settings.setValue("isGcs", e.isGcsMode);
    settings.setValue("sftpHost", e.sftpHost);
    settings.setValue("sftpPort", e.sftpPort);
    settings.setValue("sftpUser", e.sftpUsername);
    settings.setValue("sftpPath", e.sftpRemotePath);
    settings.setValue("gcsBucket", e.gcsBucketName);
    settings.setValue("gcsAccount", e.gcsAccountId);
  }
  settings.endArray();
  settings.endGroup();

  if (backupModeComboBox_->currentText() == tr("SFTP Backup")) {
    QString host = sftpHostLineEdit_->text();
    QString port = sftpPortLineEdit_->text();
    QString username = sftpUsernameLineEdit_->text();
    QString passwordInField = sftpPasswordLineEdit_->text();

    if (!host.isEmpty() && !port.isEmpty() && !username.isEmpty() &&
        m_credentialManager) {
      QString serviceName = QString("sftp_%1_%2").arg(host).arg(port.toInt());
      if (sftpSavePasswordCheckBox_->isChecked()) {
        if (!passwordInField.isEmpty()) {
          m_credentialManager->storeSecret(serviceName, username,
                                           passwordInField);
        }
      } else {
        m_credentialManager->deleteSecret(serviceName, username);
      }
    }
  }
  updateLog("Settings saved.");
}

void MainWindow::handleScheduledBackup(const QString &sourcePath,
                                       const QString &destinationOrIdentifier) {
  updateLog(
      QString(
          "Scheduled backup triggered by Scheduler: Source '%1', Dest/ID '%2'")
          .arg(sourcePath, destinationOrIdentifier));

  if (backupProgressBar_) {
    backupProgressBar_->setRange(0, 0);
    backupProgressBar_->setVisible(true);
  }

  IStorageTarget *currentTarget =
      nullptr; // Local variable for this backup operation
  // Do not delete this->sftpTarget_ or this->gcsTarget_ (viewer targets)
  // localTarget_ can be deleted if it's only for one-off local backups from
  // UI/scheduler For consistency, all scheduled backups will use temporary
  // targets. If this->localTarget_ was set by runBackupNow, it's already
  // deleted there or by destructor. If scheduler uses its own parameters, it
  // should always create a new target.

  if (scheduler_->isGcsMode()) {
    updateLog("Scheduled task is GCS Mode.");
    QString bucketName = scheduler_->gcsBucketName();
    QString accountId = scheduler_->gcsObjectPrefix();

    if (bucketName.isEmpty() || accountId.isEmpty()) {
      updateLog("Error: Scheduled GCS backup but bucket name or account ID "
                "(from scheduler's gcsObjectPrefix) is missing in Scheduler.");
      QMessageBox::critical(this, tr("Scheduled Backup Error"),
                            tr("GCS configuration for scheduled backup is "
                               "incomplete (bucket or account ID)."));
      return;
    }

    std::map<std::string, std::string> gcsConfig;
    gcsConfig["gcs_bucket_name"] = bucketName.toStdString();
    gcsConfig["gcs_account_identifier"] = accountId.toStdString();
    gcsConfig["gcs_object_prefix"] = "";

    GcsTarget *scheduledGcsTarget =
        new GcsTarget(gcsConfig, m_credentialManager.get());
    currentTarget = scheduledGcsTarget;
    updateLog(QString("Scheduled GCS backup started: From '%1' to GCS Bucket "
                      "'%2' (Account: '%3')")
                  .arg(sourcePath, bucketName, accountId));

  } else if (scheduler_->isSftpMode()) {
    updateLog("Scheduled task is SFTP Mode.");
    std::map<std::string, std::string> sftpConfig;
    sftpConfig["host"] = scheduler_->sftpHost().toStdString();
    sftpConfig["port"] = QString::number(scheduler_->sftpPort()).toStdString();
    sftpConfig["username"] = scheduler_->sftpUsername().toStdString();
    sftpConfig["remoteBasePath"] = scheduler_->sftpRemotePath().toStdString();
    // Password for scheduled SFTP should be retrieved by SftpTarget from
    // CredentialManager
    SftpTarget *scheduledSftpTarget = new SftpTarget(sftpConfig);
    currentTarget = scheduledSftpTarget;
    updateLog(QString("Scheduled SFTP backup: From '%1' to host '%2'")
                  .arg(sourcePath, scheduler_->sftpHost()));
  } else { // Local Mode
    updateLog("Scheduled task is Local Mode.");
    if (destinationOrIdentifier.isEmpty()) {
      updateLog("Error: Scheduled local backup triggered but destination path "
                "is empty in Scheduler.");
      QMessageBox::critical(
          this, tr("Scheduled Backup Error"),
          tr("Destination path for scheduled local backup is missing."));
      return;
    }
    std::filesystem::path fsDestinationPathFromScheduler(
        destinationOrIdentifier.toStdString());
    // For local scheduled backup, ensure it uses the full destination path from
    // scheduler, not a sub-folder based on source.
    LocalTarget *scheduledLocalTarget =
        new LocalTarget(fsDestinationPathFromScheduler.string());
    currentTarget = scheduledLocalTarget;
    updateLog(QString("Scheduled Local backup: From '%1' to '%2'")
                  .arg(sourcePath, destinationOrIdentifier));
  }

  if (!currentTarget) {
    QMessageBox::critical(
        this, tr("Scheduled Backup Error"),
        tr("Failed to initialize backup target for scheduled task."));
    updateLog("Error: Scheduled backup failed. Target initialization failed.");
    return;
  }

  performBackupInternal(sourcePath, currentTarget);

  // Clean up the temporary target used for this scheduled backup operation
  delete currentTarget;
  currentTarget = nullptr;
  if (backupProgressBar_)
    backupProgressBar_->setVisible(false);
  statusBar()->showMessage(tr("Last backup: OK at %1")
                               .arg(QTime::currentTime().toString("HH:mm")));
}

// Remote file viewer wrappers
void MainWindow::browseRemotePath(const QString &path) {
  currentRemotePath_ = path;
  if (fileViewerWidget_)
    fileViewerWidget_->browsePath(path);
}



QString MainWindow::shortenPathForDisplay(const QString &path) const {
  QDir home = QDir::home();
  QString relative = home.relativeFilePath(path);
  if (!relative.startsWith("..")) {
    return QString("~/") + relative;
  }
  return path;
}

QString MainWindow::currentDestinationForDisplay() const {
  QString currentModeText = backupModeComboBox_->currentText();
  if (currentModeText == tr("SFTP Backup")) {
    return QString("%1:%2")
        .arg(sftpHostLineEdit_->text(), sftpRemotePathLineEdit_->text());
  } else if (currentModeText == tr("Google Cloud Storage")) {
    return QString("gcs://%1").arg(gcsBucketNameLineEdit_->text());
  }
  return destinationDirEdit_->text();
}

void MainWindow::adjustHeightToScreen() {
  QScreen *scr = QGuiApplication::primaryScreen();
  if (!scr)
    return;
  int avail = scr->availableGeometry().height();
  setMaximumHeight(avail);
  adjustSize();
  if (height() > avail)
    resize(width(), avail);
}

void MainWindow::applyUnifiedStyle(QWidget *widget) {
  if (!widget)
    return;
  if (auto layout = widget->layout()) {
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);
  }
  const auto buttons = widget->findChildren<QAbstractButton *>();
  for (QAbstractButton *b : buttons) {
    b->setMinimumHeight(24);
  }
  const auto groups = widget->findChildren<QGroupBox *>();
  for (QGroupBox *g : groups) {
    g->setStyleSheet(
        "QGroupBox{font-weight:bold;margin-top:10px;border:1px solid #ccc;"
        "border-radius:6px;padding-top:16px;background:#fafafa;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;top:-14px;"
        "background:#fafafa;padding:0 6px;color:#222;}");
  }
}


void MainWindow::updateScheduleSummary() {
  if (!scheduleSummaryLabel_ || !timeListWidget_)
    return;
  int count = 0;
  QTime earliest;
  bool first = true;
  for (int i = 0; i < timeListWidget_->count(); ++i) {
    QString data = timeListWidget_->item(i)->data(Qt::UserRole).toString();
    if (data.startsWith("WATCH|"))
      continue;
    QString timeStr = data.split('|').value(0);
    QTime t = QTime::fromString(timeStr, "HH:mm");
    if (t.isValid() && (first || t < earliest)) {
      earliest = t;
      first = false;
    }
    ++count;
  }
  QString nextText = earliest.isValid() ? earliest.toString("HH:mm") : "--:--";
  scheduleSummaryLabel_->setText(
      QString::fromUtf8("\xF0\x9F\x93\x85 %1 scheduled backup%2 | \xF0\x9F\x95\x92 Next: %3")
          .arg(count)
          .arg(count == 1 ? "" : "s")
          .arg(nextText));
}
