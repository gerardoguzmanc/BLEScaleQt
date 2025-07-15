#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QBluetoothPermission>
#include <QLowEnergyService> // Needed for interacting with services

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , leController(nullptr) // Initialize BLE controller to nullptr
{
    ui->setupUi(this);

    // --- UI Setup ---
    deviceListWidget = new QListWidget(this);
    scanButton = new QPushButton("Start Bluetooth Scan", this);
    connectButton = new QPushButton("Connect to Selected Device", this); // New button
    connectButton->setEnabled(false); // Disable until a device is selected
    statusLabel = new QLabel("Status: Idle", this); // New status label

    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->addWidget(scanButton);
    layout->addWidget(connectButton); // Add connect button
    layout->addWidget(deviceListWidget);
    layout->addWidget(statusLabel); // Add status label
    centralWidget->setLayout(layout);
    setCentralWidget(centralWidget);

    // --- Bluetooth Classic Scan Setup (from previous example) ---
    discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    // Important: For BLE devices, ensure you are allowing Low Energy Discovery
    // discoveryAgent->setLowEnergyDiscoveryTimeout(5000); // Set a timeout for BLE discovery if needed
    // For combined discovery: discoveryAgent->setDiscoveryMethod(QBluetoothDeviceDiscoveryAgent::DeviceDiscoveryMethod::CombinedMethod);

    connect(scanButton, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &MainWindow::deviceDiscovered);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &MainWindow::scanFinished);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &MainWindow::scanError);

    // --- New: Connect Button Logic ---
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToDevice);
    connect(deviceListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        // Enable connect button if an item is selected and it's not a status message
        connectButton->setEnabled(deviceListWidget->selectedItems().count() > 0 &&
                                  !deviceListWidget->currentItem()->text().contains("No") &&
                                  !deviceListWidget->currentItem()->text().contains("found"));
    });

    // --- Android Permissions (Crucial for Android 6.0+ and Android 12+) ---
    // This remains the same as in the scanning example.
#if QT_CONFIG(permissions) && defined(Q_OS_ANDROID)
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access);

    switch (qApp->checkPermission(bluetoothPermission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(bluetoothPermission, [this, bluetoothPermission]() {
            if (qApp->checkPermission(bluetoothPermission) == Qt::PermissionStatus::Granted) {
                qDebug() << "Bluetooth permission granted after request.";
            } else {
                qWarning() << "Bluetooth permission denied.";
                QMessageBox::warning(this, "Permission Denied", "Bluetooth access is required for this app.");
            }
        });
        break;
    case Qt::PermissionStatus::Denied:
        qWarning() << "Bluetooth permission denied.";
        QMessageBox::warning(this, "Permission Denied", "Bluetooth access is required for this app. Please grant it in system settings.");
        break;
    case Qt::PermissionStatus::Granted:
        qDebug() << "Bluetooth permission already granted.";
        break;
    }
#endif
}

MainWindow::~MainWindow()
{
    // If a controller exists, disconnect it gracefully
    if (leController) {
        leController->disconnectFromDevice();
        delete leController; // Delete controller when main window closes
    }
    delete ui;
}

// --- Bluetooth Scan Slots (from previous example) ---
void MainWindow::startScan()
{
    deviceListWidget->clear();
    statusLabel->setText("Status: Scanning...");
    qDebug() << "Starting Bluetooth device scan...";
    scanButton->setEnabled(false);
    connectButton->setEnabled(false); // Disable connect button during scan
    discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::DiscoveryMethod::LowEnergyMethod); // Specifically for BLE
    // Or, for both Classic and BLE: discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::CombinedMethod);
}

void MainWindow::deviceDiscovered(const QBluetoothDeviceInfo &device)
{
    // Filter for BLE devices specifically, or just add all
    // if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
    QString itemText = device.name().isEmpty() ? "(Unknown BLE Device)" : device.name();
    itemText += " (" + device.address().toString() + ")";
    deviceListWidget->addItem(itemText);
    qDebug() << "Discovered BLE device:" << device.name() << device.address().toString();
    // }
}

void MainWindow::scanFinished()
{
    qDebug() << "Bluetooth scan finished.";
    statusLabel->setText("Status: Scan Finished.");
    scanButton->setEnabled(true);
    if (deviceListWidget->count() == 0) {
        deviceListWidget->addItem("No Bluetooth devices found.");
        connectButton->setEnabled(false);
    } else {
        // If devices found, allow selection for connection
        connectButton->setEnabled(true);
    }
}

void MainWindow::scanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    qWarning() << "Bluetooth scan error:" << error;
    statusLabel->setText("Status: Scan Error!");
    scanButton->setEnabled(true);
    connectButton->setEnabled(false);
    QString errorString;
    switch (error) {
    case QBluetoothDeviceDiscoveryAgent::InputOutputError:
        errorString = "I/O Error (check permissions/hardware).";
        break;
    case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
        errorString = "Bluetooth is powered off.";
        break;
    case QBluetoothDeviceDiscoveryAgent::UnsupportedPlatformError:
        errorString = "Unsupported method.";
        break;
    case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
        errorString = "Missing Bluetooth permissions.";
        break;
    default:
        errorString = "Unknown error.";
        break;
    }
    QMessageBox::critical(this, "Bluetooth Error", errorString);
}

// --- New: BLE Connection Slots ---

void MainWindow::connectToDevice()
{
    if (deviceListWidget->selectedItems().isEmpty()) {
        QMessageBox::warning(this, "No Device Selected", "Please select a device from the list to connect.");
        return;
    }

    QString selectedText = deviceListWidget->currentItem()->text();
    QBluetoothAddress deviceAddress(selectedText.section('(', -1).remove(')'));

    // Find the QBluetoothDeviceInfo for the selected address
    QList<QBluetoothDeviceInfo> discoveredDevices = discoveryAgent->discoveredDevices();
    m_currentDevice = QBluetoothDeviceInfo(); // Reset
    for (const QBluetoothDeviceInfo &device : discoveredDevices) {
        if (device.address() == deviceAddress) {
            m_currentDevice = device;
            break;
        }
    }

    if (!m_currentDevice.isValid()) {
        QMessageBox::critical(this, "Error", "Could not find selected device information.");
        return;
    }

    // Clean up previous controller if any
    if (leController) {
        leController->disconnectFromDevice();
        leController->deleteLater(); // Schedule for deletion
    }

    // Create a new BLE controller for the selected device
    leController = QLowEnergyController::createCentral(m_currentDevice, this);
    if (!leController) {
        QMessageBox::critical(this, "Error", "Failed to create BLE controller.");
        statusLabel->setText("Status: Controller creation failed.");
        return;
    }

    // Connect controller signals
    connect(leController, &QLowEnergyController::stateChanged,
            this, &MainWindow::controllerStateChanged);
    connect(leController, &QLowEnergyController::connected,
            this, &MainWindow::deviceConnected);
    connect(leController, &QLowEnergyController::disconnected,
            this, &MainWindow::deviceDisconnected);
    connect(leController, QOverload<QLowEnergyController::Error>::of(&QLowEnergyController::errorOccurred),
            this, &MainWindow::controllerError);
    connect(leController, &QLowEnergyController::serviceDiscovered,
            this, &MainWindow::serviceDiscovered);
    connect(leController, &QLowEnergyController::discoveryFinished,
            this, &MainWindow::serviceDiscoveryFinished);

    statusLabel->setText(QString("Status: Connecting to %1...").arg(m_currentDevice.name()));
    qDebug() << "Attempting to connect to BLE device:" << m_currentDevice.name() << m_currentDevice.address().toString();
    leController->connectToDevice();
    connectButton->setEnabled(false); // Disable connect button during connection attempt
    scanButton->setEnabled(false);    // Disable scan button too
}

void MainWindow::controllerStateChanged(QLowEnergyController::ControllerState state)
{
    qDebug() << "BLE Controller State Changed:" << state;
    switch (state) {
    case QLowEnergyController::UnconnectedState:
        statusLabel->setText("Status: Unconnected.");
        break;
    case QLowEnergyController::ConnectingState:
        statusLabel->setText("Status: Connecting...");
        break;
    case QLowEnergyController::ConnectedState:
        statusLabel->setText("Status: Connected, Discovering Services...");
        break;
    case QLowEnergyController::DiscoveringState:
        statusLabel->setText("Status: Discovering Services...");
        break;
    case QLowEnergyController::DiscoveredState:
        statusLabel->setText("Status: Services Discovered.");
        break;
    case QLowEnergyController::ClosingState:
        statusLabel->setText("Status: Disconnecting...");
        break;
    default:
        statusLabel->setText("Status: Unknown state.");
        break;
    }
}

void MainWindow::deviceConnected()
{
    qDebug() << "Connected to BLE device.";
    statusLabel->setText("Status: Connected! Discovering services...");
    leController->discoverServices(); // Start discovering services
}

void MainWindow::deviceDisconnected()
{
    qDebug() << "Disconnected from BLE device.";
    statusLabel->setText("Status: Disconnected.");
    connectButton->setEnabled(true); // Re-enable connect for another attempt
    scanButton->setEnabled(true);    // Re-enable scan
    // Clean up controller
    if (leController) {
        leController->deleteLater();
        leController = nullptr;
    }
    m_serviceUuids.clear(); // Clear discovered services
}

void MainWindow::serviceDiscovered(const QBluetoothUuid &uuid)
{
    qDebug() << "Service Discovered:" << uuid.toString();
    m_serviceUuids.append(uuid); // Store the discovered UUID
}

void MainWindow::serviceDiscoveryFinished()
{
    qDebug() << "Service discovery finished.";
    statusLabel->setText("Status: Services Discovered. Found " + QString::number(m_serviceUuids.count()) + " services.");

    if (m_serviceUuids.isEmpty()) {
        qWarning() << "No services found on this device.";
        return;
    }

    // Example: Iterate through discovered services (for debugging/inspection)
    for (const QBluetoothUuid &uuid : qAsConst(m_serviceUuids)) {
        QLowEnergyService *service = leController->createServiceObject(uuid);
        if (service) {
            qDebug() << "Service UUID:" << service->serviceUuid().toString()
            << "Type:" << service->type();
            // You can now connect to service signals and discover characteristics
            // For example:
            // connect(service, &QLowEnergyService::stateChanged, this, [service, this](QLowEnergyService::ServiceState s){
            //     qDebug() << "Service state changed for" << service->serviceUuid().toString() << ":" << s;
            // });
            // connect(service, &QLowEnergyService::characteristicsDiscovered, this, [service, this](){
            //     qDebug() << "Characteristics discovered for" << service->serviceUuid().toString();
            //     for (const QLowEnergyCharacteristic &characteristic : service->characteristics()) {
            //         qDebug() << "  Characteristic:" << characteristic.name() << "(" << characteristic.uuid().toString() << ")";
            //     }
            // });
            // service->discoverDetails(); // Discover characteristics for this service
        } else {
            qWarning() << "Failed to create service object for UUID:" << uuid.toString();
        }
    }
}

void MainWindow::controllerError(QLowEnergyController::Error error)
{
    qWarning() << "BLE Controller Error:" << error;
    statusLabel->setText("Status: Controller Error!");
    connectButton->setEnabled(true);
    scanButton->setEnabled(true);
    QString errorString;
    switch (error) {
    case QLowEnergyController::UnknownError:
        errorString = "Unknown error.";
        break;
    case QLowEnergyController::InvalidBluetoothAdapterError:
        errorString = "Invalid Bluetooth adapter.";
        break;
    case QLowEnergyController::ConnectionError:
        errorString = "Connection error.";
        break;
    case QLowEnergyController::AdvertisingError:
        errorString = "Advertising error.";
        break;
    case QLowEnergyController::RemoteHostClosedError:
        errorString = "Remote host closed connection.";
        break;
//    case QLowEnergyController::PermissionError:
//        errorString = "Permission error (check AndroidManifest.xml and runtime permissions).";
//        break;
    default:
        errorString = "Other error.";
        break;
    }
    QMessageBox::critical(this, "BLE Controller Error", errorString);

    // Ensure controller is cleaned up on error
    if (leController) {
        leController->deleteLater();
        leController = nullptr;
    }
    m_serviceUuids.clear();
}
