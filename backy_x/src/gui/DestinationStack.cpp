#include "DestinationStack.h"
#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

DestinationStack::DestinationStack(QWidget *parent)
    : QWidget(parent), stacked_(new QStackedWidget(this)),
      lineEdit_(new QLineEdit(this)),
      browseButton_(new QPushButton(tr("Browse..."), this)) {
    QWidget *localPage = new QWidget();
    QHBoxLayout *localLayout = new QHBoxLayout(localPage);
    localLayout->setContentsMargins(0, 0, 0, 0);
    localLayout->addWidget(lineEdit_);
    localLayout->addWidget(browseButton_);
    stacked_->addWidget(localPage);

    QLabel *soon1 = new QLabel(tr("Soon..."));
    soon1->setAlignment(Qt::AlignCenter);
    stacked_->addWidget(soon1);

    QLabel *soon2 = new QLabel(tr("Soon..."));
    soon2->setAlignment(Qt::AlignCenter);
    stacked_->addWidget(soon2);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(stacked_);
}

QLineEdit *DestinationStack::lineEdit() const { return lineEdit_; }
QPushButton *DestinationStack::browseButton() const { return browseButton_; }
void DestinationStack::setCurrentIndex(int index) { stacked_->setCurrentIndex(index); }
int DestinationStack::currentIndex() const { return stacked_->currentIndex(); }
