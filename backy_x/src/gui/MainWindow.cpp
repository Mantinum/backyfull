#include "gui/MainWindow.h"
#include "ui_MainWindow.h"
#include <QListWidget>
#include <QStackedWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);
    connect(ui->navList, &QListWidget::currentRowChanged,
            this, &MainWindow::onNavChanged);
    connect(ui->backupCards, &QListWidget::currentRowChanged,
            this, &MainWindow::onBackupSelected);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::onNavChanged(int row) {
    if (ui->mainStack)
        ui->mainStack->setCurrentIndex(row);
}

void MainWindow::onBackupSelected(int /*row*/) {
    // placeholder for future implementation
}
