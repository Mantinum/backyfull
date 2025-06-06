#include "gui/WatchManager.h"

WatchManager::WatchManager(QObject *parent)
    : QObject(parent), watcher_(new QFileSystemWatcher(this)),
      timer_(new QTimer(this)), enabled_(false) {
    timer_->setSingleShot(true);
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this,
            &WatchManager::onDirectoryChanged);
    connect(timer_, &QTimer::timeout, this, &WatchManager::onTimeout);
}

void WatchManager::addEntry(const WatchEntry &entry) {
    entries_.append(entry);
    if (enabled_) {
        watcher_->addPath(entry.source);
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
    for (const WatchEntry &e : entries_) {
        if (!watcher_->directories().contains(e.source)) {
            watcher_->addPath(e.source);
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
        timer_->start(3000);
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
