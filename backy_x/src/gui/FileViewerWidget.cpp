#include "gui/FileViewerWidget.h"
#include "targets/SftpTarget.h"
#include "targets/GcsTarget.h"
#include "gui/CustomTableWidgetItems.h"

#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QStyle>
#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <chrono>

FileViewerWidget::FileViewerWidget(QWidget *parent)
    : QWidget(parent), fileTableWidget_(new QTableWidget(this)),
      refreshButton_(new QPushButton(tr("Refresh"), this)),
      downloadButton_(new QPushButton(tr("Download"), this)),
      deleteButton_(new QPushButton(tr("Delete"), this)),
      currentPathLabel_(new QLabel(tr("Path: /"), this)),
      currentRemotePath_("/"), sftpTarget_(nullptr), gcsTarget_(nullptr) {

    fileTableWidget_->setColumnCount(4);
    QStringList headers = {tr("Name"), tr("Size"), tr("Date Modified"), tr("Type")};
    fileTableWidget_->setHorizontalHeaderLabels(headers);
    fileTableWidget_->setSelectionBehavior(QAbstractItemView::SelectRows);
    fileTableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fileTableWidget_->setSelectionMode(QAbstractItemView::SingleSelection);
    fileTableWidget_->verticalHeader()->setVisible(false);
    fileTableWidget_->horizontalHeader()->setStretchLastSection(true);
    fileTableWidget_->setSortingEnabled(true);

    refreshButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    downloadButton_->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    deleteButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(refreshButton_);
    buttonLayout->addWidget(downloadButton_);
    buttonLayout->addWidget(deleteButton_);
    buttonLayout->addStretch();

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(currentPathLabel_);
    mainLayout->addWidget(fileTableWidget_);
    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);

    connect(refreshButton_, &QPushButton::clicked, this, &FileViewerWidget::refresh);
    connect(downloadButton_, &QPushButton::clicked, this, &FileViewerWidget::downloadSelected);
    connect(deleteButton_, &QPushButton::clicked, this, &FileViewerWidget::deleteSelected);
    connect(fileTableWidget_, &QTableWidget::itemDoubleClicked, this, &FileViewerWidget::onItemDoubleClicked);
}

void FileViewerWidget::displayFiles(const std::vector<FileMetadata> &files) {
    fileTableWidget_->setRowCount(0);

    if (currentRemotePath_ != "/") {
        int row = fileTableWidget_->rowCount();
        fileTableWidget_->insertRow(row);

        QIcon dirIcon = QApplication::style()->standardIcon(QStyle::SP_ArrowUp);
        QTableWidgetItem *nameItem = new QTableWidgetItem(dirIcon, "..");
        nameItem->setData(Qt::UserRole, true);
        nameItem->setData(Qt::UserRole + 1, "..");
        fileTableWidget_->setItem(row, 0, nameItem);

        SizeTableWidgetItem *sizeItem = new SizeTableWidgetItem("-");
        sizeItem->setData(Qt::UserRole, QVariant::fromValue(qlonglong(-2)));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fileTableWidget_->setItem(row, 1, sizeItem);

        DateTimeTableWidgetItem *dateItem = new DateTimeTableWidgetItem("");
        dateItem->setData(Qt::UserRole, QVariant::fromValue(qlonglong(0)));
        fileTableWidget_->setItem(row, 2, dateItem);

        fileTableWidget_->setItem(row, 3, new QTableWidgetItem(tr("Parent Directory")));
    }

    for (const auto &file : files) {
        int row = fileTableWidget_->rowCount();
        fileTableWidget_->insertRow(row);

        QIcon icon = QApplication::style()->standardIcon(file.isDirectory ? QStyle::SP_DirIcon : QStyle::SP_FileIcon);
        QTableWidgetItem *nameItem = new QTableWidgetItem(icon, QString::fromStdString(file.name));
        nameItem->setData(Qt::UserRole, file.isDirectory);
        nameItem->setData(Qt::UserRole + 1, QString::fromStdString(file.name));
        fileTableWidget_->setItem(row, 0, nameItem);

        SizeTableWidgetItem *sizeItem;
        if (file.isDirectory) {
            sizeItem = new SizeTableWidgetItem("-");
            sizeItem->setData(Qt::UserRole, QVariant::fromValue(qlonglong(-1)));
        } else {
            QString formattedSize = QString::number(file.size) + " B";
            sizeItem = new SizeTableWidgetItem(formattedSize, static_cast<qlonglong>(file.size));
        }
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        fileTableWidget_->setItem(row, 1, sizeItem);

        qint64 secs = static_cast<qint64>(std::chrono::duration_cast<std::chrono::seconds>(file.modificationTime.time_since_epoch()).count());
        QDateTime modDateTime = QDateTime::fromSecsSinceEpoch(secs);
        QString formattedDate = modDateTime.toString(Qt::TextDate);
        DateTimeTableWidgetItem *dateItem = new DateTimeTableWidgetItem(formattedDate, secs);
        fileTableWidget_->setItem(row, 2, dateItem);

        fileTableWidget_->setItem(row, 3, new QTableWidgetItem(file.isDirectory ? tr("Folder") : tr("File")));
    }
}

void FileViewerWidget::browsePath(const QString &path) {
    currentRemotePath_ = path;
    currentPathLabel_->setText(tr("Path: ") + currentRemotePath_);

    emit logMessage(tr("Attempting to browse remote path: %1").arg(path));

    std::vector<FileMetadata> files;
    if (gcsTarget_) {
        files = gcsTarget_->listFiles(path.toStdString());
        if (!gcsTarget_->getLastError().empty()) {
            QMessageBox::critical(this, tr("GCS Error"),
                                  tr("Failed to list files: %1").arg(QString::fromStdString(gcsTarget_->getLastError())));
        }
    } else if (sftpTarget_ && sftpTarget_->isSessionOpen()) {
        files = sftpTarget_->listFiles(path.toStdString());
        emit logMessage(QString("SFTP listFiles returned %1 items").arg(files.size()));
        if (!sftpTarget_->getLastError().empty()) {
            QMessageBox::critical(this, tr("SFTP Error"),
                                  tr("Failed to list files: %1").arg(QString::fromStdString(sftpTarget_->getLastError())));
        }
    } else {
        emit logMessage(tr("BrowsePath called but no remote target is connected."));
    }

    displayFiles(files);
    emit logMessage(tr("Displayed %1 files/folders for path: %2").arg(files.size()).arg(path));
}

void FileViewerWidget::refresh() {
    emit logMessage("File viewer refresh clicked.");
    browsePath(currentRemotePath_);
}

void FileViewerWidget::downloadSelected() {
    QList<QTableWidgetItem *> selectedItems = fileTableWidget_->selectedItems();
    if (selectedItems.isEmpty() || selectedItems.first()->row() < 0) {
        QMessageBox::information(this, tr("Download"), tr("No file selected or invalid selection."));
        return;
    }
    QTableWidgetItem *nameItem = fileTableWidget_->item(selectedItems.first()->row(), 0);
    if (!nameItem) {
        QMessageBox::warning(this, tr("Download Error"), tr("Could not retrieve item data."));
        return;
    }
    bool isDir = nameItem->data(Qt::UserRole).toBool();
    QString actualFileName = nameItem->data(Qt::UserRole + 1).toString();
    if (isDir || actualFileName == "..") {
        QMessageBox::information(this, tr("Download"), tr("Cannot download directories or '..'."));
        return;
    }
    QString remoteFilePath = QDir(currentRemotePath_).filePath(actualFileName);
    remoteFilePath = QDir::cleanPath(remoteFilePath);
    QString localPath = QFileDialog::getSaveFileName(this, tr("Save File"), actualFileName);
    if (localPath.isEmpty())
        return;
    emit logMessage(tr("Attempting to download remote file '%1' to '%2'").arg(remoteFilePath, localPath));
    bool success = false;
    QString errorMsg;
    if (gcsTarget_) {
        if (gcsTarget_->downloadFile(remoteFilePath.toStdString(), localPath.toStdString())) {
            success = true;
        } else {
            errorMsg = QString::fromStdString(gcsTarget_->getLastError());
        }
    } else if (sftpTarget_ && sftpTarget_->isSessionOpen()) {
        if (sftpTarget_->downloadFile(remoteFilePath.toStdString(), localPath.toStdString())) {
            success = true;
        } else {
            errorMsg = QString::fromStdString(sftpTarget_->getLastError());
        }
    } else {
        errorMsg = tr("No remote target connected.");
    }
    if (success) {
        QMessageBox::information(this, tr("Download Complete"),
                                 tr("File '%1' downloaded successfully to '%2'.").arg(actualFileName, localPath));
        emit logMessage(tr("Successfully downloaded '%1' to '%2'.").arg(remoteFilePath, localPath));
    } else {
        QMessageBox::critical(this, tr("Download Failed"),
                              tr("Failed to download '%1'. Error: %2").arg(actualFileName, errorMsg));
        emit logMessage(tr("Failed to download '%1'. Error: %2").arg(remoteFilePath, errorMsg));
    }
}

void FileViewerWidget::deleteSelected() {
    QList<QTableWidgetItem *> selectedItems = fileTableWidget_->selectedItems();
    if (selectedItems.isEmpty() || selectedItems.first()->row() < 0) {
        QMessageBox::information(this, tr("Delete"), tr("No file selected or invalid selection."));
        return;
    }
    int selectedRow = selectedItems.first()->row();
    QTableWidgetItem *nameItemWidget = fileTableWidget_->item(selectedRow, 0);
    if (!nameItemWidget) {
        QMessageBox::warning(this, tr("Delete Error"), tr("Could not retrieve item data for selected row %1.").arg(selectedRow));
        return;
    }
    QString actualFileName = nameItemWidget->data(Qt::UserRole + 1).toString();
    bool isDirectory = nameItemWidget->data(Qt::UserRole).toBool();
    if (actualFileName.isEmpty() || actualFileName == "..") {
        QMessageBox::warning(this, tr("Delete Error"), tr("Invalid selection."));
        return;
    }
    QString fullRemotePath = QDir(currentRemotePath_).filePath(actualFileName);
    fullRemotePath = QDir::cleanPath(fullRemotePath);

    QMessageBox::StandardButton reply = QMessageBox::warning(this, tr("Confirm Delete"),
                               tr("Are you sure you want to delete '%1'%2?")
                                   .arg(actualFileName, isDirectory ? tr(" (Directory)") : ""),
                               QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::No)
        return;

    bool success = false;
    QString errorMsg;
    if (gcsTarget_) {
        if (gcsTarget_->deleteFile(fullRemotePath.toStdString())) {
            success = true;
        } else {
            errorMsg = QString::fromStdString(gcsTarget_->getLastError());
        }
    } else if (sftpTarget_ && sftpTarget_->isSessionOpen()) {
        if (sftpTarget_->deleteFile(fullRemotePath.toStdString())) {
            success = true;
        } else {
            errorMsg = tr("SFTP delete failed. Check logs.");
        }
    } else {
        errorMsg = tr("No remote target connected.");
    }

    if (success) {
        QMessageBox::information(this, tr("Delete Successful"),
                                 tr("'%1' deleted successfully.").arg(actualFileName));
        emit logMessage(tr("Successfully deleted '%1'.").arg(fullRemotePath));
        if (selectedRow >= 0 && selectedRow < fileTableWidget_->rowCount()) {
            fileTableWidget_->removeRow(selectedRow);
        } else {
            refresh();
        }
    } else {
        QMessageBox::critical(this, tr("Delete Failed"),
                              tr("Failed to delete '%1'. Error: %2").arg(actualFileName, errorMsg));
        emit logMessage(tr("Failed to delete '%1'. Error: %2").arg(fullRemotePath, errorMsg));
    }
}

void FileViewerWidget::onItemDoubleClicked(QTableWidgetItem *item) {
    if (!item)
        return;
    QTableWidgetItem *nameColItem = fileTableWidget_->item(item->row(), 0);
    if (!nameColItem)
        return;
    bool isDir = nameColItem->data(Qt::UserRole).toBool();
    QString itemName = nameColItem->data(Qt::UserRole + 1).toString();
    if (isDir) {
        if (itemName == "..") {
            QDir dir(currentRemotePath_);
            if (dir.cdUp()) {
                QString parentPath = dir.path();
                if (parentPath.isEmpty() || parentPath == "." || parentPath == "//")
                    parentPath = "/";
                if (parentPath.endsWith("/."))
                    parentPath = parentPath.left(parentPath.length() - 2);
                browsePath(parentPath);
            } else {
                browsePath("/");
            }
        } else {
            QString newPath = currentRemotePath_ == "/" ? "/" + itemName : currentRemotePath_ + "/" + itemName;
            newPath = QDir::cleanPath(newPath);
            browsePath(newPath);
        }
    } else {
        downloadSelected();
    }
}
