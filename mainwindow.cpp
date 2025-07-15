#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QMessageBox>
#include <QBluetoothPermission> // For Android permissions

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // --- UI Setup ---
    deviceListWidget = new QListWidget(this);
    scanButton = new QPushButton("Start Bluetooth Scan", this);

    // Create a central widget and layout
    QWidget *centralWidget = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(centralWidget);
    layout->addWidget(scanButton);
    layout->addWidget(deviceListWidget);
    centralWidget->setLayout(layout);
    setCentralWidget(centralWidget); // Set this layout as the main window's central widget

    // --- Bluetooth Setup ---
    discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);

    // Connect signals and slots for Bluetooth discovery
    connect(scanButton, &QPushButton::clicked, this, &MainWindow::startScan);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &MainWindow::deviceDiscovered);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &MainWindow::scanFinished);
    connect(discoveryAgent, &QBluetoothDeviceDiscoveryAgent::errorOccurred,
            this, &MainWindow::scanError);

    // --- Android Permissions (Crucial for Android 6.0+ and Android 12+) ---
    // This part ensures your app has the necessary Bluetooth permissions on Android.
#if QT_CONFIG(permissions) && defined(Q_OS_ANDROID)
    QBluetoothPermission bluetoothPermission;
    bluetoothPermission.setCommunicationModes(QBluetoothPermission::Access); // Request general Bluetooth access

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
    delete ui;
}


void MainWindow::startScan()
{
    deviceListWidget->clear(); // Clear previous scan results
    qDebug() << "Starting Bluetooth device scan...";
    scanButton->setEnabled(false); // Disable button during scan
    discoveryAgent->start();
    // You can set a timeout for the scan: discoveryAgent->start(QBluetoothDeviceDiscoveryAgent::FullDiscovery, 10000); // Scan for 10 seconds
}

void MainWindow::deviceDiscovered(const QBluetoothDeviceInfo &device)
{
    if (device.isValid()) {
        QString itemText = device.name().isEmpty() ? "(Unknown device)" : device.name();
        itemText += " (" + device.address().toString() + ")";
        deviceListWidget->addItem(itemText);
        qDebug() << "Discovered:" << device.name() << "(" << device.address().toString() << ")";
    }
}

void MainWindow::scanFinished()
{
    qDebug() << "Bluetooth scan finished.";
    scanButton->setEnabled(true); // Re-enable the button
    if (deviceListWidget->count() == 0) {
        deviceListWidget->addItem("No Bluetooth devices found.");
    }
}

void MainWindow::scanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    qWarning() << "Bluetooth scan error:" << error;
    scanButton->setEnabled(true); // Re-enable the button

    QString errorString;
    switch (error) {
    case QBluetoothDeviceDiscoveryAgent::InputOutputError:
        errorString = "Input/Output Error. Check Bluetooth hardware and drivers.";
        break;
    case QBluetoothDeviceDiscoveryAgent::PoweredOffError:
        errorString = "Bluetooth is powered off.";
        break;
    case QBluetoothDeviceDiscoveryAgent::UnsupportedPlatformError:
        errorString = "Unsupported discovery method.";
        break;
    case QBluetoothDeviceDiscoveryAgent::MissingPermissionsError:
        errorString = "Missing Bluetooth permissions.";
        break;
    default:
        errorString = "An unknown Bluetooth error occurred.";
        break;
    }
    QMessageBox::critical(this, "Bluetooth Scan Error", errorString);
}

