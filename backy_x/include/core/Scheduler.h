#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QObject>
#include <QTime>
#include <QString> // For source/destination paths
#include <QTimer>
#include <QSettings> // For persistence

// Forward declaration if BackupTask struct becomes complex, for now just basic info
// struct BackupTask {
//     QString id; // Might not be needed for M1 with a single task
//     QString sourcePath;
//     QString destinationPath;
//     QTime scheduledTime; // Daily at this time
//     bool enabled;
// };

class Scheduler : public QObject {
    Q_OBJECT

public:
    explicit Scheduler(const QString& settingsFilePath = QString(), QObject *parent = nullptr);
    ~Scheduler() override;

    // Configures the daily backup task
    void setDailyBackupTask(const QString& sourcePath, const QString& destinationPath, const QTime& scheduledTime, bool enabled = true);

    // Loads task from QSettings
    void loadTask();
    
    // Manually trigger the backup (for "Run Now" button)
    // This will emit the triggerBackup signal immediately with current task's paths
    // Q_INVOKABLE void triggerBackupNow(); // Q_INVOKABLE if called from QML, not strictly needed for C++ signals

    QString sourcePath() const;
    QString destinationPath() const;
    QTime scheduledTime() const;
    bool isEnabled() const;

signals:
    // Emitted when a scheduled backup is due
    void backupTaskTriggered(const QString& sourcePath, const QString& destinationPath);
    // Emitted when task details change, so UI can update
    void taskChanged(); 

private slots:
    void checkScheduledTime(); // Slot for the QTimer to call

private:
    void saveTask(); // Saves current task to QSettings
    void scheduleNextCheck(); // Helper to arm the timer for the next day or initial check

    QString currentSourcePath_;
    QString currentDestinationPath_;
    QTime currentScheduledTime_;
    bool taskEnabled_;

    QTimer* dailyTimer_; // Timer to check if backup is due
    QSettings* settings_; // For persisting task details

    const QString SETTINGS_GROUP = "Scheduler";
    const QString KEY_SOURCE_PATH = "sourcePath";
    const QString KEY_DEST_PATH = "destinationPath";
    const QString KEY_SCHEDULED_TIME = "scheduledTime";
    const QString KEY_TASK_ENABLED = "taskEnabled";
};

#endif // SCHEDULER_H
