#include "gui/MainWindowViewModel.h"
#include <QFileDialog>

MainWindowViewModel::MainWindowViewModel(QObject *parent) : QObject(parent) {}

void MainWindowViewModel::browseSource() {
    QFileDialog dlg;
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    QString dir = dlg.getExistingDirectory(nullptr, tr("Select Source"), m_cfg.sourceDir());
    if (!dir.isEmpty())
        setSourceDir(dir);
}

void MainWindowViewModel::browseDestination() {
    QFileDialog dlg;
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    QString dir = dlg.getExistingDirectory(nullptr, tr("Select Destination"), m_cfg.destDir());
    if (!dir.isEmpty())
        setDestDir(dir);
}

void MainWindowViewModel::testSftp() {
    // Placeholder for SFTP connection test
    emit destDirChanged();
}

void MainWindowViewModel::testGcs() {
    // Placeholder for GCS connection test
    emit destDirChanged();
}
