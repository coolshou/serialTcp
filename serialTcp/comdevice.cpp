/*
  Serial - TCP

  Mario Ban, Atos AG, 02.2018

*/

#include "comdevice.h"
#include <simpleQtLogger.h>
#include <QTimer>

ComDevice::ComDevice(QObject *parent)
	: QObject(parent)
{
	L_FUNC("");
}

ComDevice::~ComDevice()
{
	L_FUNC("");
}

void ComDevice::init()
{
	L_FUNC("");
	L_FATAL("No implementation");
}

void ComDevice::slotDataSend(const QByteArray& data)
{
	L_FUNC("");
	L_FATAL("No implementation");
}

// -------------------------------------------------------------------------------------------------

ComDeviceSerial::ComDeviceSerial(const QString& serialPortName, QObject *parent)
	: ComDevice(parent)
	, _serialPortName(serialPortName)
	, _serialPort(0)
{
	L_FUNC("");
}

ComDeviceSerial::~ComDeviceSerial()
{
	L_FUNC("");
	if (_serialPort) {
		_serialPort->close();
		L_NOTE("Serial port closed");
	}
}

void ComDeviceSerial::init()
{
	L_FUNC("");

	_serialPort = new QSerialPort(this);
	_serialPort->setPortName(_serialPortName);

	_serialPort->setBaudRate(QSerialPort::Baud57600); // Baud9600
	_serialPort->setDataBits(QSerialPort::Data8);
	_serialPort->setParity(QSerialPort::NoParity);
	_serialPort->setStopBits(QSerialPort::OneStop);
	_serialPort->setFlowControl(QSerialPort::NoFlowControl);

	connect(_serialPort, &QSerialPort::readyRead, this, &ComDeviceSerial::slotReadyRead);
	connect(_serialPort, static_cast<void(QSerialPort::*)(QSerialPort::SerialPortError)>(&QSerialPort::error), this, &ComDeviceSerial::slotError);

	if (!_serialPort->open(QIODevice::ReadWrite)) {
		L_ERROR("Serial port open failed");
		emit finished();
	}
}

void ComDeviceSerial::slotDataSend(const QByteArray& data)
{
	// L_FUNC("");
	if (!_serialPort) {
		return;
	}

	qint64 number = _serialPort->write(data);
	if(number == -1) {
		L_ERROR("Serial port write failed");
		emit finished();
	}
	else if(number != data.size()) {
		L_WARN("Serial port write partial data");
		emit finished();
	}
}

void ComDeviceSerial::slotReadyRead()
{
	// L_FUNC("");
	if (!_serialPort) {
		return;
	}

	QByteArray data = _serialPort->readAll();
	emit signalDataRecv(data);
}

void ComDeviceSerial::slotError(QSerialPort::SerialPortError error)
{
	L_FUNC("");
	if (error == QSerialPort::NoError) {
		return;
	}
	L_WARN(QString("Serial port error: %1").arg(error));
	emit finished();
}

// -------------------------------------------------------------------------------------------------

ComDeviceTcp::ComDeviceTcp(const QString& localIp, const QString& localPort, QObject *parent)
	: ComDevice(parent)
	, _localIp(localIp)
	, _localPort(localPort)
	, _tcpServer(0)
{
	L_FUNC("");
}

ComDeviceTcp::~ComDeviceTcp()
{
	L_FUNC("");
	if (_tcpServer) {
		foreach(QTcpSocket* tcpSocket, _tcpSocketList) {
			tcpSocket->disconnectFromHost();
			L_NOTE("TCP-Socket closed");
		}
		_tcpServer->close();
		L_NOTE("TCP-Server closed");
	}
}

void ComDeviceTcp::init()
{
	L_FUNC("");

	_tcpServer = new QTcpServer(this);

	connect(_tcpServer, &QTcpServer::acceptError, this, &ComDeviceTcp::slotAcceptError);
	connect(_tcpServer, &QTcpServer::newConnection, this, &ComDeviceTcp::slotNewConnection);

	QHostAddress hostAddress = _localIp.compare("any", Qt::CaseInsensitive) == 0 ? QHostAddress::Any : QHostAddress(_localIp);
	if (!_tcpServer->listen(hostAddress, _localPort.toUShort())) {
		L_ERROR("TCP-Server listen failed");
		emit finished();
	}
	if (hostAddress.isNull()) {
		L_ERROR("TCP-Server listen address error");
		emit finished();
	}
	L_NOTE(QString("TCP-Server listening: %1 %2").arg(hostAddress.toString()).arg(_localPort));
}

void ComDeviceTcp::slotDataSend(const QByteArray& data)
{
	// L_FUNC("");
	if (!_tcpServer) {
		return;
	}

	foreach(QTcpSocket* tcpSocket, _tcpSocketList) {
		qint64 number = tcpSocket->write(data);
		if (number == -1) {
			L_ERROR("TCP-Socket write failed");
			emit finished();
		}
		else if (number != data.size()) {
			L_WARN("TCP-Socket write partial data");
			emit finished();
		}
	}
}

void ComDeviceTcp::slotAcceptError(QAbstractSocket::SocketError socketError)
{
	L_FUNC("");
	L_ERROR(QString("TCP-Server accept error: %1").arg(socketError));
	emit finished();
}

void ComDeviceTcp::slotNewConnection()
{
	L_FUNC("");
	if (!_tcpServer) {
		return;
	}

	QTcpSocket* tcpSocket = _tcpServer->nextPendingConnection();
	if (!tcpSocket) {
		return;
	}

	connect(tcpSocket, &QAbstractSocket::disconnected, this, &ComDeviceTcp::slotDisconnected);
	connect(tcpSocket, &QIODevice::readyRead, this, &ComDeviceTcp::slotReadyRead);

	_tcpSocketList << tcpSocket;

	L_NOTE("TCP-Socket connected");
}

void ComDeviceTcp::slotDisconnected()
{
	L_FUNC("");
	QTcpSocket* tcpSocket = qobject_cast<QTcpSocket*>(sender());
	if (!tcpSocket) {
		return;
	}
	_tcpSocketList.removeAll(tcpSocket);
	tcpSocket->deleteLater();
	L_NOTE("TCP-Socket closed");
}

void ComDeviceTcp::slotReadyRead()
{
	// L_FUNC("");
	QTcpSocket* tcpSocket = qobject_cast<QTcpSocket*>(sender());
	if (!tcpSocket) {
		return;
	}
	QByteArray data = tcpSocket->readAll();
	emit signalDataRecv(data);
}

// -------------------------------------------------------------------------------------------------

ComDeviceScreen::ComDeviceScreen(QObject *parent)
	: ComDevice(parent)
	, _textStreamIn(0)
	, _textStreamOut(0)
	, _socketNotifierIn(0)
	, _fileIn(0)
{
	L_FUNC("");
}

ComDeviceScreen::~ComDeviceScreen()
{
	L_FUNC("");
	if (_fileIn) {
		delete _fileIn;
	}
	if (_socketNotifierIn) {
		delete _socketNotifierIn;
	}
	if (_textStreamOut) {
		delete _textStreamOut;
	}
	if (_textStreamIn) {
		delete _textStreamIn;
	}
}

void ComDeviceScreen::init()
{
	L_FUNC("");

	//_textStreamIn = new QTextStream(stdin);
	_textStreamOut = new QTextStream(stdout);

	//_socketNotifierIn = new QSocketNotifier(0 /*stdin*/, QSocketNotifier::Read);

	//connect(_socketNotifierIn, &QSocketNotifier::activated, this, &ComDeviceScreen::slotActivated);

	//_fileIn = new QFile;
	//_fileIn->open(stdin, QIODevice::ReadOnly);

	//connect(_fileIn, &QIODevice::readyRead, this, &ComDeviceScreen::slotReadyRead);

	//QIODevice* ioDeviceIn = _textStreamIn->device();
	//if (!ioDeviceIn) {
	//	L_WARN("QIODevice error");
	//	emit finished();
	//}

	//connect(ioDeviceIn, &QIODevice::readyRead, this, &ComDeviceScreen::slotReadyRead);

	//QTimer::singleShot(10, this, SLOT(slotReadyRead()));
}

void ComDeviceScreen::slotDataSend(const QByteArray& data)
{
	// L_FUNC("");
	if (!_textStreamOut) {
		return;
	}

	_textStreamOut->operator<<(data);
	_textStreamOut->flush();
}

void ComDeviceScreen::slotReadyRead()
{
	L_FUNC("");
	//if (!_fileIn) {
	//	return;
	//}

	//if (_fileIn->bytesAvailable()) {

	//QByteArray data = _fileIn->readAll();

	//QByteArray data = _fileIn->readLine();
	//if (data.size()) {
	//	emit signalDataRecv(data);
	//}

	//}

	//QIODevice* ioDeviceIn = _textStreamIn->device();
	//if (!ioDeviceIn) {
	//	L_WARN("QIODevice error");
	//	emit finished();
	//}

	//if (_textStreamIn->atEnd()) {
	//	return;
	//}

	//QByteArray data = ioDeviceIn->readAll();

	//qint64 number = ioDeviceIn->bytesAvailable();
	//if (number) {
	//	QByteArray data = ioDeviceIn->read(number);
	//	emit signalDataRecv(data);
	//}
	//if (data.size()) {
	//	emit signalDataRecv(data);
	//}

	//QTimer::singleShot(10, this, SLOT(slotReadyRead()));
}

void ComDeviceScreen::slotActivated(int socket)
{
	L_FUNC("");
	//if (!_textStreamIn) {
	//	return;
	//}

	//QIODevice* ioDeviceIn = _textStreamIn->device();
	//if (!ioDeviceIn) {
	//	L_WARN("QIODevice error");
	//	emit finished();
	//}

	//QByteArray data = ioDeviceIn->readAll();
	//QByteArray data = QByteArray::fromStdString(_textStreamIn->readLine().toStdString());
	//emit signalDataRecv(data);
}
