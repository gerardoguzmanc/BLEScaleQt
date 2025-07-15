#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QBluetoothDeviceDiscoveryAgent> // Add this for Bluetooth device discovery
#include <QBluetoothDeviceInfo>          // Add this for Bluetooth device information
#include <QListWidget>                   // To display devices
#include <QPushButton>                   // To start scan
#include <QVBoxLayout>                   // To arrange widgets

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void startScan();
    void deviceDiscovered(const QBluetoothDeviceInfo &device);
    void scanFinished();
    void scanError(QBluetoothDeviceDiscoveryAgent::Error error);

private:
    Ui::MainWindow *ui;
    QBluetoothDeviceDiscoveryAgent *discoveryAgent;
    QListWidget *deviceListWidget;
    QPushButton *scanButton;
};
#endif // MAINWINDOW_H
