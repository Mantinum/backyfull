#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

// Qt class includes
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimeEdit>
#include <QListWidget>
#include <QFileDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QDockWidget>
#include <QCheckBox>
#include <QLabel> // Added for gcsAuthStatusLabel_
// Forward declarations for Qt UI elements related to File Viewer
QT_BEGIN_NAMESPACE
class QTableWidget;
class QTableWidgetItem;
QT_END_NAMESPACE

// Forward declaration for Scheduler
class Scheduler;
#include "util/CredentialManager.h" // For CredentialManager
#include <memory> // For std::unique_ptr
#include "core/IStorageTarget.h" // For FileMetadata

// Forward declaration for Storage Targets
class IStorageTarget;
class LocalTarget;
class SftpTarget;
class GcsTarget; // Forward declare GcsTarget

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
    void applySchedule();
    void runBackupNow();
    void updateLog(const QString& message);
    void onTaskChanged();
    void onBackupModeChanged(int index);
    void handleScheduledBackup(const QString& sourcePath, const QString& destinationOrIdentifier);
    void onGcsConnectButtonClicked();
    void onGcsTestConnectionClicked(); // Slot for GCS Test Connection
    void onSftpConnectToggleClicked();
    void onGcsConnectToggleClicked();
    // Slots for File Viewer
    void onFileViewerRefreshClicked();
    void onFileViewerDownloadClicked();
    void onFileViewerDeleteClicked();
    void onFileTableItemDoubleClicked(QTableWidgetItem *item);
    void onAddBackupTimeClicked();
    void onRemoveBackupTimeClicked();

private:
    void setupUI();
    void loadSettings();
    void saveSettings();

    // UI Elements
    QLineEdit *sourceDirEdit_;
    QPushButton *sourceDirButton_;
    QLineEdit *destinationDirEdit_;
    QPushButton *destinationDirButton_;
    QTimeEdit *backupTimeEdit_;
    QPushButton *addTimeButton_;
    QListWidget *timeListWidget_;
    QPushButton *removeTimeButton_;
    QPushButton *applyScheduleButton_;
    QPushButton *runBackupButton_;
    QTextEdit *logDisplay_;

    // Backup Mode Selection
    QComboBox *backupModeComboBox_;

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
    QGroupBox *fileViewerGroupBox_ = nullptr;
    QDockWidget *fileViewerDockWidget_ = nullptr;
    QTableWidget *fileTableWidget_ = nullptr;
    QPushButton *refreshButton_ = nullptr;
    QPushButton *downloadButton_ = nullptr;
    QPushButton *deleteButton_ = nullptr;
    QLabel *currentPathLabel_ = nullptr;
    QString currentRemotePath_ = "/";

    // Core components
    Scheduler *scheduler_;
    LocalTarget *localTarget_; 
    SftpTarget *sftpTarget_;
    GcsTarget *gcsTarget_; // GCS Target instance

    std::unique_ptr<CredentialManager> m_credentialManager;

    // Helper for file dialogs
    QFileDialog *fileDialog_;

    // Internal helper to perform the backup logic
    void performBackupInternal(const QString& sourcePath, IStorageTarget* target);

    // Remote File Viewer methods
    void displayRemoteFiles(const std::vector<FileMetadata>& files);
    void browseRemotePath(const QString& path);
};

#endif // MAINWINDOW_H
