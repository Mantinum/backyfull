#include "gui/WatchManager.h"

WatchManager::WatchManager(QObject *parent)
    : QObject(parent), watcher_(new QFileSystemWatcher(this)),
      timer_(new QTimer(this)), enabled_(false), error_(false) {
    timer_->setSingleShot(true);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            &WatchManager::onDirectoryChanged);
    connect(timer_, &QTimer::timeout, this, &WatchManager::onTimeout);
}

void WatchManager::setInterval(int seconds) {
    intervalMs_ = qMax(1, seconds) * 1000;
}

void WatchManager::addEntry(const WatchEntry &entry) {
    entries_.append(entry);
    if (enabled_) {
        if (!watcher_->addPath(entry.source))
            error_ = true;
    }
}

void WatchManager::removeEntry(const QString &path) {
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].source == path) {
            if (enabled_) {
                watcher_->removePath(path);
            }
            entries_.removeAt(i);
            break;
        }
    }
}

void WatchManager::clear() {
    if (enabled_) {
        watcher_->removePaths(watcher_->directories());
    }
    entries_.clear();
    pending_.clear();
}

void WatchManager::enable() {
    if (enabled_)
        return;
    enabled_ = true;
    error_ = false;
    for (const WatchEntry &e : entries_) {
        if (!watcher_->directories().contains(e.source)) {
            if (!watcher_->addPath(e.source))
                error_ = true;
        }
    }
}

void WatchManager::disable() {
    if (!enabled_)
        return;
    watcher_->removePaths(watcher_->directories());
    pending_.clear();
    enabled_ = false;
}

void WatchManager::onDirectoryChanged(const QString &path) {
    pending_.insert(path);
    if (!timer_->isActive())
        timer_->start(intervalMs_);
}

void WatchManager::onTimeout() {
    for (const QString &p : std::as_const(pending_)) {
        for (const WatchEntry &e : std::as_const(entries_)) {
            if (e.source == p) {
                emit triggered(e);
                break;
            }
        }
    }
    pending_.clear();
}
