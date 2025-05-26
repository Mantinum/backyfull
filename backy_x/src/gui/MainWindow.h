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

// Forward declaration for our Scheduler
class Scheduler; 
// Forward declaration for IStorageTarget and LocalTarget
class IStorageTarget;
class LocalTarget;


class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void selectSourceDirectory();
    void selectDestinationDirectory();
    void applySchedule();
    void runBackupNow();
    void updateLog(const QString& message); // Slot to receive log messages
    void onTaskChanged(); // Slot to react to Scheduler's taskChanged signal

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

    // Core components
    Scheduler *scheduler_;
    // For M1, we'll directly use LocalTarget. Later, BackupEngine would manage IStorageTarget.
    LocalTarget *localTarget_; // Will be initialized with destination path from UI

    // Helper for file dialogs
    QFileDialog *fileDialog_;

    // Internal helper to perform the backup logic
    void performBackup(const QString& sourcePath, const QString& destinationPath);
};

#endif // MAINWINDOW_H
