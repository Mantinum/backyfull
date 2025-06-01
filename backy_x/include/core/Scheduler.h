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
    // Configures the daily backup task
    void setDailyBackupTask(const QString& sourcePath, 
                            const QString& destinationPathOrIdentifier, // For local, it's path. For SFTP, it might be a descriptive string or empty.
                            const QTime& scheduledTime, 
                            bool enabled, 
                            bool isSftpMode, 
                            const QString& sftpHost = QString(), 
                            int sftpPort = 22, 
                            const QString& sftpUsername = QString(), 
                            const QString& sftpRemotePath = QString(),
                            bool isGcsMode = false, // New GCS parameter
                            const QString& gcsBucketName = QString(), // New GCS parameter
                            const QString& gcsObjectPrefix = QString()); // New GCS parameter (renamed from m_gcsPrefix)

    // Loads task from QSettings
    void loadTask();
    
    QString sourcePath() const;
    QString destinationPath() const; // For local mode, or descriptive identifier for SFTP
    QTime scheduledTime() const;
    bool isEnabled() const;
    bool isSftpMode() const; // Getter for SFTP mode

    // Getters for SFTP configuration
    QString sftpHost() const;
    int sftpPort() const;
    QString sftpUsername() const;
    QString sftpRemotePath() const;

    // Getters for GCS configuration
    bool isGcsMode() const;
    QString gcsBucketName() const;
    QString gcsObjectPrefix() const;
    QString gcsAccountIdentifier() const;

signals:
    // Emitted when a scheduled backup is due
    // The slot connected to this in MainWindow will use Scheduler's getters to fetch full config
    void backupTaskTriggered(const QString& sourcePath, const QString& destinationOrIdentifier);
    // Emitted when task details change, so UI can update
    void taskChanged(); 

private slots:
    void checkScheduledTime(); // Slot for the QTimer to call

private:
    void saveTask(); // Saves current task to QSettings
    void scheduleNextCheck(); // Helper to arm the timer for the next day or initial check

    QString m_currentSourcePath;
    QString m_currentDestinationPath; // For local, this is the path. For SFTP, this might be a placeholder or unused if SFTP details are separate.
    QTime m_currentScheduledTime;
    bool m_taskEnabled;
    
    // SFTP specific settings
    bool m_isSftpMode;
    QString m_sftpHost;
    int m_sftpPort;
    QString m_sftpUsername;
    QString m_sftpRemotePath;

    // GCS specific settings
    bool m_isGcsMode;
    QString m_gcsBucketName;
    QString m_gcsObjectPrefix; // Renamed from m_gcsPrefix

    QTimer* m_dailyTimer; // Timer to check if backup is due
    QSettings* m_settings; // For persisting task details

    // QSettings keys
    const QString SETTINGS_GROUP = "Scheduler";
    const QString KEY_SOURCE_PATH = "sourcePath";
    const QString KEY_DEST_PATH = "destinationPath"; // Or "destinationIdentifier"
    const QString KEY_SCHEDULED_TIME = "scheduledTime";
    const QString KEY_TASK_ENABLED = "taskEnabled";
    const QString KEY_IS_SFTP_MODE = "isSftpMode";
    const QString KEY_SFTP_HOST = "sftpHost";
    const QString KEY_SFTP_PORT = "sftpPort";
    const QString KEY_SFTP_USERNAME = "sftpUsername";
    const QString KEY_SFTP_REMOTE_PATH = "sftpRemotePath";
    const QString KEY_IS_GCS_MODE = "isGcsMode";
    const QString KEY_GCS_BUCKET_NAME = "gcsBucketName";
    const QString KEY_GCS_OBJECT_PREFIX = "gcsObjectPrefix";
};

#endif // SCHEDULER_H
