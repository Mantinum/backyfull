#ifndef WATCHMANAGER_H
#define WATCHMANAGER_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QList>
#include <QString>

struct WatchEntry {
    QString source;
    QString destination;
    bool isSftpMode = false;
    bool isGcsMode = false;
    QString sftpHost;
    int sftpPort = 22;
    QString sftpUsername;
    QString sftpRemotePath;
    QString gcsBucketName;
    QString gcsAccountId;
};

class WatchManager : public QObject
{
    Q_OBJECT
public:
    explicit WatchManager(QObject *parent = nullptr);

    void addEntry(const WatchEntry &entry);
    void removeEntry(const QString &path);
    const QList<WatchEntry> &entries() const { return entries_; }
    void clear();
    void enable();
    void disable();
    bool isEnabled() const { return enabled_; }

signals:
    void triggered(const WatchEntry &entry);

private slots:
    void onDirectoryChanged(const QString &path);
    void onTimeout();

private:
    QFileSystemWatcher *watcher_;
    QTimer *timer_;
    QList<WatchEntry> entries_;
    QSet<QString> pending_;
    bool enabled_;
};

#endif // WATCHMANAGER_H
