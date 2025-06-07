#include "core/Job.h"

JobsModel::JobsModel(QObject *parent) : QAbstractTableModel(parent) {}

int JobsModel::rowCount(const QModelIndex &) const { return jobs_.size(); }
int JobsModel::columnCount(const QModelIndex &) const { return 4; }

QVariant JobsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= jobs_.size())
        return {};
    const Job &j = jobs_[index.row()];
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case 0:
            return j.type == JobType::Scheduled ? tr("Scheduled") : tr("Watch");
        case 1: {
            if (j.type == JobType::Scheduled) {
                QStringList days;
                for (int d = 0; d < 7; ++d)
                    if (j.daysMask.testBit(d))
                        days << QLocale().dayName(d + 1, QLocale::ShortFormat);
                return j.time.toString("HH:mm") +
                       (days.isEmpty() ? QString(" (All)")
                                       : " (" + days.join(',') + ")");
            }
            return QString();
        }
        case 2:
            return j.type == JobType::Watch ? j.interval : QVariant();
        case 3:
            return j.next.isValid() ? j.next.toString("yyyy-MM-dd HH:mm")
                                    : QString();
        }
    }
    return {};
}

QVariant JobsModel::headerData(int section, Qt::Orientation orient, int role) const {
    if (orient == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case 0:
            return tr("Type");
        case 1:
            return tr("When");
        case 2:
            return tr("Interval");
        case 3:
            return tr("Next");
        }
    }
    return QAbstractTableModel::headerData(section, orient, role);
}

Qt::ItemFlags JobsModel::flags(const QModelIndex &index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void JobsModel::addJob(const Job &job) {
    beginInsertRows(QModelIndex(), jobs_.size(), jobs_.size());
    jobs_.append(job);
    endInsertRows();
}

void JobsModel::removeRows(const QList<int> &rows) {
    QList<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int r : sorted) {
        if (r < 0 || r >= jobs_.size())
            continue;
        beginRemoveRows(QModelIndex(), r, r);
        jobs_.removeAt(r);
        endRemoveRows();
    }
}

void JobsModel::setJobNext(int row, const QDateTime &dt) {
    if (row < 0 || row >= jobs_.size())
        return;
    jobs_[row].next = dt;
    emit dataChanged(index(row, 3), index(row, 3));
}
