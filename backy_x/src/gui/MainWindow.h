#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QListWidget;
class QStackedWidget;
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private slots:
    void onNavChanged(int row);
    void onBackupSelected(int row);
private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
