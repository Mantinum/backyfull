#ifndef FILEVIEWERWIDGET_H
#define FILEVIEWERWIDGET_H

#include <QWidget>
#include <QString>
#include <vector>

#include "core/IStorageTarget.h" // For FileMetadata

QT_BEGIN_NAMESPACE
class QTableWidget;
class QPushButton;
class QLabel;
class QTableWidgetItem;
QT_END_NAMESPACE

class SftpTarget;
class GcsTarget;

class FileViewerWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileViewerWidget(QWidget *parent = nullptr);

    QTableWidget *tableWidget() const { return fileTableWidget_; }

    void setSftpTarget(SftpTarget *target) { sftpTarget_ = target; }
    void setGcsTarget(GcsTarget *target) { gcsTarget_ = target; }
    QString currentPath() const { return currentRemotePath_; }

signals:
    void logMessage(const QString &msg);

public slots:
    void browsePath(const QString &path);
    void refresh();
    void downloadSelected();
    void deleteSelected();

private slots:
    void onItemDoubleClicked(QTableWidgetItem *item);

private:
    void displayFiles(const std::vector<FileMetadata> &files);

    QTableWidget *fileTableWidget_;
    QPushButton *refreshButton_;
    QPushButton *downloadButton_;
    QPushButton *deleteButton_;
    QLabel *currentPathLabel_;
    QString currentRemotePath_;
    SftpTarget *sftpTarget_;
    GcsTarget *gcsTarget_;
};

#endif // FILEVIEWERWIDGET_H
