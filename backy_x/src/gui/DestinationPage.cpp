#include "gui/DestinationPage.h"
#include "ui_DestinationPage.h"

DestinationPage::DestinationPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::DestinationPage) {
  ui->setupUi(this);
}

DestinationPage::~DestinationPage() { delete ui; }

void DestinationPage::setCurrentIndex(int idx) {
  ui->stackedWidget->setCurrentIndex(idx);
}
