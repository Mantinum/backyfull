#include "core/Scheduler.h"
#include <QDebug> // For logging/debug output
#include <QCoreApplication> // For QSettings organization/app name if not already set
#include <QDateTime> // Required for scheduleNextCheck logic

Scheduler::Scheduler(const QString& settingsFilePath, QObject *parent)
    : QObject(parent),
      taskEnabled_(false),
      dailyTimer_(new QTimer(this)),
      settings_(nullptr)
{
    if (settingsFilePath.isEmpty()) {
        // Default behavior: use standard app/org settings
        if (QCoreApplication::organizationName().isEmpty()) {
            QCoreApplication::setOrganizationName("BackyFullOrgTestDefault"); // Default for safety
        }
        if (QCoreApplication::applicationName().isEmpty()) {
            QCoreApplication::setApplicationName("BackyFullTestDefault");    // Default for safety
        }
        settings_ = new QSettings(QSettings::IniFormat, QSettings::UserScope,
                                  QCoreApplication::organizationName(),
                                  QCoreApplication::applicationName(), this);
    } else {
        // Use provided settings file path (typically for testing)
        settings_ = new QSettings(settingsFilePath, QSettings::IniFormat, this);
    }
    
    connect(dailyTimer_, &QTimer::timeout, this, &Scheduler::checkScheduledTime);
    loadTask(); // Load task settings on startup using the initialized settings_
}

Scheduler::~Scheduler() {
    // dailyTimer_ is a child of Scheduler, QObject parent-child cleanup handles it.
    // settings_ is also a child.
}

void Scheduler::setDailyBackupTask(const QString& sourcePath, const QString& destinationPath, const QTime& scheduledTime, bool enabled) {
    currentSourcePath_ = sourcePath;
    currentDestinationPath_ = destinationPath;
    currentScheduledTime_ = scheduledTime;
    taskEnabled_ = enabled;

    saveTask();
    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::loadTask() {
    settings_->beginGroup(SETTINGS_GROUP);
    currentSourcePath_ = settings_->value(KEY_SOURCE_PATH, "").toString();
    currentDestinationPath_ = settings_->value(KEY_DEST_PATH, "").toString();
    currentScheduledTime_ = settings_->value(KEY_SCHEDULED_TIME, QTime(23, 0)).toTime(); // Default to 11 PM
    taskEnabled_ = settings_->value(KEY_TASK_ENABLED, false).toBool();
    settings_->endGroup();

    qDebug() << "Scheduler: Loaded task -"
             << "Source:" << currentSourcePath_
             << "Dest:" << currentDestinationPath_
             << "Time:" << currentScheduledTime_.toString("HH:mm")
             << "Enabled:" << taskEnabled_;

    scheduleNextCheck();
    emit taskChanged();
}

void Scheduler::saveTask() {
    settings_->beginGroup(SETTINGS_GROUP);
    settings_->setValue(KEY_SOURCE_PATH, currentSourcePath_);
    settings_->setValue(KEY_DEST_PATH, currentDestinationPath_);
    settings_->setValue(KEY_SCHEDULED_TIME, currentScheduledTime_);
    settings_->setValue(KEY_TASK_ENABLED, taskEnabled_);
    settings_->endGroup();
    settings_->sync(); // Ensure data is written

    qDebug() << "Scheduler: Saved task -"
             << "Source:" << currentSourcePath_
             << "Dest:" << currentDestinationPath_
             << "Time:" << currentScheduledTime_.toString("HH:mm")
             << "Enabled:" << taskEnabled_;
}

void Scheduler::scheduleNextCheck() {
    dailyTimer_->stop();
    if (!taskEnabled_ || !currentScheduledTime_.isValid() || currentSourcePath_.isEmpty() || currentDestinationPath_.isEmpty()) {
        qDebug() << "Scheduler: Task disabled or invalid, not scheduling.";
        return;
    }

    QDateTime currentDateTime = QDateTime::currentDateTime();
    QDateTime scheduledDateTime(currentDateTime.date(), currentScheduledTime_);

    if (scheduledDateTime < currentDateTime) {
        // If scheduled time for today has already passed, schedule for tomorrow
        scheduledDateTime = scheduledDateTime.addDays(1);
    }

    qint64 msecsUntilScheduled = currentDateTime.msecsTo(scheduledDateTime);
    if (msecsUntilScheduled < 0) { // Should not happen if logic above is correct
        msecsUntilScheduled = 0; 
    }
    
    // For testing, one might use a shorter interval. For production, this is fine.
    // The timer will fire once at the scheduled time.
    // Then checkScheduledTime will re-arm it for the next day.
    dailyTimer_->setSingleShot(true); // Ensures it fires once, then we reschedule
    dailyTimer_->start(msecsUntilScheduled);

    qDebug() << "Scheduler: Next check scheduled for:" << scheduledDateTime.toString("yyyy-MM-dd HH:mm:ss")
             << "in" << msecsUntilScheduled / 1000.0 << "seconds.";
}

void Scheduler::checkScheduledTime() {
    if (!taskEnabled_ || !currentScheduledTime_.isValid() || currentSourcePath_.isEmpty() || currentDestinationPath_.isEmpty()) {
        qDebug() << "Scheduler: Check time called, but task is not active or fully configured.";
        scheduleNextCheck(); // Reschedule even if not active, in case it becomes active later
        return;
    }

    QTime currentTime = QTime::currentTime();
    // Check if current time is "close enough" to scheduled time.
    // QTimer might not be perfectly exact to the millisecond.
    // A common approach is to check if current time is >= scheduled time and < scheduled time + a small delta (e.g., 1 minute)
    // Or, since this timer is a single-shot specifically set for the scheduled time,
    // we can assume if it fires, it's time.

    qDebug() << "Scheduler: Timer fired! Current time:" << currentTime.toString("HH:mm:ss")
             << "Scheduled:" << currentScheduledTime_.toString("HH:mm");

    // Emit signal to trigger the backup
    emit backupTaskTriggered(currentSourcePath_, currentDestinationPath_);
    
    // Reschedule for the next day
    scheduleNextCheck();
}

// --- Getter methods ---
QString Scheduler::sourcePath() const {
    return currentSourcePath_;
}

QString Scheduler::destinationPath() const {
    return currentDestinationPath_;
}

QTime Scheduler::scheduledTime() const {
    return currentScheduledTime_;
}

bool Scheduler::isEnabled() const {
    return taskEnabled_;
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
