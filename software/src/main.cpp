#include <QCoreApplication>
#include <QsLog.h>
#include <QStringList>
#include <velib/qt/ve_qitem.hpp>
#include <velib/qt/ve_qitems_dbus.hpp>
#include <velib/qt/ve_qitem_dbus_publisher.hpp>
#include "dbus_bridge.h"
#include "dbus_cgwacs.h"

void initLogger(QsLogging::Level logLevel)
{
	QsLogging::Logger &logger = QsLogging::Logger::instance();
	QsLogging::DestinationPtr debugDestination(
			QsLogging::DestinationFactory::MakeDebugOutputDestination());
	logger.addDestination(debugDestination);
	logger.setIncludeTimestamp(false);

	QLOG_INFO() << "dbus-cgwacs" << "v"VERSION << "started";
	QLOG_INFO() << "Built with Qt" << QT_VERSION_STR << "running on" << qVersion();
	QLOG_INFO() << "Built on" << __DATE__ << "at" << __TIME__;
	logger.setLoggingLevel(logLevel);
}

extern "C"
{
// This function is called by the serial port API from velib when the device is
// disconnected from the serial port.
void pltExit(int ret)
{
	QCoreApplication::exit(ret);
}
}

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	app.setApplicationVersion(VERSION);

	initLogger(QsLogging::InfoLevel);

	bool expectVerbosity = false;
	bool expectDBusAddress = false;
	bool isZigbee = false;
	QString portName;
	QString dbusAddress = "system";
	QStringList args = app.arguments();
	args.pop_front();
	foreach (QString arg, args) {
		if (expectVerbosity) {
			QsLogging::Logger &logger = QsLogging::Logger::instance();
			QsLogging::Level logLevel = static_cast<QsLogging::Level>(qBound(
				static_cast<int>(QsLogging::TraceLevel),
				arg.toInt(),
				static_cast<int>(QsLogging::OffLevel)));
			logger.setLoggingLevel(logLevel);
			expectVerbosity = false;
		} else if (expectDBusAddress) {
			dbusAddress = arg;
			expectDBusAddress = false;
		} else if (arg == "-h" || arg == "--help") {
			QLOG_INFO() << app.arguments().first();
			QLOG_INFO() << "\t-h, --help";
			QLOG_INFO() << "\t Show this message.";
			QLOG_INFO() << "\t-V, --version";
			QLOG_INFO() << "\t Show the application version.";
			QLOG_INFO() << "\t-d level, --debug level";
			QLOG_INFO() << "\t Set log level";
			QLOG_INFO() << "\t-b, --dbus";
			QLOG_INFO() << "\t dbus address or 'session' or 'system'";
			QLOG_INFO() << "\t <Port Name>";
			QLOG_INFO() << "\t Name of communication port (eg. /dev/ttyUSB0)";
			exit(1);
		} else if (arg == "-V" || arg == "--version") {
			QLOG_INFO() << VERSION;
			exit(0);
		} else if (arg == "-d" || arg == "--debug") {
			expectVerbosity = true;
		} else if (arg == "-t" || arg == "--timestamp") {
			QsLogging::Logger &logger = QsLogging::Logger::instance();
			logger.setIncludeTimestamp(true);
		} else if (arg == "-b" || arg == "--dbus") {
			expectDBusAddress = true;
		} else if (arg == "-z" || arg == "--zigbee") {
			isZigbee = true;
		} else if (!arg.startsWith('-')) {
			portName = arg;
		}
	}

	if (portName.isEmpty()) {
		QLOG_ERROR() << "No communication port specified on command line";
		exit(2);
	} else {
		QLOG_INFO() << "Connecting to" << portName;
	}

	VeQItemDbusProducer producer(VeQItems::getRoot(), "sub", false, false);
	producer.open(dbusAddress);

	BridgeItemProducer dbusExportProducer(VeQItems::getRoot(), "pub");
	dbusExportProducer.open();
	VeQItemDbusPublisher publisher(dbusExportProducer.services());
	publisher.open(dbusAddress);

	DBusCGwacs a(portName, isZigbee);

	app.connect(&a, SIGNAL(connectionLost()), &app, SLOT(quit()));

	return app.exec();
}
