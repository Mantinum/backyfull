#include "core/BackupConfig.h"
#include <QJsonObject>
#include <QJsonArray>

BackupConfig::BackupConfig(QObject *parent) : QObject(parent) {}

void BackupConfig::setSourceDir(const QString &dir) {
    if (m_sourceDir != dir) {
        m_sourceDir = dir;
        emit sourceDirChanged();
    }
}

void BackupConfig::setDestDir(const QString &dir) {
    if (m_destDir != dir) {
        m_destDir = dir;
        emit destDirChanged();
    }
}

void BackupConfig::setMode(const QString &m) {
    if (m_mode != m) {
        m_mode = m;
        emit modeChanged();
    }
}

void BackupConfig::setScheduleList(const QStringList &list) {
    if (m_scheduleList != list) {
        m_scheduleList = list;
        emit scheduleListChanged();
    }
}

void BackupConfig::setMonitoringEnabled(bool e) {
    if (m_monitoringEnabled != e) {
        m_monitoringEnabled = e;
        emit monitoringEnabledChanged();
    }
}

void BackupConfig::setMonitoringInterval(int i) {
    if (m_monitoringInterval != i) {
        m_monitoringInterval = i;
        emit monitoringIntervalChanged();
    }
}

QJsonObject BackupConfig::toJson() const {
    QJsonObject obj;
    obj["sourceDir"] = m_sourceDir;
    obj["destDir"] = m_destDir;
    obj["mode"] = m_mode;
    obj["schedule"] = QJsonArray::fromStringList(m_scheduleList);
    obj["monitoringEnabled"] = m_monitoringEnabled;
    obj["monitoringInterval"] = m_monitoringInterval;
    return obj;
}
