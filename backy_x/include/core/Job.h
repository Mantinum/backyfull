#ifndef JOB_H
#define JOB_H

#include <QAbstractTableModel>
#include <QBitArray>
#include <QDateTime>
#include <QList>

enum class JobType { Scheduled, Watch };

struct Job {
    JobType type{JobType::Scheduled};
    QBitArray daysMask;   // Monday=0 .. Sunday=6
    QTime time;
    int interval{0};      // seconds for watch
    QDateTime next;
    bool enabled{true};
};

class JobsModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit JobsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void addJob(const Job &job);
    void removeRows(const QList<int> &rows);

    Job &jobRef(int row) { return jobs_[row]; }
    const QList<Job> &jobs() const { return jobs_; }
    void setJobNext(int row, const QDateTime &dt);

private:
    QList<Job> jobs_;
};

#endif // JOB_H
