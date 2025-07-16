#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothDeviceInfo>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QLowEnergyCharacteristic>
#include <QLabel>
#include <QMap>

QT_BEGIN_NAMESPACE

// --- Add this operator overload ---
inline bool operator<(const QLowEnergyCharacteristic &lhs, const QLowEnergyCharacteristic &rhs)
{
    // QLowEnergyCharacteristic does not have a public unique ID or comparison
    // directly. The most reliable way to compare them for map keys is by their UUID
    // and potentially the service handle if UUIDs can be repeated across services
    // in complex scenarios, but usually UUID is sufficient if chars are unique per service.
    // However, the truly unique identifier for a characteristic within a service is its handle.
    // QLowEnergyCharacteristic in Qt6 doesn't expose its handle directly for comparison.
    // The UUID is the most accessible and reliable property for uniqueness.
    // If UUIDs within a service are not guaranteed unique by the peripheral,
    // this could cause issues. For most typical BLE devices, UUIDs for characteristics
    // within a given service are unique.

    // Update: As of Qt 6, QLowEnergyCharacteristic provides operator== and operator!=
    // by default based on internal identity (which includes the handle).
    // However, for std::map (which QMap uses internally), a strict weak ordering is needed.
    // Comparing by UUID is the common approach.
    return lhs.uuid() < rhs.uuid();
}
// ---------------------------------

namespace Ui { class MainWindow; }
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

    // New slots for service and characteristic interaction
    void onServiceSelected(); // Slot for when a service is selected in the list
    void serviceDetailsDiscovered(QLowEnergyService::ServiceState newState);
    void characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue);
    void characteristicRead(const QLowEnergyCharacteristic &characteristic, const QByteArray &value);
    void descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue);
    void serviceError(QLowEnergyService::ServiceError error); // Service-specific errors

private:
    Ui::MainWindow *ui; // This should be `nullptr` if not using .ui file
    QBluetoothDeviceDiscoveryAgent *discoveryAgent;
    QListWidget *deviceListWidget; // Will show devices initially, then services
    QListWidget *characteristicListWidget; // New: To show characteristics and values
    QPushButton *scanButton;
    QPushButton *connectButton;
    QPushButton *readCharButton; // New: Button to manually read selected characteristic
    QLabel *statusLabel;
    QComboBox *deviceComboBox;

    QLowEnergyController *leController;
    QBluetoothDeviceInfo m_currentDevice;
    QList<QBluetoothUuid> m_serviceUuids; // Stores discovered service UUIDs

    // Maps to manage discovered services and characteristics
    QMap<QBluetoothUuid, QLowEnergyService*> m_services; // Key: Service UUID, Value: Service object
    QMap<QLowEnergyCharacteristic, QListWidgetItem*> m_characteristicItems; // Key: Characteristic, Value: List item for quick update
    QLowEnergyService *m_currentService; // The currently selected service
};
#endif // MAINWINDOW_H
