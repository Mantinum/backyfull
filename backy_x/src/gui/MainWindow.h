#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

// Qt class includes
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimeEdit>
#include <QFileDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QCheckBox>
#include <QLabel> // Added for gcsAuthStatusLabel_

// Forward declaration for Scheduler
class Scheduler;
#include "util/CredentialManager.h" // For CredentialManager
#include <memory> // For std::unique_ptr

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

    // GCS Settings
    QGroupBox *gcsSettingsGroupBox_;
    QLineEdit *gcsBucketNameLineEdit_;
    QLineEdit *gcsAccountIdentifierLineEdit_;
    QPushButton *gcsConnectButton_;
    QPushButton *gcsTestConnectionButton_; // Added
    QLabel *gcsAuthStatusLabel_;

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
};

#endif // MAINWINDOW_H
