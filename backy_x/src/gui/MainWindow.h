#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

// Qt class includes (replacing forward declarations)
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QTimeEdit>
#include <QFileDialog>
#include <QComboBox>     // For m_backupModeComboBox
#include <QGroupBox>     // For m_sftpSettingsGroupBox
#include <QCheckBox>     // For m_sftpSavePasswordCheckBox
// QLineEdit is already included
// QLabel will be included in .cpp or here if needed for direct use

// Forward declaration for our Scheduler
class Scheduler;
#include "util/CredentialManager.h" // For CredentialManager
#include <memory> // For std::unique_ptr

// Forward declaration for IStorageTarget and LocalTarget
class IStorageTarget;
class LocalTarget;
class SftpTarget; // Forward declare SftpTarget
// Forward declarations for layout classes used in setupUI if not included,
// but they are mostly implementation details of .cpp
// class QFormLayout;
// class QVBoxLayout;


class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override; // To save settings on close

private slots:
    void selectSourceDirectory();
    void selectDestinationDirectory();
    void applySchedule();
    void runBackupNow();
    void updateLog(const QString& message); // Slot to receive log messages
    void onTaskChanged(); // Slot to react to Scheduler's taskChanged signal
    void onBackupModeChanged(int index); // Slot for backup mode change
    void handleScheduledBackup(const QString& sourcePath, const QString& destinationOrIdentifier); // Slot for scheduled backups

private:
    void setupUI(); // Helper to create and layout widgets
    void loadSettings(); // Load window geometry and potentially other UI settings
    void saveSettings(); // Save window geometry

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
    QGroupBox *m_localDestinationGroupBox; // Declaration for the local destination group box
    // QLabel *destinationDirLabel_; // Will be changed based on mode

    // SFTP Settings
    QGroupBox *sftpSettingsGroupBox_;
    QLineEdit *sftpHostLineEdit_;
    QLineEdit *sftpPortLineEdit_;
    QLineEdit *sftpUsernameLineEdit_;
    QLineEdit *sftpPasswordLineEdit_;
    QLineEdit *sftpRemotePathLineEdit_;
    QCheckBox *sftpSavePasswordCheckBox_;


    // Core components
    Scheduler *scheduler_;
    // For M1, we'll directly use LocalTarget. Later, BackupEngine would manage IStorageTarget.
    LocalTarget *localTarget_; 
    SftpTarget *sftpTarget_; // Will be initialized based on mode

    std::unique_ptr<CredentialManager> m_credentialManager;

    // Helper for file dialogs
    QFileDialog *fileDialog_;

    // Internal helper to perform the backup logic
    // This will use the currently active target (localTarget_ or sftpTarget_)
    void performBackupInternal(const QString& sourcePath, IStorageTarget* target);
};

#endif // MAINWINDOW_H
