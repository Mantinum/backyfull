#ifndef DESTINATIONSTACK_H
#define DESTINATIONSTACK_H

#include <QWidget>

QT_BEGIN_NAMESPACE
class QStackedWidget;
class QLineEdit;
class QPushButton;
QT_END_NAMESPACE

class DestinationStack : public QWidget {
    Q_OBJECT
public:
    explicit DestinationStack(QWidget *parent = nullptr);

    QLineEdit *lineEdit() const;
    QPushButton *browseButton() const;

public slots:
    void setCurrentIndex(int index);
public:
    int currentIndex() const;

private:
    QStackedWidget *stacked_;
    QLineEdit *lineEdit_;
    QPushButton *browseButton_;
};

#endif // DESTINATIONSTACK_H
