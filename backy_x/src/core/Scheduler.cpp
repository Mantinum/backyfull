#include "core/Scheduler.h"
#include <QDebug>
#include <QCoreApplication>
#include <QDateTime>

Scheduler::Scheduler(const QString& settingsFilePath, QObject *parent)
    : QObject(parent),
      m_taskEnabled(false), // Renamed variables
      m_isSftpMode(false),  // Initialize new SFTP flag
      m_sftpPort(22),       // Default SFTP port
      m_dailyTimer(new QTimer(this)), // Renamed
      m_settings(nullptr)      // Renamed
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
                                   const QTime& scheduledTime, 
                                   bool enabled, 
                                   bool isSftpMode,
                                   const QString& sftpHost,
                                   int sftpPort,
                                   const QString& sftpUsername,
                                   const QString& sftpRemotePath) {
    m_currentSourcePath = sourcePath;
    m_currentDestinationPath = destinationPathOrIdentifier; // Store identifier or local path
    m_currentScheduledTime = scheduledTime;
    m_taskEnabled = enabled;
    m_isSftpMode = isSftpMode;

    if (m_isSftpMode) {
        m_sftpHost = sftpHost;
        m_sftpPort = sftpPort;
        m_sftpUsername = sftpUsername;
        m_sftpRemotePath = sftpRemotePath;
    } else {
        // Clear SFTP details if not in SFTP mode to avoid confusion
        m_sftpHost.clear();
        m_sftpPort = 22; // Reset to default
        m_sftpUsername.clear();
        m_sftpRemotePath.clear();
    }

    saveTask();
    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::loadTask() {
    m_settings->beginGroup(SETTINGS_GROUP);
    m_currentSourcePath = m_settings->value(KEY_SOURCE_PATH, "").toString();
    m_currentDestinationPath = m_settings->value(KEY_DEST_PATH, "").toString();
    m_currentScheduledTime = m_settings->value(KEY_SCHEDULED_TIME, QTime(23, 0)).toTime();
    m_taskEnabled = m_settings->value(KEY_TASK_ENABLED, false).toBool();
    m_isSftpMode = m_settings->value(KEY_IS_SFTP_MODE, false).toBool();

    if (m_isSftpMode) {
        m_sftpHost = m_settings->value(KEY_SFTP_HOST, "").toString();
        m_sftpPort = m_settings->value(KEY_SFTP_PORT, 22).toInt();
        m_sftpUsername = m_settings->value(KEY_SFTP_USERNAME, "").toString();
        m_sftpRemotePath = m_settings->value(KEY_SFTP_REMOTE_PATH, "").toString();
    }
    m_settings->endGroup();

    qDebug() << "Scheduler: Loaded task -"
             << "Source:" << m_currentSourcePath
             << "Dest/ID:" << m_currentDestinationPath
             << "Time:" << m_currentScheduledTime.toString("HH:mm")
             << "Enabled:" << m_taskEnabled
             << "SFTP Mode:" << m_isSftpMode;
    if (m_isSftpMode) {
        qDebug() << "SFTP Details - Host:" << m_sftpHost << "Port:" << m_sftpPort << "User:" << m_sftpUsername << "Path:" << m_sftpRemotePath;
    }

    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::saveTask() {
    m_settings->beginGroup(SETTINGS_GROUP);
    m_settings->setValue(KEY_SOURCE_PATH, m_currentSourcePath);
    m_settings->setValue(KEY_DEST_PATH, m_currentDestinationPath);
    m_settings->setValue(KEY_SCHEDULED_TIME, m_currentScheduledTime);
    m_settings->setValue(KEY_TASK_ENABLED, m_taskEnabled);
    m_settings->setValue(KEY_IS_SFTP_MODE, m_isSftpMode);

    if (m_isSftpMode) {
        m_settings->setValue(KEY_SFTP_HOST, m_sftpHost);
        m_settings->setValue(KEY_SFTP_PORT, m_sftpPort);
        m_settings->setValue(KEY_SFTP_USERNAME, m_sftpUsername);
        m_settings->setValue(KEY_SFTP_REMOTE_PATH, m_sftpRemotePath);
    } else {
        // Remove SFTP keys if not in SFTP mode to keep settings clean
        m_settings->remove(KEY_SFTP_HOST);
        m_settings->remove(KEY_SFTP_PORT);
        m_settings->remove(KEY_SFTP_USERNAME);
        m_settings->remove(KEY_SFTP_REMOTE_PATH);
    }
    m_settings->endGroup();
    m_settings->sync(); 

    qDebug() << "Scheduler: Saved task -"
             << "Source:" << m_currentSourcePath
             << "Dest/ID:" << m_currentDestinationPath
             << "Time:" << m_currentScheduledTime.toString("HH:mm")
             << "Enabled:" << m_taskEnabled
             << "SFTP Mode:" << m_isSftpMode;
     if (m_isSftpMode) {
        qDebug() << "SFTP Details - Host:" << m_sftpHost << "Port:" << m_sftpPort << "User:" << m_sftpUsername << "Path:" << m_sftpRemotePath;
    }
}

void Scheduler::scheduleNextCheck() {
    m_dailyTimer->stop(); // Renamed variable
    if (!m_taskEnabled || !m_currentScheduledTime.isValid() || m_currentSourcePath.isEmpty() || 
        (m_isSftpMode ? (m_sftpHost.isEmpty() || m_sftpUsername.isEmpty() || m_sftpRemotePath.isEmpty()) : m_currentDestinationPath.isEmpty())) {
        qDebug() << "Scheduler: Task disabled or not fully configured for the current mode, not scheduling.";
        return;
    }

    QDateTime currentDateTime = QDateTime::currentDateTime();
    QDateTime scheduledDateTime(currentDateTime.date(), m_currentScheduledTime); // Renamed variable

    if (scheduledDateTime < currentDateTime) {
        scheduledDateTime = scheduledDateTime.addDays(1);
    }

    qint64 msecsUntilScheduled = currentDateTime.msecsTo(scheduledDateTime);
    if (msecsUntilScheduled < 0) { 
        msecsUntilScheduled = 0; 
    }
    
    m_dailyTimer->setSingleShot(true); // Renamed variable
    m_dailyTimer->start(msecsUntilScheduled); // Renamed variable

    qDebug() << "Scheduler: Next check scheduled for:" << scheduledDateTime.toString("yyyy-MM-dd HH:mm:ss")
             << "in" << msecsUntilScheduled / 1000.0 << "seconds.";
}

void Scheduler::checkScheduledTime() {
    if (!m_taskEnabled || !m_currentScheduledTime.isValid() || m_currentSourcePath.isEmpty() ||
        (m_isSftpMode ? (m_sftpHost.isEmpty() || m_sftpUsername.isEmpty() || m_sftpRemotePath.isEmpty()) : m_currentDestinationPath.isEmpty())) {
        qDebug() << "Scheduler: Check time called, but task is not active or fully configured for the current mode.";
        scheduleNextCheck(); 
        return;
    }

    QTime currentTime = QTime::currentTime();
    qDebug() << "Scheduler: Timer fired! Current time:" << currentTime.toString("HH:mm:ss")
             << "Scheduled:" << m_currentScheduledTime.toString("HH:mm"); // Renamed variable

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

QTime Scheduler::scheduledTime() const {
    return m_currentScheduledTime; // Renamed variable
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

// Example of how triggerBackupNow could be implemented if needed:
// void Scheduler::triggerBackupNow() {
//     if (taskEnabled_ && !currentSourcePath_.isEmpty() && !currentDestinationPath_.isEmpty()) {
//         qDebug() << "Scheduler: Manual backup trigger for:" << currentSourcePath_;
//         emit backupTaskTriggered(currentSourcePath_, currentDestinationPath_);
//     } else {
//         qDebug() << "Scheduler: Manual backup trigger ignored, task not enabled or not configured.";
//     }
// }
