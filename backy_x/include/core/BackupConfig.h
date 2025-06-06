#ifndef BACKUPCONFIG_H
#define BACKUPCONFIG_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>

class BackupConfig : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceDir READ sourceDir WRITE setSourceDir NOTIFY sourceDirChanged)
    Q_PROPERTY(QString destDir READ destDir WRITE setDestDir NOTIFY destDirChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QStringList scheduleList READ scheduleList WRITE setScheduleList NOTIFY scheduleListChanged)
    Q_PROPERTY(bool monitoringEnabled READ monitoringEnabled WRITE setMonitoringEnabled NOTIFY monitoringEnabledChanged)
    Q_PROPERTY(int monitoringInterval READ monitoringInterval WRITE setMonitoringInterval NOTIFY monitoringIntervalChanged)

public:
    explicit BackupConfig(QObject *parent = nullptr);

    QString sourceDir() const { return m_sourceDir; }
    void setSourceDir(const QString &dir);

    QString destDir() const { return m_destDir; }
    void setDestDir(const QString &dir);

    QString mode() const { return m_mode; }
    void setMode(const QString &m);

    QStringList scheduleList() const { return m_scheduleList; }
    void setScheduleList(const QStringList &list);

    bool monitoringEnabled() const { return m_monitoringEnabled; }
    void setMonitoringEnabled(bool e);

    int monitoringInterval() const { return m_monitoringInterval; }
    void setMonitoringInterval(int i);

    QJsonObject toJson() const;

signals:
    void sourceDirChanged();
    void destDirChanged();
    void modeChanged();
    void scheduleListChanged();
    void monitoringEnabledChanged();
    void monitoringIntervalChanged();

private:
    QString m_sourceDir;
    QString m_destDir;
    QString m_mode;
    QStringList m_scheduleList;
    bool m_monitoringEnabled{false};
    int m_monitoringInterval{0};
};

#endif // BACKUPCONFIG_H
