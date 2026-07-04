#ifndef DATA_PROCESSOR_H
#define DATA_PROCESSOR_H

#include <QObject>
#include "defines.h"

class AcSensor;
class AcSensorSettings;

/*!
 * Processes energy meter data from an and stores it in an `AcSensor` object.
 * This class will also compute values needed for the D-Bus service which cannot
 * be obtained from the energy meter directly.
 */
class DataProcessor : public QObject
{
	Q_OBJECT
public:
	explicit DataProcessor(AcSensor *acSensor,
						   AcSensorSettings *settings, QObject *parent = 0);

	void setPower(Phase phase, double value);

	void setVoltage(Phase phase, double value);

	void setCurrent(Phase phase, double value);

	void setPositiveEnergy(Phase phase, double value);

	void setPositiveEnergy(double sum);

	void setNegativeEnergy(double sum);

	void setNegativeEnergy(Phase phase, double value);

	void setFrequency(double v);

	void setPowerFactor(Phase phase, double f);

	void updateEnergySettings();

private:
	double getForwardEnergy(Phase phase);

	void setForwardEnergy(Phase phase, double value);

	double getReverseEnergy(Phase phase);

	void setReverseEnergy(Phase phase, double value);

	void updateEnergySettings(Phase phase);

	void setInitialEnergy(Phase phase, double defaultValue);

	AcSensor *mAcSensor;
	AcSensorSettings *mSettings;
	double mPositivePower[4];
	double mNegativePower[4];
	bool mStoreReverseEnergy;
};

#endif // DATA_PROCESSOR_H
