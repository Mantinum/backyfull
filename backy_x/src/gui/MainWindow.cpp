#include "gui/MainWindow.h"
#include "core/Scheduler.h"
#include "targets/GcsTarget.h" // Added for GCS Target
#include "targets/LocalTarget.h"
#include "targets/SftpTarget.h"
#include "util/CredentialManager.h"

#include <QApplication>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <filesystem>
#include <functional>
#include <map>
// Added for File Viewer
#include "CustomTableWidgetItems.h" // For custom sorting items
#include <QDockWidget>
#include <QHeaderView>
#include <QMenu>
#include <QMenuBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), sourceDirEdit_(nullptr), sourceDirButton_(nullptr),
      destinationDirEdit_(nullptr), destinationDirButton_(nullptr),
      destinationStack_(nullptr),
      backupTimeEdit_(nullptr), addTimeButton_(nullptr),
      timeListWidget_(nullptr), removeTimeButton_(nullptr),
      runBackupButton_(nullptr),
      logDisplay_(nullptr), scrollArea_(nullptr), backupModeComboBox_(nullptr),
      sftpSettingsGroupBox_(nullptr),
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
      fileViewerGroupBox_(nullptr), fileViewerDockWidget_(nullptr),
      fileTableWidget_(nullptr), refreshButton_(nullptr),
      downloadButton_(nullptr), deleteButton_(nullptr), currentPathLabel_(nullptr),
      currentRemotePath_("/"), // Initialize currentRemotePath_
      watchGroupBox_(nullptr),
      addWatchButton_(nullptr), watchStatusLabel_(nullptr),
      dirWatcher_(nullptr), watchTriggerTimer_(nullptr),
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

  dirWatcher_ = new QFileSystemWatcher(this);
  watchTriggerTimer_ = new QTimer(this);
  watchTriggerTimer_->setSingleShot(true);
  connect(dirWatcher_, &QFileSystemWatcher::directoryChanged, this,
          &MainWindow::onDirectoryChanged);
  connect(watchTriggerTimer_, &QTimer::timeout, this,
          &MainWindow::onWatchTimerTimeout);

  setupUI();
  loadSettings();

  connect(scheduler_, &Scheduler::backupTaskTriggered, this,
          &MainWindow::handleScheduledBackup);
  connect(scheduler_, &Scheduler::taskChanged, this,
          &MainWindow::onTaskChanged);

  if (backupModeComboBox_) {
    onBackupModeChanged(backupModeComboBox_->currentIndex());
  }
  onTaskChanged();

  updateLog("BackyFull application started.");
  updateLog(
      "Please configure your backup source, destination/SFTP, and schedule.");
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

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  if (sourceDestLayout_) {
    sourceDestLayout_->setDirection(
        width() >= 1000 ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
  }
}

void MainWindow::setupUI() {
  setWindowTitle(tr("BackyFull - Backup Configuration"));

  // Menu bar
  QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

  scrollArea_ = new QScrollArea(this);
  setCentralWidget(scrollArea_);
  scrollArea_->setWidgetResizable(true);
  QWidget *centralWidget = new QWidget(scrollArea_);
  scrollArea_->setWidget(centralWidget);

  QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
  mainLayout->setSpacing(8);
  mainLayout->setContentsMargins(12, 12, 12, 12);

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
  sourceLayout->setSpacing(8);
  sourceGroupBox->layout()->setContentsMargins(12, 12, 12, 12);
  sourceLayout->addWidget(new QLabel(tr("Source Directory:")), 0, 0);
  sourceDirEdit_ = new QLineEdit();
  sourceDirEdit_->setReadOnly(true);
  sourceLayout->addWidget(sourceDirEdit_, 0, 1);
  sourceDirButton_ = new QPushButton(tr("Browse..."));
  connect(sourceDirButton_, &QPushButton::clicked, this,
          &MainWindow::selectSourceDirectory);
  sourceLayout->addWidget(sourceDirButton_, 0, 2);

  watchGroupBox_ = new QGroupBox(tr("Automatic Folder Monitoring"));
  QGridLayout *watchLayout = new QGridLayout(watchGroupBox_);
  watchLayout->setSpacing(8);
  watchGroupBox_->layout()->setContentsMargins(12, 12, 12, 12);
  addWatchButton_ = new QPushButton(tr("Activer la surveillance automatique"));
  watchLayout->addWidget(addWatchButton_, 0, 0, 1, 3);
  watchStatusLabel_ = new QLabel(tr("Aucune surveillance"));
  watchLayout->addWidget(watchStatusLabel_, 1, 0, 1, 3);
  connect(addWatchButton_, &QPushButton::clicked, this,
          &MainWindow::onAddWatchEntry);
  sourceLayout->addWidget(watchGroupBox_, 1, 0, 1, 3);

  destinationStack_ = new DestinationStack();
  destinationDirEdit_ = destinationStack_->lineEdit();
  destinationDirEdit_->setReadOnly(true);
  destinationDirButton_ = destinationStack_->browseButton();
  connect(destinationDirButton_, &QPushButton::clicked, this,
          &MainWindow::selectDestinationDirectory);

  QGroupBox *destGroupBox =
      new QGroupBox(tr("Local Destination Configuration"));
  QVBoxLayout *destLayout = new QVBoxLayout(destGroupBox);
  destLayout->setSpacing(8);
  destGroupBox->layout()->setContentsMargins(12, 12, 12, 12);
  destLayout->addWidget(destinationStack_);

  sourceDestLayout_ = new QBoxLayout(QBoxLayout::TopToBottom);
  sourceDestLayout_->addWidget(sourceGroupBox, 1);
  sourceDestLayout_->addWidget(destGroupBox, 1);
  mainLayout->addLayout(sourceDestLayout_);
  sourceDestLayout_->setDirection(width() >= 1000 ? QBoxLayout::LeftToRight
                                                : QBoxLayout::TopToBottom);

  connect(backupModeComboBox_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          destinationStack_, &DestinationStack::setCurrentIndex);

  sftpSettingsGroupBox_ = new QGroupBox(tr("SFTP Configuration"));
  QFormLayout *sftpFormLayout = new QFormLayout(sftpSettingsGroupBox_);
  sftpFormLayout->setSpacing(8);
  sftpSettingsGroupBox_->layout()->setContentsMargins(12, 12, 12, 12);
  sftpHostLineEdit_ = new QLineEdit();
  sftpFormLayout->addRow(new QLabel(tr("SFTP Host:")), sftpHostLineEdit_);
  sftpPortLineEdit_ = new QLineEdit();
  sftpPortLineEdit_->setText("22");
  sftpPortLineEdit_->setValidator(new QIntValidator(1, 65535, this));
  sftpFormLayout->addRow(new QLabel(tr("SFTP Port:")), sftpPortLineEdit_);
  sftpUsernameLineEdit_ = new QLineEdit();
  sftpFormLayout->addRow(new QLabel(tr("SFTP Username:")),
                         sftpUsernameLineEdit_);
  sftpPasswordLineEdit_ = new QLineEdit();
  sftpPasswordLineEdit_->setEchoMode(QLineEdit::Password);
  sftpFormLayout->addRow(new QLabel(tr("SFTP Password:")),
                         sftpPasswordLineEdit_);
  sftpSavePasswordCheckBox_ = new QCheckBox(tr("Save password securely"));
  sftpFormLayout->addRow(sftpSavePasswordCheckBox_);
  sftpRemotePathLineEdit_ = new QLineEdit();
  sftpFormLayout->addRow(new QLabel(tr("SFTP Remote Path:")),
                         sftpRemotePathLineEdit_);
  sftpConnectToggleButton_ = new QPushButton(tr("Connect"));
  sftpFormLayout->addRow(sftpConnectToggleButton_);
  connect(sftpConnectToggleButton_, &QPushButton::clicked, this,
          &MainWindow::onSftpConnectToggleClicked);
  mainLayout->addWidget(sftpSettingsGroupBox_);

  gcsSettingsGroupBox_ =
      new QGroupBox(tr("Google Cloud Storage Configuration"));
  QFormLayout *gcsFormLayout = new QFormLayout(gcsSettingsGroupBox_);
  gcsFormLayout->setSpacing(8);
  gcsSettingsGroupBox_->layout()->setContentsMargins(12, 12, 12, 12);
  gcsBucketNameLineEdit_ = new QLineEdit();
  gcsFormLayout->addRow(new QLabel(tr("GCS Bucket Name:")),
                        gcsBucketNameLineEdit_);
  gcsAccountIdentifierLineEdit_ = new QLineEdit();
  gcsFormLayout->addRow(new QLabel(tr("GCS Account Identifier:")),
                        gcsAccountIdentifierLineEdit_);
  gcsConnectButton_ = new QPushButton(tr("Connect to Google Account"));
  gcsFormLayout->addRow(gcsConnectButton_);
  connect(gcsConnectButton_, &QPushButton::clicked, this,
          &MainWindow::onGcsConnectButtonClicked);
  gcsAuthStatusLabel_ = new QLabel(tr("Status: Not Authenticated"));
  gcsFormLayout->addRow(gcsAuthStatusLabel_);
  gcsTestConnectionButton_ =
      new QPushButton(tr("Test Connection")); // Instantiate and add
  gcsFormLayout->addRow(gcsTestConnectionButton_);
  connect(gcsTestConnectionButton_, &QPushButton::clicked, this,
          &MainWindow::onGcsTestConnectionClicked);
  gcsConnectToggleButton_ =
      new QPushButton(tr("Connect")); // For listing session
  gcsFormLayout->addRow(gcsConnectToggleButton_);
  connect(gcsConnectToggleButton_, &QPushButton::clicked, this,
          &MainWindow::onGcsConnectToggleClicked);
  mainLayout->addWidget(gcsSettingsGroupBox_);

  connect(backupModeComboBox_,
          QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onBackupModeChanged);

  QGroupBox *scheduleGroupBox = new QGroupBox(tr("Scheduling & Controls"));
  QGridLayout *scheduleLayout = new QGridLayout(scheduleGroupBox);
  scheduleLayout->setSpacing(8);
  scheduleGroupBox->layout()->setContentsMargins(12, 12, 12, 12);
  scheduleLayout->addWidget(new QLabel(tr("Backup Time:")), 0, 0);
  backupTimeEdit_ = new QTimeEdit();
  backupTimeEdit_->setDisplayFormat("HH:mm");
  scheduleLayout->addWidget(backupTimeEdit_, 0, 1);
  QHBoxLayout *daysLayout = new QHBoxLayout();
  const QStringList dayLabels = {tr("Mon"), tr("Tue"), tr("Wed"), tr("Thu"),
                                 tr("Fri"), tr("Sat"), tr("Sun")};
  for (int i = 0; i < 7; ++i) {
    QCheckBox *cb = new QCheckBox(dayLabels[i]);
    dayCheckBoxes_.append(cb);
    daysLayout->addWidget(cb);
  }
  scheduleLayout->addLayout(daysLayout, 1, 0, 1, 3);
  addTimeButton_ = new QPushButton(tr("Add"));
  scheduleLayout->addWidget(addTimeButton_, 0, 2);
  connect(addTimeButton_, &QPushButton::clicked, this,
          &MainWindow::onAddBackupTimeClicked);

  scheduleLayout->addWidget(new QLabel(tr("Scheduled Times:")), 2, 0, 1, 3);
  timeListWidget_ = new QListWidget();
  scheduleLayout->addWidget(timeListWidget_, 3, 0, 1, 3);
  removeTimeButton_ = new QPushButton(tr("Remove Selected"));
  scheduleLayout->addWidget(removeTimeButton_, 4, 0, 1, 3);
  connect(removeTimeButton_, &QPushButton::clicked, this,
          &MainWindow::onRemoveBackupTimeClicked);

  runBackupButton_ = new QPushButton(tr("Run Backup Now"));
  connect(runBackupButton_, &QPushButton::clicked, this,
          &MainWindow::runBackupNow);
  scheduleLayout->addWidget(runBackupButton_, 5, 0, 1, 3);
  mainLayout->addWidget(scheduleGroupBox);

  mainLayout->addWidget(new QLabel(tr("Logs:")));
  logDisplay_ = new QTextEdit();
  logDisplay_->setReadOnly(true);
  mainLayout->addWidget(logDisplay_);
  mainLayout->setStretchFactor(logDisplay_, 1);

  // File Viewer GroupBox within a DockWidget
  fileViewerGroupBox_ = new QGroupBox(tr("Remote File Viewer"));
  QVBoxLayout *fileViewerLayout =
      new QVBoxLayout(); // No parent here, will be set on the group box
  fileViewerLayout->setSpacing(8);
  fileViewerLayout->setContentsMargins(12, 12, 12, 12);

  currentPathLabel_ =
      new QLabel(tr("Path: /"), fileViewerGroupBox_);       // Parented
  fileTableWidget_ = new QTableWidget(fileViewerGroupBox_); // Parented
  fileTableWidget_->setColumnCount(4);
  QStringList headers = {tr("Name"), tr("Size"), tr("Date Modified"),
                         tr("Type")};
  fileTableWidget_->setHorizontalHeaderLabels(headers);
  fileTableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
  fileTableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  fileTableWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
  fileTableWidget_->verticalHeader()->setVisible(false);
  fileTableWidget_->horizontalHeader()->setStretchLastSection(true);
  fileTableWidget_->setSortingEnabled(true); // Enable sorting

  refreshButton_ =
      new QPushButton(tr("Refresh"), fileViewerGroupBox_); // Parented
  downloadButton_ =
      new QPushButton(tr("Download"), fileViewerGroupBox_); // Parented
  deleteButton_ =
      new QPushButton(tr("Delete"), fileViewerGroupBox_); // Parented

  QHBoxLayout *buttonLayout = new QHBoxLayout(); // No parent here
  buttonLayout->setSpacing(8);
  buttonLayout->addWidget(refreshButton_);
  buttonLayout->addWidget(downloadButton_);
  buttonLayout->addWidget(deleteButton_);
  buttonLayout->addStretch();

  fileViewerLayout->addWidget(currentPathLabel_);
  fileViewerLayout->addWidget(fileTableWidget_);
  fileViewerLayout->addLayout(buttonLayout);
  fileViewerGroupBox_->setLayout(fileViewerLayout);

  fileViewerDockWidget_ = new QDockWidget(tr("Remote File Viewer"), this);
  fileViewerDockWidget_->setWidget(fileViewerGroupBox_);
  addDockWidget(Qt::RightDockWidgetArea, fileViewerDockWidget_);
  fileViewerDockWidget_->hide();
  viewMenu->addAction(fileViewerDockWidget_->toggleViewAction());

  // Connect file viewer signals
  connect(refreshButton_, &QPushButton::clicked, this,
          &MainWindow::onFileViewerRefreshClicked);
  connect(downloadButton_, &QPushButton::clicked, this,
          &MainWindow::onFileViewerDownloadClicked);
  connect(deleteButton_, &QPushButton::clicked, this,
          &MainWindow::onFileViewerDeleteClicked);
  connect(fileTableWidget_, &QTableWidget::itemDoubleClicked, this,
          &MainWindow::onFileTableItemDoubleClicked);

  fileDialog_ = new QFileDialog(this);
  adjustHeightToScreen();
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
  for (int i = 0; i < dayCheckBoxes_.size(); ++i) {
    if (dayCheckBoxes_[i]->isChecked()) {
      dayNames << dayCheckBoxes_[i]->text();
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
  for (QCheckBox *cb : dayCheckBoxes_)
    cb->setChecked(false);
  updateScheduleFromUI();
}

void MainWindow::onRemoveBackupTimeClicked() {
  const QList<QListWidgetItem *> items = timeListWidget_->selectedItems();
  for (QListWidgetItem *item : items) {
    QString data = item->data(Qt::UserRole).toString();
    if (data.startsWith("WATCH|")) {
      QString path = data.mid(QStringLiteral("WATCH|").length());
      for (int i = 0; i < watchEntries_.size(); ++i) {
        if (watchEntries_[i].source == path) {
          dirWatcher_->removePath(path);
          watchEntries_.removeAt(i);
          break;
        }
      }
    }
    delete item;
  }
  watchStatusLabel_->setText(
      tr("%1 dossier(s) surveill\u00e9(s)").arg(watchEntries_.size()));
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

  for (const WatchEntry &e : watchEntries_) {
    if (e.source == dir) {
      QMessageBox::information(this, tr("Already Watching"),
                               tr("This directory is already being watched."));
      return;
    }
  }

  watchEntries_.append(entry);
  dirWatcher_->addPath(dir);

  QString display = QString::fromUtf8("\xF0\x9F\x93\x81 ") +
                    tr("Surveillance : %1 \u2192 %2")
                        .arg(shortenPathForDisplay(dir),
                             currentDestinationForDisplay());
  QListWidgetItem *item = new QListWidgetItem(display);
  QFont f = item->font();
  f.setItalic(true);
  item->setFont(f);
  item->setData(Qt::UserRole, QStringLiteral("WATCH|") + dir);
  timeListWidget_->addItem(item);

  watchStatusLabel_->setText(
      tr("%1 dossier(s) surveill\u00e9(s)").arg(watchEntries_.size()));
  adjustHeightToScreen();
}

void MainWindow::onDirectoryChanged(const QString &path) {
  pendingWatchPaths_.insert(path);
  watchStatusLabel_->setText(tr("Dernier changement: %1")
                                 .arg(QDateTime::currentDateTime().toString()));
  if (!watchTriggerTimer_->isActive())
    watchTriggerTimer_->start(3000);
}

void MainWindow::onWatchTimerTimeout() {
  for (const QString &p : pendingWatchPaths_) {
    for (const WatchEntry &e : watchEntries_) {
      if (e.source == p) {
        updateLog(tr("Modification détectée dans %1, lancement de la sauvegarde.")
                      .arg(e.source));
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
    }
  }
  pendingWatchPaths_.clear();
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
}

void MainWindow::refreshWatchEntriesDisplay() {
  if (!timeListWidget_)
    return;
  for (const WatchEntry &e : watchEntries_) {
    QString destDisp;
    if (e.isSftpMode) {
      destDisp = QString("%1:%2").arg(e.sftpHost, e.sftpRemotePath);
    } else if (e.isGcsMode) {
      destDisp = QString("gcs://%1").arg(e.gcsBucketName);
    } else {
      destDisp = e.destination;
    }
    destDisp = shortenPathForDisplay(destDisp);
    QString display = QString::fromUtf8("\xF0\x9F\x93\x81 ") +
                      tr("Surveillance : %1 \u2192 %2")
                          .arg(shortenPathForDisplay(e.source), destDisp);
    QListWidgetItem *item = new QListWidgetItem(display);
    QFont f = item->font();
    f.setItalic(true);
    item->setFont(f);
    item->setData(Qt::UserRole, QStringLiteral("WATCH|") + e.source);
    timeListWidget_->addItem(item);
  }
  watchStatusLabel_->setText(
      tr("%1 dossier(s) surveill\u00e9(s)").arg(watchEntries_.size()));
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
}

void MainWindow::onGcsConnectButtonClicked() {
  updateLog(
      tr("GCS 'Connect to Google Account' button clicked (OAuth process)."));
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
    if (fileTableWidget_) { // Ensure table exists
      fileTableWidget_->setRowCount(0);
    }
    currentRemotePath_ = "/";
    if (currentPathLabel_) { // Ensure label exists
      currentPathLabel_->setText(tr("Path: /"));
    }
    // QSettings settings(QCoreApplication::organizationName(),
    // QCoreApplication::applicationName());
    // settings.remove("GCS/gcs_last_authenticated_account");
  }
  // tempGcsTargetForOAuth goes out of scope here.
  // The main gcsTarget_ (for listing) is managed by onGcsConnectToggleClicked.
}

void MainWindow::onSftpConnectToggleClicked() {
  if (sftpTarget_) { // Connected for listing -> Disconnect
    updateLog(tr(
        "SFTP 'Disconnect' (viewer) button clicked. Closing viewer session."));
    sftpTarget_->endSession();
    delete sftpTarget_;
    sftpTarget_ = nullptr;
    if (fileTableWidget_) { // Check if table exists
      fileTableWidget_->setRowCount(0);
    }
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
    if (sftpConnectToggleButton_) { // Check if button exists
      sftpConnectToggleButton_->setText(tr("Connect"));
    }
    updateLog(tr("SFTP viewer session closed.")); // Specific log message
    return;
  }

  // Not connected for listing -> Connect
  updateLog(tr("SFTP 'Connect' (viewer) button clicked. Attempting to open "
               "viewer session."));
  std::map<std::string, std::string> cfg{
      {"host", sftpHostLineEdit_->text().toStdString()},
      {"port", sftpPortLineEdit_->text().toStdString()},
      {"username", sftpUsernameLineEdit_->text().toStdString()},
      {"remoteBasePath", sftpRemotePathLineEdit_->text().toStdString()}
      // Password handling relies on SftpTarget's constructor/CredentialManager
  };

  if (cfg["host"].empty() || cfg["username"].empty()) {
    QMessageBox::warning(
        this, tr("SFTP Configuration"),
        tr("SFTP Host and Username are required to connect for listing."));
    updateLog(tr("SFTP viewer connection failed: Host or Username empty."));
    // Ensure UI is in a consistent disconnected state (button text might
    // already be "Connect")
    if (sftpConnectToggleButton_) {
      sftpConnectToggleButton_->setText(tr("Connect"));
    }
    if (fileTableWidget_) {
      fileTableWidget_->setRowCount(0);
    }
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
    return;
  }

  // Ensure this sftpTarget_ is exclusively for the viewer.
  // If any old instance exists (should not happen if logic is correct), delete
  // it. delete sftpTarget_; // Not strictly needed here if logic elsewhere is
  // perfect, but safe. sftpTarget_ = nullptr; //

  sftpTarget_ =
      new SftpTarget(cfg); // MainWindow::sftpTarget_ is for the viewer
  if (!sftpTarget_->beginSession()) {
    QString err_msg = QString::fromStdString(
        sftpTarget_->getLastError()); // Get error before deleting target
    QMessageBox::critical(
        this, tr("SFTP Error"),
        tr("SFTP viewer connection failed: %1")
            .arg(err_msg.isEmpty() ? tr("Unknown error") : err_msg));
    updateLog(tr("SFTP viewer connection failed: %1")
                  .arg(err_msg.isEmpty() ? tr("Unknown error") : err_msg));
    delete sftpTarget_;
    sftpTarget_ = nullptr;
    // Ensure UI is in a consistent disconnected state
    if (sftpConnectToggleButton_) {
      sftpConnectToggleButton_->setText(tr("Connect"));
    }
    if (fileTableWidget_) {
      fileTableWidget_->setRowCount(0);
    }
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
    return;
  }

  if (sftpConnectToggleButton_) {
    sftpConnectToggleButton_->setText(tr("Disconnect"));
  }
  currentRemotePath_ = "/";
  // browseRemotePath will update currentPathLabel_
  browseRemotePath(currentRemotePath_);
  updateLog(tr("SFTP viewer session opened.")); // Specific log message
}

void MainWindow::onGcsConnectToggleClicked() {
  if (gcsTarget_) { // Connected for listing -> Disconnect
    updateLog(
        tr("GCS 'Disconnect' button clicked. Closing session for listing."));
    // We don't call endSession() if GCS target is used for active backup,
    // but for listing, it's okay. However, GcsTarget::endSession might not do
    // much if it's just a flag. The main thing is to delete the target for
    // listing. Let's assume GcsTarget destructor or endSession handles any
    // necessary cleanup.
    gcsTarget_->endSession(); // Call directly
    delete gcsTarget_;
    gcsTarget_ = nullptr;
    fileTableWidget_->setRowCount(0);
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
    gcsConnectToggleButton_->setText(tr("Connect"));
    updateLog(tr("GCS session closed for listing. OAuth status is separate."));
    // gcsAuthStatusLabel_ should NOT be changed here.
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
    fileTableWidget_->setRowCount(0);
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
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
        tr("Please use the 'Connect to Google Account' button to authenticate "
           "for account '%1' before attempting to list files.")
            .arg(accountIdFromUI));
    updateLog(tr("GCS connection for listing aborted: Account '%1' not "
                 "authenticated via 'Connect to Google Account' button, or "
                 "does not match last authenticated user ('%2').")
                  .arg(accountIdFromUI, lastAuthAccount));
    // Ensure UI is consistent
    gcsConnectToggleButton_->setText(tr("Connect"));
    fileTableWidget_->setRowCount(0);
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
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
    fileTableWidget_->setRowCount(0);
    currentRemotePath_ = "/";
    if (currentPathLabel_) {
      currentPathLabel_->setText(tr("Path: /"));
    }
    // Potentially update gcsAuthStatusLabel_ if error indicates auth failure,
    // but the plan says this button doesn't directly change it.
    // However, if beginSession fails due to auth, the user should know.
    // Perhaps GcsTarget::getLastError() can distinguish auth errors.
    // For now, keeping gcsAuthStatusLabel_ unchanged by this button directly.
    return;
  }

  gcsConnectToggleButton_->setText(tr("Disconnect"));
  currentRemotePath_ = "/"; // Set before browse
  if (currentPathLabel_) {
    currentPathLabel_->setText(tr("Path: /"));
  }
  browseRemotePath(currentRemotePath_);
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
    backupModeComboBox_->setCurrentText(tr("Google Cloud Storage"));
    gcsBucketNameLineEdit_->setText(scheduler_->gcsBucketName());
    gcsAccountIdentifierLineEdit_->setText(scheduler_->gcsAccountIdentifier());
  } else if (scheduler_->isSftpMode()) {
    backupModeComboBox_->setCurrentText(tr("SFTP Backup"));
  } else {
    backupModeComboBox_->setCurrentText(tr("Local Backup"));
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
}

void MainWindow::onBackupModeChanged(int index) {
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
  if (fileTableWidget_) {
    fileTableWidget_->setRowCount(0);
  }
  currentRemotePath_ = "/";
  if (currentPathLabel_) {
    currentPathLabel_->setText(tr("Path: /"));
  }

  // Show/hide relevant group boxes
  if (sftpSettingsGroupBox_)
    sftpSettingsGroupBox_->setVisible(sftpSelected);
  if (gcsSettingsGroupBox_)
    gcsSettingsGroupBox_->setVisible(gcsSelected);

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
      watchEntries_.append(e);
      dirWatcher_->addPath(e.source);
    }
  }
  settings.endArray();
  settings.endGroup();
  refreshWatchEntriesDisplay();

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
  for (int i = 0; i < watchEntries_.size(); ++i) {
    settings.setArrayIndex(i);
    const WatchEntry &e = watchEntries_[i];
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
}

// New slots implementation & Remote file viewer methods
void MainWindow::displayRemoteFiles(const std::vector<FileMetadata> &files) {
  fileTableWidget_->setRowCount(0); // Clear existing items

  // Add ".." navigation entry if not at root
  if (currentRemotePath_ != "/") {
    int row = fileTableWidget_->rowCount();
    fileTableWidget_->insertRow(row);

    QIcon dirIcon = QApplication::style()->standardIcon(
        QStyle::SP_ArrowUp); // Or SP_DirIcon, SP_ArrowUp is more explicit for
                             // "up"
    QTableWidgetItem *nameItem = new QTableWidgetItem(dirIcon, "..");
    nameItem->setData(Qt::UserRole, true);     // isDirectory = true
    nameItem->setData(Qt::UserRole, true);     // isDirectory = true
    nameItem->setData(Qt::UserRole + 1, ".."); // Actual name for navigation
    fileTableWidget_->setItem(row, 0, nameItem);

    SizeTableWidgetItem *sizeItem = new SizeTableWidgetItem("-");
    sizeItem->setData(Qt::UserRole, QVariant::fromValue(qlonglong(
                                        -2))); // Special value for ".." sorting
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fileTableWidget_->setItem(row, 1, sizeItem);

    DateTimeTableWidgetItem *dateItem = new DateTimeTableWidgetItem("");
    dateItem->setData(
        Qt::UserRole,
        QVariant::fromValue(
            qlonglong(0))); // Special value for ".." sorting (very old date)
    fileTableWidget_->setItem(row, 2, dateItem);

    fileTableWidget_->setItem(row, 3,
                              new QTableWidgetItem(tr("Parent Directory")));
  }

  for (const auto &file : files) {
    int row = fileTableWidget_->rowCount();
    fileTableWidget_->insertRow(row);

    QIcon icon = QApplication::style()->standardIcon(
        file.isDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
    QTableWidgetItem *nameItem =
        new QTableWidgetItem(icon, QString::fromStdString(file.name));
    nameItem->setData(Qt::UserRole, file.isDirectory);
    nameItem->setData(Qt::UserRole + 1,
                      QString::fromStdString(file.name)); // Store actual name
    fileTableWidget_->setItem(row, 0, nameItem);

    SizeTableWidgetItem *sizeItem;
    if (file.isDirectory) {
      sizeItem = new SizeTableWidgetItem("-");
      // Store a value that helps sort directories together, e.g., -1, if ".."
      // uses -2
      sizeItem->setData(Qt::UserRole, QVariant::fromValue(qlonglong(-1)));
    } else {
      // Basic size formatting (could be enhanced to KB/MB/GB)
      QString formattedSize =
          QString::number(file.size) + " B"; // Placeholder, can be improved
      sizeItem = new SizeTableWidgetItem(formattedSize,
                                         static_cast<qlonglong>(file.size));
    }
    sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    fileTableWidget_->setItem(row, 1, sizeItem);

    qint64 secs =
        static_cast<qint64>(std::chrono::duration_cast<std::chrono::seconds>(
                                file.modificationTime.time_since_epoch())
                                .count());

    QDateTime modDateTime = QDateTime::fromSecsSinceEpoch(secs);
    QString formattedDate = modDateTime.toString(Qt::TextDate);

    DateTimeTableWidgetItem *dateItem = new DateTimeTableWidgetItem(
        formattedDate, secs); // New line using qint64 secs
    fileTableWidget_->setItem(row, 2, dateItem);

    fileTableWidget_->setItem(
        row, 3,
        new QTableWidgetItem(file.isDirectory ? tr("Folder") : tr("File")));
  }
}

void MainWindow::browseRemotePath(const QString &path) {
  currentRemotePath_ = path;
  // Update the path label immediately. If listing fails or not connected, path
  // still shows intent.
  if (currentPathLabel_) {
    currentPathLabel_->setText(tr("Path: ") + currentRemotePath_);
  }
  updateLog(tr("Attempting to browse remote path: %1").arg(path));

  std::vector<FileMetadata> files;
  QString currentModeText = backupModeComboBox_->currentText();

  if (currentModeText == tr("Google Cloud Storage")) {
    if (!gcsTarget_) { // Check if GCS target for listing exists (i.e.,
                       // "Connect" was clicked)
      updateLog(tr("GCS browse attempted but not connected for listing. Please "
                   "use the 'Connect' button first."));
      // Display empty list, user sees "Connect" button.
      displayRemoteFiles({}); // Ensure view is empty
      // Optionally, could show a QMessageBox::information here, but an empty
      // table + visible Connect button is also clear.
      return;
    }
    // If gcsTarget_ exists, assume it's connected (beginSession was successful
    // in onGcsConnectToggleClicked)
    files = gcsTarget_->listFiles(path.toStdString());
    if (!gcsTarget_->getLastError().empty()) {
      QMessageBox::critical(
          this, tr("GCS Error"),
          tr("Failed to list files: %1")
              .arg(QString::fromStdString(gcsTarget_->getLastError())));
      updateLog(
          tr("GCS listFiles failed for path '%1': %2")
              .arg(path, QString::fromStdString(gcsTarget_->getLastError())));
      // files will be empty or partially filled, displayRemoteFiles will show
      // what was returned.
    }
  } else if (currentModeText == tr("SFTP Backup")) {
    if (!sftpTarget_ || !sftpTarget_->isSessionOpen()) {
      updateLog(tr("SFTP viewer not connected or session invalid. Please use "
                   "the 'Connect' button."));
      if (fileTableWidget_) {   // Ensure table exists before modifying
        displayRemoteFiles({}); // Clear view by displaying an empty list
      }
      // Optional: Consider if sftpConnectToggleButton_ text should be reset to
      // "Connect" here. If sftpTarget_ exists but session is not open, it
      // implies an inconsistent state. For now, the primary goal is to prevent
      // listFiles on an invalid session and clear the view. if
      // (sftpConnectToggleButton_ && sftpTarget_ &&
      // !sftpTarget_->isSessionOpen()) {
      //     sftpConnectToggleButton_->setText(tr("Connect"));
      // }
      return;
    }
    // If sftpTarget_ exists AND session is open...
    files = sftpTarget_->listFiles(path.toStdString());
    // Temporary diagnostic log for SFTP
    updateLog(QString("SFTP listFiles returned %1 items").arg(files.size()));

    if (!sftpTarget_->getLastError()
             .empty()) { // Assuming SftpTarget gets a getLastError()
      QMessageBox::critical(
          this, tr("SFTP Error"),
          tr("Failed to list files: %1")
              .arg(QString::fromStdString(sftpTarget_->getLastError())));
      updateLog(
          tr("SFTP listFiles failed for path '%1': %2")
              .arg(path, QString::fromStdString(sftpTarget_->getLastError())));
    }
  } else {
    // This case should ideally not be reached if fileViewerGroupBox is only
    // visible in remote modes
    updateLog(
        tr("BrowseRemotePath called in non-remote mode: %1. Clearing view.")
            .arg(currentModeText));
    displayRemoteFiles({}); // Display empty list
    return;
  }
  displayRemoteFiles(files);
  updateLog(tr("Displayed %1 files/folders for path: %2")
                .arg(files.size())
                .arg(path));
}

void MainWindow::onFileViewerRefreshClicked() {
  updateLog("File viewer refresh clicked. Current path: " + currentRemotePath_);
  browseRemotePath(currentRemotePath_);
}

void MainWindow::onFileViewerDownloadClicked() {
  // Session Checks
  QString currentModeText =
      backupModeComboBox_->currentText(); // Get current mode

  if (currentModeText == tr("SFTP Backup")) {
    if (!sftpTarget_ || !sftpTarget_->isSessionOpen()) {
      updateLog(tr("SFTP Download: Not connected or session invalid. Please "
                   "connect viewer session."));
      QMessageBox::warning(this, tr("SFTP Download Error"),
                           tr("SFTP session is not active. Please use the "
                              "'Connect' button for the SFTP viewer first."));
      return;
    }
  } else if (currentModeText == tr("Google Cloud Storage")) {
    if (!gcsTarget_) { // For GCS, gcsTarget_ existing implies an attempt to
                       // connect for listing was made. Actual token validity is
                       // handled by GCS operations.
      updateLog(tr("GCS Download: Not connected for listing. Please use the "
                   "GCS 'Connect' button for listing first."));
      QMessageBox::warning(this, tr("GCS Download Error"),
                           tr("GCS session for listing is not active. Please "
                              "use the 'Connect' (for listing) button first."));
      return;
    }
  } else {
    // Should not happen if download button is only enabled for remote modes,
    // but good to have.
    updateLog(tr("Download Error: Download is not available for the current "
                 "backup mode."));
    QMessageBox::warning(
        this, tr("Download Error"),
        tr("Download is not available for the current backup mode."));
    return;
  }

  QList<QTableWidgetItem *> selectedItems = fileTableWidget_->selectedItems();
  if (selectedItems.isEmpty() || selectedItems.first()->row() < 0) {
    QMessageBox::information(this, tr("Download"),
                             tr("No file selected or invalid selection."));
    return;
  }
  // Ensure the first item (column 0) of the selected row is used for name data
  QTableWidgetItem *nameItem =
      fileTableWidget_->item(selectedItems.first()->row(), 0);
  if (!nameItem) {
    QMessageBox::warning(this, tr("Download Error"),
                         tr("Could not retrieve item data."));
    return;
  }

  bool isDir = nameItem->data(Qt::UserRole).toBool();
  QString actualFileName =
      nameItem->data(Qt::UserRole + 1).toString(); // Actual name from metadata

  if (isDir) {
    QMessageBox::information(
        this, tr("Download"),
        tr("Folder download is not implemented for this item type."));
    return;
  }
  if (actualFileName ==
      "..") { // Should not happen if selection is managed, but good check
    QMessageBox::information(this, tr("Download"), tr("Cannot download '..'."));
    return;
  }

  QString remoteFilePath = currentRemotePath_;
  if (remoteFilePath.endsWith("/")) {
    remoteFilePath += actualFileName;
  } else {
    remoteFilePath += "/" + actualFileName;
  }
  remoteFilePath = QDir::cleanPath(remoteFilePath);

  QString localPath =
      QFileDialog::getSaveFileName(this, tr("Save File"), actualFileName);
  if (localPath.isEmpty()) {
    return; // User cancelled
  }

  updateLog(tr("Attempting to download remote file '%1' to '%2'")
                .arg(remoteFilePath, localPath));

  bool success = false;
  QString errorMsg;
  // currentModeText is already defined and checked at the top of the function.

  if (currentModeText == tr("Google Cloud Storage")) {
    // Redundant check removed: if (!gcsTarget_)
    if (gcsTarget_->downloadFile(remoteFilePath.toStdString(),
                                 localPath.toStdString())) {
      success = true;
    } else {
      errorMsg = QString::fromStdString(gcsTarget_->getLastError());
    }
  } else if (currentModeText == tr("SFTP Backup")) {
    // Redundant check removed: if (!sftpTarget_)
    // SFTP downloadFile expects path relative to its base.
    // Our currentRemotePath_ is already relative to SFTP base if m_objectPrefix
    // is used correctly by SftpTarget. Or, if currentRemotePath_ is absolute
    // from SFTP root, then SftpTarget's remotePath needs that. For now, assume
    // remoteFilePath as constructed is what SftpTarget expects.
    if (sftpTarget_->downloadFile(remoteFilePath.toStdString(),
                                  localPath.toStdString())) {
      success = true;
    } else {
      errorMsg = QString::fromStdString(sftpTarget_->getLastError());
      if (errorMsg.isEmpty()) { // Fallback if getLastError was empty but
                                // download still failed
        errorMsg = tr("SFTP download failed. Please check logs or ensure the "
                      "file path is correct and accessible.");
      }
    }
  }
  // The 'else' case for unsupported modes is handled by the checks at the top
  // of the function.

  if (success) {
    QMessageBox::information(this, tr("Download Complete"),
                             tr("File '%1' downloaded successfully to '%2'.")
                                 .arg(actualFileName, localPath));
    updateLog(tr("Successfully downloaded '%1' to '%2'.")
                  .arg(remoteFilePath, localPath));
  } else {
    QMessageBox::critical(
        this, tr("Download Failed"),
        tr("Failed to download '%1'. Error: %2").arg(actualFileName, errorMsg));
    updateLog(
        tr("Failed to download '%1'. Error: %2").arg(remoteFilePath, errorMsg));
  }
}

void MainWindow::onFileViewerDeleteClicked() {
  QList<QTableWidgetItem *> selectedItems = fileTableWidget_->selectedItems();
  if (selectedItems.isEmpty() || selectedItems.first()->row() < 0) {
    QMessageBox::information(this, tr("Delete"),
                             tr("No file selected or invalid selection."));
    return;
  }
  int selectedRow = selectedItems.first()->row();

  QTableWidgetItem *nameItemWidget =
      fileTableWidget_->item(selectedRow, 0); // Name is in column 0
  if (!nameItemWidget) {
    QMessageBox::warning(this, tr("Delete Error"),
                         tr("Could not retrieve item data for selected row %1.")
                             .arg(selectedRow));
    return;
  }

  QString actualFileName = nameItemWidget->data(Qt::UserRole + 1).toString();
  bool isDirectory = nameItemWidget->data(Qt::UserRole).toBool();

  if (actualFileName.isEmpty()) {
    QMessageBox::warning(this, tr("Delete Error"),
                         tr("Invalid or empty file name selected."));
    updateLog(
        tr("SFTP Delete: Attempted to delete an item with an empty name."));
    return;
  }
  if (actualFileName == "..") {
    QMessageBox::information(
        this, tr("Delete Error"),
        tr("Cannot delete the parent directory navigation entry."));
    return;
  }

  QString fullRemotePath = currentRemotePath_;
  if (fullRemotePath.endsWith('/')) {
    fullRemotePath += actualFileName;
  } else {
    fullRemotePath += "/" + actualFileName;
  }
  fullRemotePath = QDir::cleanPath(fullRemotePath);

  QMessageBox::StandardButton reply;
  QString itemTypeForMessage = isDirectory ? tr(" (Directory)") : "";
  reply = QMessageBox::warning(this, tr("Confirm Delete"),
                               tr("Are you sure you want to delete '%1'%2?")
                                   .arg(actualFileName, itemTypeForMessage),
                               QMessageBox::Yes | QMessageBox::No);

  if (reply == QMessageBox::No) {
    return;
  }

  updateLog(tr("User confirmed deletion of: %1 (isDirectory: %2)")
                .arg(fullRemotePath, isDirectory ? "true" : "false"));

  bool success = false;
  QString errorMsg;
  QString currentModeText = backupModeComboBox_->currentText();

  if (currentModeText == tr("Google Cloud Storage")) {
    if (!gcsTarget_) {
      QMessageBox::critical(this, tr("GCS Error"),
                            tr("GCS target not initialized. Cannot delete."));
      return;
    }
    if (isDirectory) {
      // GCS: Deleting "folders" (prefixes) is complex.
      // It requires listing all objects under the prefix and deleting them
      // individually. This is a non-trivial operation and typically not done
      // with a single "delete" call.
      errorMsg =
          tr("Deleting folders/prefixes in GCS requires deleting all contained "
             "objects and is not implemented as a single operation.");
      QMessageBox::information(this, tr("GCS Delete Info"), errorMsg);
      updateLog("GCS delete directory attempted: " + errorMsg);
      return; // Don't proceed with gcsTarget->deleteFile for directories
    }
    // Proceed with file deletion for GCS
    if (gcsTarget_->deleteFile(fullRemotePath.toStdString())) {
      success = true;
    } else {
      errorMsg = QString::fromStdString(gcsTarget_->getLastError());
    }
  } else if (currentModeText == tr("SFTP Backup")) {
    // Add this new, more comprehensive check:
    if (!sftpTarget_ || !sftpTarget_->isSessionOpen()) {
      QMessageBox::warning(
          this, tr("SFTP Delete Error"),
          tr("SFTP session is not active or invalid. Please connect first."));
      updateLog(tr("SFTP Delete: Attempted to delete when session was not "
                   "active for file '%1'.")
                    .arg(actualFileName));
      return;
    }
    if (isDirectory) {
      // SFTP: Standard SFTP 'rm' typically doesn't remove directories unless
      // empty. 'rmdir' is for empty directories. Recursive delete usually needs
      // server-side support or client-side recursion. The current
      // SftpTarget::deleteFile uses "rm", so it will likely fail for non-empty
      // dirs. We can inform the user or attempt it and let the server decide.
      updateLog(tr("Attempting to delete SFTP directory '%1' using 'rm'. This "
                   "may only work for empty directories or if server allows "
                   "'rm' on dirs.")
                    .arg(fullRemotePath));
      // Proceed to call deleteFile, server error will be reported if it fails.
    }
    if (sftpTarget_->deleteFile(fullRemotePath.toStdString())) {
      success = true;
    } else {
      errorMsg =
          tr("SFTP delete failed. Check logs. The server might not allow 'rm' "
             "on directories or the directory was not empty.");
      // TODO: SftpTarget should ideally provide specific error via
      // getLastError()
    }
  } else {
    QMessageBox::warning(
        this, tr("Delete Error"),
        tr("Delete is not supported for the current backup mode."));
    return;
  }

  if (success) {
    QMessageBox::information(
        this, tr("Delete Successful"),
        tr("'%1' deleted successfully.").arg(actualFileName));
    updateLog(tr("Successfully deleted '%1'.").arg(fullRemotePath));
    if (selectedRow >= 0 && selectedRow < fileTableWidget_->rowCount()) {
      fileTableWidget_->removeRow(selectedRow);
      updateLog(tr("Removed item from view at row %1.").arg(selectedRow));
    } else {
      updateLog(tr("Could not remove item from view, row %1 invalid or table "
                   "changed. Forcing refresh.")
                    .arg(selectedRow));
      onFileViewerRefreshClicked(); // Fallback to refresh
    }
  } else {
    QMessageBox::critical(
        this, tr("Delete Failed"),
        tr("Failed to delete '%1'. Error: %2").arg(actualFileName, errorMsg));
    updateLog(
        tr("Failed to delete '%1'. Error: %2").arg(fullRemotePath, errorMsg));
  }
}

void MainWindow::onFileTableItemDoubleClicked(QTableWidgetItem *item) {
  if (!item)
    return;

  // Ensure we are using the data from column 0 (Name column) for path
  // construction
  QTableWidgetItem *nameColItem = fileTableWidget_->item(item->row(), 0);
  if (!nameColItem)
    return; // Should not happen if item itself is valid

  bool isDir = nameColItem->data(Qt::UserRole).toBool();
  QString itemName = nameColItem->data(Qt::UserRole + 1)
                         .toString(); // Get actual name from metadata

  if (isDir) {
    if (itemName == "..") {
      QDir dir(currentRemotePath_);
      if (dir.cdUp()) {
        QString parentPath = dir.path();
        // Ensure root path is just "/"
        if (parentPath.isEmpty() || parentPath == "." || parentPath == "//") {
          parentPath = "/";
        }
        // QDir might return "/." for parent of "/foo", clean it.
        if (parentPath.endsWith("/.")) {
          parentPath = parentPath.left(parentPath.length() - 2);
          if (parentPath.isEmpty())
            parentPath = "/";
        }
        updateLog("Navigating up to: " + parentPath);
        browseRemotePath(parentPath);
      } else {
        updateLog("Could not navigate up from: " + currentRemotePath_);
        browseRemotePath("/"); // Go to root if cdUp fails strangely
      }
    } else {
      QString newPath;
      if (currentRemotePath_ == "/") {
        newPath = "/" + itemName;
      } else {
        newPath = currentRemotePath_ + "/" + itemName;
      }
      // Clean path to remove any potential double slashes, though
      // QDir::filePath or manual construction should be careful
      newPath = QDir::cleanPath(newPath);
      updateLog(QString("Navigating into directory: %1 (New path: %2)")
                    .arg(itemName, newPath));
      browseRemotePath(newPath);
    }
  } else {
    // Double-clicking a file could trigger download, view, etc.
    // For now, let's call the download action.
    updateLog(
        QString("Double-clicked file: %1. Triggering download.").arg(itemName));
    onFileViewerDownloadClicked();
  }
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
