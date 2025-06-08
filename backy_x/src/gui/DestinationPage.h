#ifndef DESTINATIONPAGE_H
#define DESTINATIONPAGE_H

#include <QWidget>

namespace Ui {
class DestinationPage;
}

class DestinationPage : public QWidget {
  Q_OBJECT
public:
  explicit DestinationPage(QWidget *parent = nullptr);
  ~DestinationPage();
  void setCurrentIndex(int idx);
private:
  Ui::DestinationPage *ui;
};

#endif // DESTINATIONPAGE_H
