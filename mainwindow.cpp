#include "mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QBluetoothPermission>
#include <QLowEnergyDescriptor>
#include <QApplication>
#include <QComboBox> // Add this include for QComboBox

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(nullptr)
    , leController(nullptr)
    , m_currentService(nullptr)
{
    // --- UI Setup ---
    // Change from QListWidget to QComboBox
    deviceComboBox = new QComboBox(this); // New: QComboBox for devices/services
    characteristicListWidget = new QListWidget(this); // Remains a QListWidget
    scanButton = new QPushButton("Start Bluetooth Scan", this);
    connectButton = new QPushButton("Connect to Selected Device", this);
    connectButton->setEnabled(false);
    // readCharButton removed
    statusLabel = new QLabel("Status: Idle", this);
    characteristicValueLabel = new QLabel("Characteristic Value: N/A", this); // New: Label for characteristic value

    // Create a main layout to hold two vertical sub-layouts (one for devices/services, one for characteristics)
    QHBoxLayout *mainHorizontalLayout = new QHBoxLayout();

    // Left side: Scan/Connect buttons and Device/Service List (now QComboBox)
    QVBoxLayout *leftLayout = new QVBoxLayout();
    leftLayout->addWidget(scanButton);
    leftLayout->addWidget(connectButton);
    leftLayout->addWidget(deviceComboBox); // Use deviceComboBox here

    // Right side: Characteristic List (read button removed)
    QVBoxLayout *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(characteristicListWidget); // Only characteristic list
    rightLayout->addWidget(characteristicValueLabel); // Add the new label here

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
    // Change signal from itemSelectionChanged to currentIndexChanged
    connect(connectButton, &QPushButton::clicked, this, &MainWindow::connectToDevice);
    connect(deviceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        bool enableConnect = index >= 0 &&
                             !deviceComboBox->currentText().contains("No") &&
                             !deviceComboBox->currentText().contains("found") &&
                             !deviceComboBox->currentText().contains("--- Discovered Services ---"); // Exclude separator
        connectButton->setEnabled(enableConnect);
        // readCharButton->setEnabled(false); // Removed: Disable read button
        characteristicListWidget->clear();
        characteristicValueLabel->setText("Characteristic Value: N/A"); // Clear value when selection changes

        // If the selected item is a service (after service discovery is complete)
        if (leController && leController->state() == QLowEnergyController::DiscoveredState) {
            onServiceSelected(); // This will now be called when a service is selected from the combobox
        }
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
    deviceComboBox->clear(); // Change: Clear the QComboBox
    characteristicListWidget->clear();
    statusLabel->setText("Status: Scanning...");
    qDebug() << "Starting Bluetooth device scan...";
    scanButton->setEnabled(false);
    connectButton->setEnabled(false);
    // readCharButton removed here
    m_serviceUuids.clear();
    m_services.clear();
    m_characteristicItems.clear();
    m_currentService = nullptr;

    if (leController) {
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
        deviceComboBox->addItem(itemText); // Change: Add item to QComboBox
        qDebug() << "Discovered BLE device:" << itemText;
    }
}

void MainWindow::scanFinished()
{
    qDebug() << "Bluetooth scan finished.";
    statusLabel->setText("Status: Scan Finished.");
    scanButton->setEnabled(true);
    if (deviceComboBox->count() == 0) { // Change: Check QComboBox count
        deviceComboBox->addItem("No Bluetooth devices found."); // Change: Add item to QComboBox
        connectButton->setEnabled(false);
    } else {
        connectButton->setEnabled(true);
    }
}

void MainWindow::scanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    qWarning() << "Bluetooth scan error:" << error;
    statusLabel->setText("Status: Scan Error!");
    scanButton->setEnabled(true);
    connectButton->setEnabled(false);
    // readCharButton removed here
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
void MainWindow::deviceDisconnected()
{
    qDebug() << "Disconnected from BLE device.";
    statusLabel->setText("Status: Disconnected.");
    connectButton->setEnabled(true);
    scanButton->setEnabled(true);
    // readCharButton removed here

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
    deviceComboBox->clear(); // Change: Clear the QComboBox
    characteristicListWidget->clear();
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

void MainWindow::connectToDevice()
{
    // Change: Check if there's a selected item in QComboBox
    if (deviceComboBox->currentIndex() == -1 || deviceComboBox->currentText().contains("No Bluetooth devices found.")) {
        QMessageBox::warning(this, "No Device Selected", "Please select a device from the list to connect.");
        return;
    }

    QString selectedText = deviceComboBox->currentText(); // Change: Get current text from QComboBox
    QBluetoothAddress deviceAddress(selectedText.section('(', -1).remove(')'));

    QList<QBluetoothDeviceInfo> discoveredDevices = discoveryAgent->discoveredDevices();
    m_currentDevice = QBluetoothDeviceInfo();
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
    connectButton->setEnabled(false);
    scanButton->setEnabled(false);
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

    deviceComboBox->clear(); // Change: Clear the QComboBox
    deviceComboBox->addItem("--- Discovered Services ---"); // Change: Add separator to QComboBox
    if (m_serviceUuids.isEmpty()) {
        deviceComboBox->addItem("No services found on this device."); // Change: Add item to QComboBox
    } else {
        for (const QBluetoothUuid &uuid : std::as_const(m_serviceUuids)) {
            QString serviceInfo = uuid.toString();
            deviceComboBox->addItem(serviceInfo); // Change: Add item to QComboBox
        }
    }
    connectButton->setEnabled(false);
    scanButton->setEnabled(true);
}

void MainWindow::controllerError(QLowEnergyController::Error error)
{
    qWarning() << "BLE Controller Error:" << error;
    statusLabel->setText("Status: Controller Error!");
    connectButton->setEnabled(true);
    scanButton->setEnabled(true);
    // readCharButton removed here
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
    default:
        errorString = "Other error.";
        break;
    }
    QMessageBox::critical(this, "BLE Controller Error", errorString);

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
    deviceComboBox->clear(); // Change: Clear the QComboBox
    characteristicListWidget->clear();
}

// --- New: Service and Characteristic Interaction Slots ---
void MainWindow::onServiceSelected()
{
    // Change: Check if an item is selected in QComboBox
    if (deviceComboBox->currentIndex() == -1 || !leController ||
        leController->state() != QLowEnergyController::DiscoveredState ||
        deviceComboBox->currentText().contains("--- Discovered Services ---") || // Don't try to select the separator
        deviceComboBox->currentText().contains("No services found")) {
        return;
    }

    QString selectedServiceText = deviceComboBox->currentText(); // Change: Get current text from QComboBox
    QString uuidString = selectedServiceText.split(" ").first();
    QBluetoothUuid selectedUuid(uuidString);

    if (m_services.contains(selectedUuid)) {
        m_currentService = m_services.value(selectedUuid);
        qDebug() << "Service already known, displaying characteristics for:" << selectedUuid.toString();
        if (m_currentService->state() == QLowEnergyService::RemoteServiceDiscovered) {
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

                if (characteristic.properties() & QLowEnergyCharacteristic::Read) {
                    m_currentService->readCharacteristic(characteristic);
                }
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
        QLowEnergyService *service = leController->createServiceObject(selectedUuid, this);
        if (service) {
            m_services.insert(selectedUuid, service);
            m_currentService = service;
            characteristicListWidget->clear();
            m_characteristicItems.clear();

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
            service->discoverDetails();
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
    qDebug() << "Characteristic Changed:" << characteristic.uuid().toString() << "New Value:" << newValue.toHex();
    // Update the dedicated label with the value
    QString valueDisplay = QString("Value: %1 (Hex) / %2")
                               .arg(newValue.toHex().toUpper())
                               .arg(QString::fromUtf8(newValue));
    characteristicValueLabel->setText(valueDisplay);

    // Optionally, if you still want to update the list item's text to show the *current* value
    // alongside its properties, you can modify the item text like this:
/*    if (m_characteristicItems.contains(characteristic)) {
        QListWidgetItem *item = m_characteristicItems.value(characteristic);
        QString charInfo = QString("Char: %1 (%2)\nValue: %3 (Hex) / %4")
                               .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                               .arg(characteristic.uuid().toString())
                               .arg(newValue.toHex().toUpper())
                               .arg(QString::fromUtf8(newValue));
        item->setText(charInfo);
        }
*/

}

void MainWindow::characteristicRead(const QLowEnergyCharacteristic &characteristic, const QByteArray &value)
{
    qDebug() << "Characteristic Read:" << characteristic.uuid().toString() << "Value:" << value.toHex();
    // Update the dedicated label with the value
    QString valueDisplay = QString("Value: %1 (Hex) / %2")
                               .arg(value.toHex().toUpper())
                               .arg(QString::fromUtf8(value));
    characteristicValueLabel->setText(valueDisplay);

    // Optionally, if you still want to update the list item's text to show the *read* value
    // alongside its properties, you can modify the item text like this:
/*
    if (m_characteristicItems.contains(characteristic)) {
        QListWidgetItem *item = m_characteristicItems.value(characteristic);
        QString charInfo = QString("Char: %1 (%2)\nValue: %3 (Hex) / %4")
                               .arg(characteristic.name().isEmpty() ? characteristic.uuid().toString() : characteristic.name())
                               .arg(characteristic.uuid().toString())
                               .arg(value.toHex().toUpper())
                               .arg(QString::fromUtf8(value));
        item->setText(charInfo);
    }
*/
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
