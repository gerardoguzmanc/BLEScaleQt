#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QBluetoothDeviceDiscoveryAgent> // Add this for Bluetooth device discovery
#include <QBluetoothDeviceInfo>          // Add this for Bluetooth device information
#include <QListWidget>                   // To display devices
#include <QPushButton>                   // To start scan
#include <QVBoxLayout>                   // To arrange widgets

#include <QLowEnergyController> // Add this for BLE controller
#include <QLabel> // To show connection status


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

    void connectToDevice();
    void controllerStateChanged(QLowEnergyController::ControllerState state);
    void deviceConnected();
    void deviceDisconnected();
    void serviceDiscovered(const QBluetoothUuid &uuid);
    void serviceDiscoveryFinished();
    void controllerError(QLowEnergyController::Error error);

private:
    Ui::MainWindow *ui;
    QBluetoothDeviceDiscoveryAgent *discoveryAgent;
    QListWidget *deviceListWidget;
    QPushButton *scanButton;
    QPushButton *connectButton; // New connect button
    QLabel *statusLabel;        // New status label

    QLowEnergyController *leController; // BLE controller
    QBluetoothDeviceInfo m_currentDevice; // To store the selected device info
    QList<QBluetoothUuid> m_serviceUuids; // To store discovered service UUIDs
};
#endif // MAINWINDOW_H
