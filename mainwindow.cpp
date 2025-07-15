#include "mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QBluetoothPermission>
#include <QLowEnergyDescriptor>
#include <QApplication>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(nullptr) // Set to nullptr since you're not using a .ui file
    , leController(nullptr)
    , m_currentService(nullptr) // Initialize current service pointer
{
    // --- UI Setup ---
    deviceListWidget = new QListWidget(this); // Shows devices, then services
    characteristicListWidget = new QListWidget(this); // Shows characteristics and their values
    scanButton = new QPushButton("Start Bluetooth Scan", this);
    connectButton = new QPushButton("Connect to Selected Device", this);
    connectButton->setEnabled(false);
    readCharButton = new QPushButton("Read Selected Characteristic", this); // New button
    readCharButton->setEnabled(false); // Disable initially
    statusLabel = new QLabel("Status: Idle", this);

    // Create a main layout to hold two vertical sub-layouts (one for devices/services, one for characteristics)
    QHBoxLayout *mainHorizontalLayout = new QHBoxLayout();

    // Left side: Scan/Connect buttons and Device/Service List
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(scanButton);
    leftLayout->addWidget(connectButton);
    leftLayout->addWidget(deviceListWidget); // This will switch between devices and services

    // Right side: Characteristic List and Read Button
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(readCharButton);
    rightLayout->addWidget(characteristicListWidget);

    mainHorizontalLayout->addLayout(leftLayout);
    mainHorizontalLayout->addLayout(rightLayout);

    // Main vertical layout for status label and the combined horizontal layout
    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(mainHorizontalLayout);
    mainLayout->addWidget(statusLabel);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setLayout(mainLayout);
    setCentralWidget(centralWidget);

    // --- Bluetooth Scan Setup ---
    discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &MainWindow::deviceDiscovered);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &MainWindow::scanFinished);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &MainWindow::scanError);

    // --- Connect Button Logic ---
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToDevice);
    connect(deviceListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        bool enableConnect = deviceListWidget->selectedItems().count() > 0 &&
                             !deviceListWidget->currentItem()->text().contains("No") &&
                             !deviceListWidget->currentItem()->text().contains("found");
        connectButton->setEnabled(enableConnect);
        readCharButton->setEnabled(false); // Disable read button until char is selected
        // When device/service selection changes, clear characteristic list
        characteristicListWidget->clear();

        // If the selected item is a service (after service discovery is complete)
        if (leController && leController->state() == QLowEnergyController::DiscoveredState) {
            onServiceSelected();
        }
    });

    // --- New: Read Characteristic Button Logic ---
    connect(readCharButton, &QPushButton::clicked, this, [this]() {
        if (characteristicListWidget->selectedItems().isEmpty()) {
            QMessageBox::warning(this, "No Characteristic Selected", "Please select a characteristic to read.");
            return;
        }
        // This relies on the QListWidgetItem storing the characteristic
        // For simplicity, we'll iterate and find it, but a map is better for large lists
        QListWidgetItem *selectedItem = characteristicListWidget->currentItem();
        for (auto it = std::as_const(m_characteristicItems).begin(); it != std::as_const(m_characteristicItems).end(); ++it) {
            if (it.value() == selectedItem) {
                // Read the characteristic
                if (it.key().properties() & QLowEnergyCharacteristic::Read) {
                    if (m_currentService) {
                        m_currentService->readCharacteristic(it.key());
                        statusLabel->setText(QString("Status: Reading characteristic %1").arg(it.key().uuid().toString()));
                    } else {
                        qWarning() << "No current service selected for read.";
                    }
                } else {
                    QMessageBox::information(this, "Not Readable", "The selected characteristic is not readable.");
                }
                break;
            }
        }
    });

    // Enable read button only when a characteristic item is selected
    connect(characteristicListWidget, &QListWidget::itemSelectionChanged, this, [this]() {
        readCharButton->setEnabled(characteristicListWidget->selectedItems().count() > 0);
    });


    // --- Android Permissions (remains same) ---
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
    // Clean up QLowEnergyService objects
    for (QLowEnergyService *service : std::as_const(m_services)) {
        if (service) {
            // Optional: Disable notifications before deleting service
            for (const QLowEnergyCharacteristic &characteristic : service->characteristics()) {
                if (characteristic.properties() & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)) {
                    QLowEnergyDescriptor notificationDescriptor = characteristic.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                    if (notificationDescriptor.isValid()) {
                        service->writeDescriptor(notificationDescriptor, QByteArray(2, 0)); // Turn off notifications
                    }
                }
            }
            service->deleteLater();
        }
    }
    m_services.clear();

    if (leController) {
        leController->disconnectFromDevice();
        leController->deleteLater();
    }
}

// --- Bluetooth Scan Slots ---
void MainWindow::startScan()
{
    deviceListWidget->clear(); // Clear previous scan results (devices or services)
    characteristicListWidget->clear(); // Clear characteristic list
    statusLabel->setText("Status: Scanning...");
    qDebug() << "Starting Bluetooth device scan...";
    scanButton->setEnabled(false);
    connectButton->setEnabled(false);
    readCharButton->setEnabled(false);
    m_serviceUuids.clear(); // Clear previous service UUIDs
    m_services.clear();     // Clear service objects
    m_characteristicItems.clear(); // Clear characteristic items
    m_currentService = nullptr; // Reset current service

    if (leController) { // Disconnect if already connected
        leController->disconnectFromDevice();
        leController->deleteLater();
        leController = nullptr;
    }

    discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::DiscoveryMethod::LowEnergyMethod);
}

void MainWindow::deviceDiscovered(const QBluetoothDeviceInfo &device)
{
    if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
        QString itemText = device.name().isEmpty() ? "(Unknown BLE Device)" : device.name();
        itemText += " (" + device.address().toString() + ")";
        deviceListWidget->addItem(itemText);
        qDebug() << "Discovered BLE device:" << itemText;
    }
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
    readCharButton->setEnabled(false);
    QString errorString;
    switch (error) {
    case QBluetoothDeviceDiscoveryAgent::InputOutputError:
        errorString = "I/O Error (check permissions/hardware).";
        break;
    case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
        errorString = "Bluetooth is powered off.";
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

// --- BLE Connection Slots ---

void MainWindow::connectToDevice()
{
    if (deviceListWidget->selectedItems().isEmpty()) {
        QMessageBox::warning(this, "No Device Selected", "Please select a device from the list to connect.");
        return;
    }

    QString selectedText = deviceListWidget->currentItem()->text();
    QBluetoothAddress deviceAddress(selectedText.section('(', -1).remove(')'));

    QList<QBluetoothDeviceInfo> discoveredDevices = discoveryAgent->discoveredDevices();
    m_currentDevice = QBluetoothDeviceInfo(); // Reset
    for (const QBluetoothDeviceInfo &device : std::as_const(discoveredDevices)) {
        if (device.address() == deviceAddress) {
            m_currentDevice = device;
            break;
        }
    }

    if (!m_currentDevice.isValid()) {
        QMessageBox::critical(this, "Error", "Could not find selected device information.");
        return;
    }

    // Clean up previous controller and services if any
    if (leController) {
        leController->disconnectFromDevice();
        leController->deleteLater();
        leController = nullptr;
    }
    for (QLowEnergyService *service : std::as_const(m_services)) {
        if (service) service->deleteLater();
    }
    m_services.clear();
    m_serviceUuids.clear();
    m_characteristicItems.clear();
    m_currentService = nullptr;
    characteristicListWidget->clear();

    leController = QLowEnergyController::createCentral(m_currentDevice, this);
    if (!leController) {
        QMessageBox::critical(this, "Error", "Failed to create BLE controller.");
        statusLabel->setText("Status: Controller creation failed.");
        return;
    }

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
    readCharButton->setEnabled(false); // Disable read button

    // Clean up controller and services
    if (leController) {
        leController->deleteLater();
        leController = nullptr;
    }
    for (QLowEnergyService *service : std::as_const(m_services)) {
        if (service) service->deleteLater();
    }
    m_services.clear();
    m_serviceUuids.clear();
    m_characteristicItems.clear();
    m_currentService = nullptr;
    deviceListWidget->clear(); // Clear both lists on disconnect
    characteristicListWidget->clear();
}

void MainWindow::serviceDiscovered(const QBluetoothUuid &uuid)
{
    qDebug() << "Service Discovered:" << uuid.toString();
    m_serviceUuids.append(uuid); // Store the discovered UUID
}

void MainWindow::serviceDiscoveryFinished()
{
    qDebug() << "Service discovery finished. Found" << m_serviceUuids.count() << "services.";
    statusLabel->setText("Status: Services Discovered. Select a service.");

    deviceListWidget->clear(); // Clear the device list to show services
    deviceListWidget->addItem("--- Discovered Services ---");
    if (m_serviceUuids.isEmpty()) {
        deviceListWidget->addItem("No services found on this device.");
    } else {
        for (const QBluetoothUuid &uuid : std::as_const(m_serviceUuids)) {
            QString serviceInfo = uuid.toString();
            // Use QBluetoothUuid::uuidToName if available and not giving errors
            // if (uuid.isWellKnownUuid()) { // This check requires Qt 6.0+
            //     serviceInfo += " (" + QBluetoothUuid::uuidToName(uuid) + ")";
            // }
            deviceListWidget->addItem(serviceInfo);
        }
    }
    connectButton->setEnabled(false); // Keep connect disabled
    scanButton->setEnabled(true); // Allow new scan if needed
    // The readCharButton remains disabled until a characteristic is selected
}

void MainWindow::controllerError(QLowEnergyController::Error error)
{
    qWarning() << "BLE Controller Error:" << error;
    statusLabel->setText("Status: Controller Error!");
    connectButton->setEnabled(true);
    scanButton->setEnabled(true);
    readCharButton->setEnabled(false);
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
/*
    case QLowEnergyController::PermissionError:
        errorString = "Permission error (check AndroidManifest.xml and runtime permissions).";
        break;
*/
    default:
        errorString = "Other error.";
        break;
    }
    QMessageBox::critical(this, "BLE Controller Error", errorString);

    // Ensure controller and services are cleaned up on error
    if (leController) {
        leController->deleteLater();
        leController = nullptr;
    }
    for (QLowEnergyService *service : std::as_const(m_services)) {
        if (service) service->deleteLater();
    }
    m_services.clear();
    m_serviceUuids.clear();
    m_characteristicItems.clear();
    m_currentService = nullptr;
    deviceListWidget->clear();
    characteristicListWidget->clear();
}

// --- New: Service and Characteristic Interaction Slots ---

void MainWindow::onServiceSelected()
{
    if (deviceListWidget->selectedItems().isEmpty() || !leController ||
        leController->state() != QLowEnergyController::DiscoveredState) {
        return;
    }

    QString selectedServiceText = deviceListWidget->currentItem()->text();
    // Extract UUID from the displayed text (e.g., "0000180a-0000-1000-8000-00805f9b34fb (Device Information)")
    QString uuidString = selectedServiceText.split(" ").first();
    QBluetoothUuid selectedUuid(uuidString);

    // Check if we already have this service object
    if (m_services.contains(selectedUuid)) {
        m_currentService = m_services.value(selectedUuid);
        qDebug() << "Service already known, displaying characteristics for:" << selectedUuid.toString();
        // If characteristics were already discovered for this service, display them directly
        // Otherwise, discoverDetails() needs to be called again or waited for if already in progress
        if (m_currentService->state() == QLowEnergyService::RemoteServiceDiscovered) {
            // Redisplay characteristics if changing selection
            characteristicListWidget->clear();
            m_characteristicItems.clear();
            for (const QLowEnergyCharacteristic &characteristic : m_currentService->characteristics()) {
                QString charInfo = QString("  Char: %1 (%2) - Props: %3")
                .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                    .arg(characteristic.uuid().toString())
                    .arg(characteristic.properties());
                QListWidgetItem *item = new QListWidgetItem(charInfo);
                characteristicListWidget->addItem(item);
                m_characteristicItems.insert(characteristic, item);

                // Initial read for readable characteristics
                if (characteristic.properties() & QLowEnergyCharacteristic::Read) {
                    m_currentService->readCharacteristic(characteristic);
                }
                // Enable notifications/indications
                if (characteristic.properties() & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)) {
                    QLowEnergyDescriptor notificationDescriptor = characteristic.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                    if (notificationDescriptor.isValid()) {
                        m_currentService->writeDescriptor(notificationDescriptor, QByteArray::fromHex("0100")); // Enable Notifications
                        qDebug() << "Enabled notifications for characteristic:" << characteristic.uuid().toString();
                    }
                }
            }
        }
    } else {
        // If not, create and discover details for the selected service
        QLowEnergyService *service = leController->createServiceObject(selectedUuid, this);
        if (service) {
            m_services.insert(selectedUuid, service);
            m_currentService = service; // Set as current
            characteristicListWidget->clear(); // Clear old characteristics
            m_characteristicItems.clear(); // Clear old characteristic items

            connect(service, &QLowEnergyService::stateChanged,
                    this, &MainWindow::serviceDetailsDiscovered);
            connect(service, QOverload<QLowEnergyService::ServiceError>::of(&QLowEnergyService::errorOccurred),
                    this, &MainWindow::serviceError);
            connect(service, &QLowEnergyService::characteristicChanged,
                    this, &MainWindow::characteristicChanged);
            connect(service, &QLowEnergyService::characteristicRead,
                    this, &MainWindow::characteristicRead);
            connect(service, &QLowEnergyService::descriptorWritten,
                    this, &MainWindow::descriptorWritten);

            statusLabel->setText(QString("Status: Discovering characteristics for %1...").arg(selectedServiceText));
            service->discoverDetails(); // Start discovering characteristics for this service
        } else {
            statusLabel->setText("Status: Failed to create service object.");
            qWarning() << "Failed to create service object for:" << selectedUuid.toString();
            m_currentService = nullptr;
        }
    }
}

void MainWindow::serviceDetailsDiscovered(QLowEnergyService::ServiceState newState)
{
    QLowEnergyService *service = qobject_cast<QLowEnergyService*>(sender());
    if (!service) return;

    if (newState == QLowEnergyService::RemoteServiceDiscovered) {
        statusLabel->setText(QString("Status: Characteristics discovered for %1.").arg(service->serviceUuid().toString()));
        characteristicListWidget->clear(); // Clear previous characteristics
        m_characteristicItems.clear(); // Clear previous characteristic items

        for (const QLowEnergyCharacteristic &characteristic : service->characteristics()) {
            QString charInfo = QString("  Char: %1 (%2) - Props: %3")
            .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                .arg(characteristic.uuid().toString())
                .arg(characteristic.properties());
            QListWidgetItem *item = new QListWidgetItem(charInfo);
            characteristicListWidget->addItem(item);
            m_characteristicItems.insert(characteristic, item); // Store for later updates

            // Read value if readable
            if (characteristic.properties() & QLowEnergyCharacteristic::Read) {
                service->readCharacteristic(characteristic); // Triggers characteristicRead slot
            }

            // Enable notifications/indications if supported
            if (characteristic.properties() & (QLowEnergyCharacteristic::Notify | QLowEnergyCharacteristic::Indicate)) {
                QLowEnergyDescriptor notificationDescriptor = characteristic.descriptor(QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration);
                if (notificationDescriptor.isValid()) {
                    // Write to the Client Characteristic Configuration Descriptor (CCCD)
                    // 0x01 for notifications, 0x02 for indications
                    service->writeDescriptor(notificationDescriptor, QByteArray::fromHex("0100")); // Enable Notifications
                    qDebug() << "Enabled notifications for characteristic:" << characteristic.uuid().toString();
                }
            }
        }
    }
}

void MainWindow::characteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &newValue)
{
    // This slot is called when a characteristic's value changes (due to notification/indication)
    qDebug() << "Characteristic Changed:" << characteristic.uuid().toString() << "New Value:" << newValue.toHex();
    if (m_characteristicItems.contains(characteristic)) {
        QListWidgetItem *item = m_characteristicItems.value(characteristic);
        QString charInfo = QString("  Char: %1 (%2)\n  Value: %3 (Hex) / %4")
                               .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                               .arg(characteristic.uuid().toString())
                               .arg(newValue.toHex().toUpper())
                               .arg(QString::fromUtf8(newValue)); // Try to decode as UTF-8
        item->setText(charInfo);
    }
}

void MainWindow::characteristicRead(const QLowEnergyCharacteristic &characteristic, const QByteArray &value)
{
    // This slot is called after a readCharacteristic() request completes
    qDebug() << "Characteristic Read:" << characteristic.uuid().toString() << "Value:" << value.toHex();
    if (m_characteristicItems.contains(characteristic)) {
        QListWidgetItem *item = m_characteristicItems.value(characteristic);
        QString charInfo = QString("  Char: %1 (%2)\n  Value: %3 (Hex) / %4")
                               .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                               .arg(characteristic.uuid().toString())
                               .arg(value.toHex().toUpper())
                               .arg(QString::fromUtf8(value)); // Try to decode as UTF-8
        item->setText(charInfo);
    }
}

void MainWindow::descriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &newValue)
{
    if (descriptor.uuid() == QBluetoothUuid::DescriptorType::ClientCharacteristicConfiguration) {
        //qDebug() << "CCCD Written for characteristic:" << descriptor.characteristic().uuid().toString() << "Value:" << newValue.toHex();
        if (newValue == QByteArray::fromHex("0100")) {
            qDebug() << "Notifications enabled successfully.";
        } else if (newValue == QByteArray::fromHex("0200")) {
            qDebug() << "Indications enabled successfully.";
        } else if (newValue == QByteArray(2, 0)) {
            qDebug() << "Notifications/Indications disabled successfully.";
        }
    }
}

void MainWindow::serviceError(QLowEnergyService::ServiceError error)
{
    QLowEnergyService *service = qobject_cast<QLowEnergyService*>(sender());
    if (!service) return;

    qWarning() << "Service Error for" << service->serviceUuid().toString() << ":" << error;
    statusLabel->setText(QString("Status: Service %1 Error %2").arg(service->serviceUuid().toString()).arg(error));
}
