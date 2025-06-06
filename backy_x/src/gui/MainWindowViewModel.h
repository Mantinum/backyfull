#ifndef MAINWINDOWVIEWMODEL_H
#define MAINWINDOWVIEWMODEL_H

#include <QObject>
#include "core/BackupConfig.h"

class MainWindowViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceDir READ sourceDir WRITE setSourceDir NOTIFY sourceDirChanged)
    Q_PROPERTY(QString destDir READ destDir WRITE setDestDir NOTIFY destDirChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)

public:
    explicit MainWindowViewModel(QObject *parent = nullptr);

    BackupConfig *config() { return &m_cfg; }

    QString sourceDir() const { return m_cfg.sourceDir(); }
    void setSourceDir(const QString &d) { m_cfg.setSourceDir(d); }

    QString destDir() const { return m_cfg.destDir(); }
    void setDestDir(const QString &d) { m_cfg.setDestDir(d); }

    QString mode() const { return m_cfg.mode(); }
    void setMode(const QString &m) { m_cfg.setMode(m); }

public slots:
    void browseSource();
    void browseDestination();
    void testSftp();
    void testGcs();

signals:
    void sourceDirChanged();
    void destDirChanged();
    void modeChanged();

private:
    BackupConfig m_cfg;
};

#endif // MAINWINDOWVIEWMODEL_H
