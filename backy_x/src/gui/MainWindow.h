#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

// Qt class includes
#include <QCheckBox>
#include <QToolButton>
#include <QAbstractButton>
#include <QComboBox>
#include <QStackedWidget>
#include <QProgressBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel> // Added for gcsAuthStatusLabel_
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QTimeEdit>
#include <QScrollArea>

// Forward declaration for Scheduler
class Scheduler;
#include "core/Job.h"
#include "core/IStorageTarget.h"    // For FileMetadata
#include "util/CredentialManager.h" // For CredentialManager
#include "gui/WatchManager.h"
#include <memory>                   // For std::unique_ptr

// Forward declaration for Storage Targets
class IStorageTarget;
class LocalTarget;
class SftpTarget;
class GcsTarget; // Forward declare GcsTarget
class FileViewerWidget;
class WatchManager;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent *event) override;

private slots:
  void selectSourceDirectory();
  void selectDestinationDirectory();
  void runBackupNow();
  void updateLog(const QString &message);
  void onTaskChanged();
  void onBackupModeChanged(int index);
  void handleScheduledBackup(const QString &sourcePath,
                             const QString &destinationOrIdentifier);
  void onGcsConnectButtonClicked();
  void onGcsTestConnectionClicked(); // Slot for GCS Test Connection
  void onSftpConnectToggleClicked();
  void onGcsConnectToggleClicked();
  // Slots for File Viewer handled by FileViewerWidget
  void onAddBackupTimeClicked();
  void onRemoveBackupTimeClicked();
  void onRemoveSelectedJob();

  // Auto watch slots
  void onAddWatchEntry();
  void onWatchToggled(bool checked);
  void updateWatchStatusIcon();
  void handleWatchTriggered(const WatchEntry &entry);

private:
  Ui::MainWindow *ui{nullptr};
  void setupUI();
  void loadSettings();
  void saveSettings();
  void updateScheduleFromUI();
  void refreshWatchEntriesDisplay();

  // UI Elements
  QLineEdit *sourceDirEdit_;
  QPushButton *sourceDirButton_;
  QLineEdit *destinationDirEdit_;
  QPushButton *destinationDirButton_;
  QTimeEdit *backupTimeEdit_;
  QList<QAbstractButton *> dayButtons_;
  QToolButton *addTimeButton_;
  QListWidget *timeListWidget_;
  QPushButton *removeTimeButton_;
  QPushButton *runBackupButton_;
  QTableView *tvJobs_ = nullptr;
  QPushButton *btnRunNow_ = nullptr;
  QPushButton *btnRemoveSelected_ = nullptr;
  JobsModel *jobsModel_ = nullptr;
  QLabel *scheduleSummaryLabel_{nullptr};
  QTextEdit *logDisplay_;

  QScrollArea *scrollArea_ = nullptr;

  // Backup Mode Selection
  QComboBox *backupModeComboBox_;
  QStackedWidget *backupModeStackedWidget_;

  QProgressBar *backupProgressBar_{nullptr};

  // Local Backup Settings
  QGroupBox *m_localDestinationGroupBox;

  // SFTP Settings
  QGroupBox *sftpSettingsGroupBox_;
  QLineEdit *sftpHostLineEdit_;
  QLineEdit *sftpPortLineEdit_;
  QLineEdit *sftpUsernameLineEdit_;
  QLineEdit *sftpPasswordLineEdit_;
  QLineEdit *sftpRemotePathLineEdit_;
  QCheckBox *sftpSavePasswordCheckBox_;
  QPushButton *sftpConnectToggleButton_{nullptr};

  // GCS Settings
  QGroupBox *gcsSettingsGroupBox_;
  QLineEdit *gcsBucketNameLineEdit_;
  QLineEdit *gcsAccountIdentifierLineEdit_;
  QPushButton *gcsConnectButton_;
  QPushButton *gcsTestConnectionButton_; // Added
  QLabel *gcsAuthStatusLabel_;
  QPushButton *gcsConnectToggleButton_{nullptr};

  // File Viewer UI Elements
  QDockWidget *fileViewerDockWidget_ = nullptr;
  FileViewerWidget *fileViewerWidget_ = nullptr;
  QString currentRemotePath_ = "/";

  // Auto Watch UI
  QCheckBox *cbWatch_ = nullptr;
  QLabel *lblWatchStatus_ = nullptr;
  QSpinBox *sbWatchInterval_ = nullptr;
  QLabel *lblLastEvent_ = nullptr;

  WatchManager *watchManager_ = nullptr;

  // Core components
  Scheduler *scheduler_;
  LocalTarget *localTarget_;
  SftpTarget *sftpTarget_;
  GcsTarget *gcsTarget_; // GCS Target instance

  std::unique_ptr<CredentialManager> m_credentialManager;

  // Helper for file dialogs
  QFileDialog *fileDialog_;

  // Internal helper to perform the backup logic
  void performBackupInternal(const QString &sourcePath, IStorageTarget *target);

  // Remote File Viewer methods
  void browseRemotePath(const QString &path);

  QString shortenPathForDisplay(const QString &path) const;
  QString currentDestinationForDisplay() const;

  void adjustHeightToScreen();
  void applyUnifiedStyle(QWidget *widget);
  void updateScheduleSummary();
};

#endif // MAINWINDOW_H
