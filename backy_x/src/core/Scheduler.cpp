#include "core/Scheduler.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>

Scheduler::Scheduler(const QString& settingsFilePath, QObject *parent)
    : QObject(parent),
      m_taskEnabled(false),
      m_isSftpMode(false),
      m_sftpPort(22),
      m_isGcsMode(false),
      m_dailyTimer(new QTimer(this)),
      m_settings(nullptr)
{
    if (settingsFilePath.isEmpty()) {
        if (QCoreApplication::organizationName().isEmpty()) {
            QCoreApplication::setOrganizationName("BackyFullOrgTestDefault");
        }
        if (QCoreApplication::applicationName().isEmpty()) {
            QCoreApplication::setApplicationName("BackyFullTestDefault");
        }
        m_settings = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                   QCoreApplication::organizationName(),
                                   QCoreApplication::applicationName(), this);
    } else {
        m_settings = new QSettings(settingsFilePath, QSettings::IniFormat, this);
    }
    
    connect(m_dailyTimer, &QTimer::timeout, this, &Scheduler::checkScheduledTime);
    loadTask(); 
}

Scheduler::~Scheduler() {
    // m_dailyTimer and m_settings are children, Qt handles cleanup.
}

void Scheduler::setDailyBackupTask(const QString& sourcePath,
                                   const QString& destinationPathOrIdentifier,
                                   const QList<QTime>& scheduledTimes,
                                   bool enabled,
                                   bool isSftpMode,
                                   const QString& sftpHost,
                                   int sftpPort,
                                   const QString& sftpUsername,
                                   const QString& sftpRemotePath,
                                   bool isGcsMode,
                                   const QString& gcsBucketName,
                                   const QString& gcsObjectPrefix) {
    m_currentSourcePath = sourcePath;
    m_currentDestinationPath = destinationPathOrIdentifier; // Store identifier or local path
    m_scheduledTimes = scheduledTimes;
    m_taskEnabled = enabled;
    m_isSftpMode = isSftpMode;
    m_isGcsMode = isGcsMode;

    if (m_isSftpMode) {
        m_sftpHost = sftpHost;
        m_sftpPort = sftpPort;
        m_sftpUsername = sftpUsername;
        m_sftpRemotePath = sftpRemotePath;
        // Clear GCS and Local settings if SFTP mode is active
        m_isGcsMode = false;
        m_gcsBucketName.clear();
        m_gcsObjectPrefix.clear();
        if (!m_isGcsMode) m_currentDestinationPath.clear(); // Clear local path if not GCS (SFTP uses destinationOrIdentifier differently)

    } else if (m_isGcsMode) {
        m_gcsBucketName = gcsBucketName;
        m_gcsObjectPrefix = gcsObjectPrefix;
        // Clear SFTP and Local settings if GCS mode is active
        m_isSftpMode = false;
        m_sftpHost.clear();
        m_sftpPort = 22;
        m_sftpUsername.clear();
        m_sftpRemotePath.clear();
        // For GCS, destinationPathOrIdentifier can be used to store bucket name or a GCS specific identifier
        // Or it can be cleared if gcsBucketName is the sole source of truth for bucket.
        // Let's assume for now destinationPathOrIdentifier is used for GCS as well.
        // m_currentDestinationPath = gcsBucketName; // Or some other logic

    } else { // Local mode
        // Clear SFTP and GCS details
        m_sftpHost.clear();
        m_sftpPort = 22;
        m_sftpUsername.clear();
        m_sftpRemotePath.clear();
        m_gcsBucketName.clear();
        m_gcsObjectPrefix.clear();
    }

    saveTask();
    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::loadTask() {
    m_settings->beginGroup(SETTINGS_GROUP);
    m_currentSourcePath = m_settings->value(KEY_SOURCE_PATH, "").toString();
    m_currentDestinationPath = m_settings->value(KEY_DEST_PATH, "").toString();
    QStringList timeStrings = m_settings->value(KEY_SCHEDULED_TIMES, QStringList({"23:00"})).toStringList();
    m_scheduledTimes.clear();
    for (const QString& ts : timeStrings) {
        QTime t = QTime::fromString(ts, "HH:mm");
        if (t.isValid()) m_scheduledTimes.append(t);
    }
    m_taskEnabled = m_settings->value(KEY_TASK_ENABLED, false).toBool();
    m_isSftpMode = m_settings->value(KEY_IS_SFTP_MODE, false).toBool();
    m_isGcsMode = m_settings->value(KEY_IS_GCS_MODE, false).toBool();

    if (m_isSftpMode) {
        m_sftpHost = m_settings->value(KEY_SFTP_HOST, "").toString();
        m_sftpPort = m_settings->value(KEY_SFTP_PORT, 22).toInt();
        m_sftpUsername = m_settings->value(KEY_SFTP_USERNAME, "").toString();
        m_sftpRemotePath = m_settings->value(KEY_SFTP_REMOTE_PATH, "").toString();
    } else if (m_isGcsMode) {
        m_gcsBucketName = m_settings->value(KEY_GCS_BUCKET_NAME, "").toString();
        m_gcsObjectPrefix = m_settings->value(KEY_GCS_OBJECT_PREFIX, "").toString();
    }
    // No specific 'else' for local mode here, as its primary setting is m_currentDestinationPath

    m_settings->endGroup();

    qDebug() << "Scheduler: Loaded task -"
             << "Source:" << m_currentSourcePath
             << "Dest/ID:" << m_currentDestinationPath
             << "Times:" << m_scheduledTimes
             << "Enabled:" << m_taskEnabled
             << "SFTP Mode:" << m_isSftpMode
             << "GCS Mode:" << m_isGcsMode;
    if (m_isSftpMode) {
        qDebug() << "SFTP Details - Host:" << m_sftpHost << "Port:" << m_sftpPort << "User:" << m_sftpUsername << "Path:" << m_sftpRemotePath;
    }
    if (m_isGcsMode) {
        qDebug() << "GCS Details - Bucket:" << m_gcsBucketName << "Prefix:" << m_gcsObjectPrefix;
    }

    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::saveTask() {
    m_settings->beginGroup(SETTINGS_GROUP);
    m_settings->setValue(KEY_SOURCE_PATH, m_currentSourcePath);
    m_settings->setValue(KEY_DEST_PATH, m_currentDestinationPath);
    QStringList timeStrings;
    for (const QTime& t : m_scheduledTimes) {
        timeStrings << t.toString("HH:mm");
    }
    m_settings->setValue(KEY_SCHEDULED_TIMES, timeStrings);
    m_settings->setValue(KEY_TASK_ENABLED, m_taskEnabled);
    m_settings->setValue(KEY_IS_SFTP_MODE, m_isSftpMode);
    m_settings->setValue(KEY_IS_GCS_MODE, m_isGcsMode);

    if (m_isSftpMode) {
        m_settings->setValue(KEY_SFTP_HOST, m_sftpHost);
        m_settings->setValue(KEY_SFTP_PORT, m_sftpPort);
        m_settings->setValue(KEY_SFTP_USERNAME, m_sftpUsername);
        m_settings->setValue(KEY_SFTP_REMOTE_PATH, m_sftpRemotePath);
        // Remove GCS and Local keys
        m_settings->remove(KEY_IS_GCS_MODE);
        m_settings->remove(KEY_GCS_BUCKET_NAME);
        m_settings->remove(KEY_GCS_OBJECT_PREFIX);
        // KEY_DEST_PATH is used by SFTP as an identifier, so don't remove it based on SFTP mode alone.
        // However, if it's truly SFTP, local destination path might be irrelevant.
        // For now, let's assume setDailyBackupTask correctly sets m_currentDestinationPath for SFTP.

    } else if (m_isGcsMode) {
        m_settings->setValue(KEY_GCS_BUCKET_NAME, m_gcsBucketName);
        m_settings->setValue(KEY_GCS_OBJECT_PREFIX, m_gcsObjectPrefix);
        // Remove SFTP and Local keys (if m_currentDestinationPath is not used for GCS identifier)
        m_settings->remove(KEY_SFTP_HOST);
        m_settings->remove(KEY_SFTP_PORT);
        m_settings->remove(KEY_SFTP_USERNAME);
        m_settings->remove(KEY_SFTP_REMOTE_PATH);
        // If m_currentDestinationPath is purely for local mode, remove it.
        // If it's also an identifier for GCS, this logic needs refinement.
        // For now, assuming KEY_DEST_PATH might be cleared by setDailyBackupTask if GCS doesn't use it.
        // Based on current setDailyBackupTask, it's not cleared for GCS, so we don't remove KEY_DEST_PATH here.

    } else { // Local mode
        // Remove SFTP and GCS keys
        m_settings->remove(KEY_SFTP_HOST);
        m_settings->remove(KEY_SFTP_PORT);
        m_settings->remove(KEY_SFTP_USERNAME);
        m_settings->remove(KEY_SFTP_REMOTE_PATH);
        m_settings->remove(KEY_IS_GCS_MODE);
        m_settings->remove(KEY_GCS_BUCKET_NAME);
        m_settings->remove(KEY_GCS_OBJECT_PREFIX);
        // KEY_DEST_PATH is for local mode here.
    }
    m_settings->endGroup();
    m_settings->sync(); 

    qDebug() << "Scheduler: Saved task -"
             << "Source:" << m_currentSourcePath
             << "Dest/ID:" << m_currentDestinationPath
             << "Times:" << m_scheduledTimes
             << "Enabled:" << m_taskEnabled
             << "SFTP Mode:" << m_isSftpMode
             << "GCS Mode:" << m_isGcsMode;
    if (m_isSftpMode) {
        qDebug() << "SFTP Details - Host:" << m_sftpHost << "Port:" << m_sftpPort << "User:" << m_sftpUsername << "Path:" << m_sftpRemotePath;
    }
    if (m_isGcsMode) {
        qDebug() << "GCS Details - Bucket:" << m_gcsBucketName << "Prefix:" << m_gcsObjectPrefix;
    }
}

void Scheduler::scheduleNextCheck() {
    m_dailyTimer->stop();
    bool sftpConfigOk = !m_isSftpMode || (!m_sftpHost.isEmpty() && !m_sftpUsername.isEmpty() && !m_sftpRemotePath.isEmpty());
    bool gcsConfigOk = !m_isGcsMode || !m_gcsBucketName.isEmpty(); // Object prefix can be empty
    bool localConfigOk = (!m_isSftpMode && !m_isGcsMode) ? !m_currentDestinationPath.isEmpty() : true; // Only check if local mode

    if (!m_taskEnabled || m_scheduledTimes.isEmpty() || m_currentSourcePath.isEmpty() || !sftpConfigOk || !gcsConfigOk || !localConfigOk) {
        qDebug() << "Scheduler: Task disabled or not fully configured for the current mode, not scheduling.";
        if (!m_taskEnabled) qDebug() << "Reason: Task not enabled.";
        if (m_scheduledTimes.isEmpty()) qDebug() << "Reason: No scheduled times.";
        if (m_currentSourcePath.isEmpty()) qDebug() << "Reason: Source path empty.";
        if (m_isSftpMode && !sftpConfigOk) qDebug() << "Reason: SFTP mode active but not fully configured.";
        if (m_isGcsMode && !gcsConfigOk) qDebug() << "Reason: GCS mode active but bucket name empty.";
        if (!m_isSftpMode && !m_isGcsMode && !localConfigOk) qDebug() << "Reason: Local mode active but destination path empty.";
        return;
    }
    QDateTime currentDateTime = QDateTime::currentDateTime();
    QDateTime nextDateTime;
    bool found = false;
    for (const QTime& t : m_scheduledTimes) {
        if (!t.isValid()) continue;
        QDateTime candidate(currentDateTime.date(), t);
        if (candidate < currentDateTime)
            candidate = candidate.addDays(1);
        if (!found || candidate < nextDateTime) {
            nextDateTime = candidate;
            found = true;
        }
    }
    if (!found) return;

    qint64 msecsUntilScheduled = currentDateTime.msecsTo(nextDateTime);
    if (msecsUntilScheduled < 0) msecsUntilScheduled = 0;

    m_dailyTimer->setSingleShot(true);
    m_dailyTimer->start(msecsUntilScheduled);

    qDebug() << "Scheduler: Next check scheduled for:" << nextDateTime.toString("yyyy-MM-dd HH:mm:ss")
             << "in" << msecsUntilScheduled / 1000.0 << "seconds.";
}

void Scheduler::checkScheduledTime() {
    bool sftpConfigOk = !m_isSftpMode || (!m_sftpHost.isEmpty() && !m_sftpUsername.isEmpty() && !m_sftpRemotePath.isEmpty());
    bool gcsConfigOk = !m_isGcsMode || !m_gcsBucketName.isEmpty(); // Object prefix can be empty
    bool localConfigOk = (!m_isSftpMode && !m_isGcsMode) ? !m_currentDestinationPath.isEmpty() : true;

    if (!m_taskEnabled || m_scheduledTimes.isEmpty() || m_currentSourcePath.isEmpty() || !sftpConfigOk || !gcsConfigOk || !localConfigOk) {
        qDebug() << "Scheduler: Check time called, but task is not active or fully configured for the current mode.";
        scheduleNextCheck();
        return;
    }

    QTime currentTime = QTime::currentTime();
    qDebug() << "Scheduler: Timer fired! Current time:" << currentTime.toString("HH:mm:ss")
             << "Times:" << m_scheduledTimes;

    emit backupTaskTriggered(m_currentSourcePath, m_currentDestinationPath); // Renamed variables
    
    scheduleNextCheck();
}

// --- Getter methods ---
QString Scheduler::sourcePath() const {
    return m_currentSourcePath; // Renamed variable
}

QString Scheduler::destinationPath() const {
    return m_currentDestinationPath; // Renamed variable (this is dest path for local, or identifier for SFTP)
}

QList<QTime> Scheduler::scheduledTimes() const {
    return m_scheduledTimes;
}

bool Scheduler::isEnabled() const {
    return m_taskEnabled; // Renamed variable
}

bool Scheduler::isSftpMode() const {
    return m_isSftpMode;
}

QString Scheduler::sftpHost() const {
    return m_sftpHost;
}

int Scheduler::sftpPort() const {
    return m_sftpPort;
}

QString Scheduler::sftpUsername() const {
    return m_sftpUsername;
}

QString Scheduler::sftpRemotePath() const {
    return m_sftpRemotePath;
}

// --- GCS Getter methods ---
bool Scheduler::isGcsMode() const {
    return m_isGcsMode;
}

QString Scheduler::gcsBucketName() const {
    return m_gcsBucketName;
}

QString Scheduler::gcsObjectPrefix() const {
    return m_gcsObjectPrefix;
}

QString Scheduler::gcsAccountIdentifier() const {
    return m_gcsObjectPrefix; // m_gcsObjectPrefix stores the GCS account identifier
}

// Example of how triggerBackupNow could be implemented if needed:
// void Scheduler::triggerBackupNow() {
//     if (taskEnabled_ && !currentSourcePath_.isEmpty() && !currentDestinationPath_.isEmpty()) {
//         qDebug() << "Scheduler: Manual backup trigger for:" << currentSourcePath_;
//         emit backupTaskTriggered(currentSourcePath_, currentDestinationPath_);
//     } else {
//         qDebug() << "Scheduler: Manual backup trigger ignored, task not enabled or not configured.";
//     }
// }
