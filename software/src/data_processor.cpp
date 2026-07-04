#include <cmath>
#include <QsLog.h>
#include "ac_sensor.h"
#include "ac_sensor_settings.h"
#include "data_processor.h"
#include "ac_sensor_phase.h"

DataProcessor::DataProcessor(AcSensor *acSensor,
							 AcSensorSettings *settings, QObject *parent) :
	QObject(parent),
	mAcSensor(acSensor),
	mSettings(settings),
	mStoreReverseEnergy(false)
{
	Q_ASSERT(acSensor != 0);
	Q_ASSERT(settings != 0);
	memset(mPositivePower, 0, sizeof(mPositivePower));
	memset(mNegativePower, 0, sizeof(mNegativePower));
}

void DataProcessor::setPower(Phase phase, double value)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	pi->setPower(value);
	bool countPositivePower = value > 0;
	bool countNegativePower = value < 0;
	if (phase != MultiPhase) {
		double totalPower = mAcSensor->total()->power();
		// A phase can briefly have the opposite sign from the aggregate. Only
		// use phase power for energy allocation when it matches the total flow.
		countPositivePower = countPositivePower &&
			(!qIsFinite(totalPower) || totalPower > 0);
		countNegativePower = countNegativePower &&
			(!qIsFinite(totalPower) || totalPower < 0);
	}
	if (countPositivePower)
		mPositivePower[phase] += value;
	if (countNegativePower)
		mNegativePower[phase] -= value;
}

void DataProcessor::setVoltage(Phase phase, double value)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	pi->setVoltage(value);
}

void DataProcessor::setCurrent(Phase phase, double value)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	pi->setCurrent(value);
}

void DataProcessor::setPositiveEnergy(Phase phase, double value)
{
	setForwardEnergy(phase, value);
}

void DataProcessor::setPositiveEnergy(double sum)
{
	double e1 = getForwardEnergy(PhaseL1);
	if (!qIsFinite(e1)) {
		// Seed synthetic phase counters from the authoritative total. Later
		// total-counter deltas are distributed by accumulated positive power.
		setForwardEnergy(PhaseL1, sum / 3);
		setForwardEnergy(PhaseL2, sum / 3);
		setForwardEnergy(PhaseL3, sum / 3);
		setForwardEnergy(MultiPhase, sum);
		memset(mPositivePower, 0, sizeof(mPositivePower));
		return;
	}
	double prevEnergy = getForwardEnergy(MultiPhase);
	double delta = sum - prevEnergy;
	if (delta <= 0)
		return;
	setForwardEnergy(MultiPhase, sum);
	double phasePower =
		mPositivePower[PhaseL1] +
		mPositivePower[PhaseL2] +
		mPositivePower[PhaseL3];
	if (qIsFinite(phasePower) && phasePower > 0) {
		setForwardEnergy(PhaseL1,
			getForwardEnergy(PhaseL1) + delta * mPositivePower[PhaseL1] / phasePower);
		setForwardEnergy(PhaseL2,
			getForwardEnergy(PhaseL2) + delta * mPositivePower[PhaseL2] / phasePower);
		setForwardEnergy(PhaseL3,
			getForwardEnergy(PhaseL3) + delta * mPositivePower[PhaseL3] / phasePower);
	} else {
		setForwardEnergy(PhaseL1, getForwardEnergy(PhaseL1) + delta / 3);
		setForwardEnergy(PhaseL2, getForwardEnergy(PhaseL2) + delta / 3);
		setForwardEnergy(PhaseL3, getForwardEnergy(PhaseL3) + delta / 3);
	}
	memset(mPositivePower, 0, sizeof(mPositivePower));
}

void DataProcessor::setNegativeEnergy(double sum)
{
	mStoreReverseEnergy = true;
	double e1 = getReverseEnergy(PhaseL1);
	double phaseEnergy =
		e1 +
		getReverseEnergy(PhaseL2) +
		getReverseEnergy(PhaseL3);
	if (!qIsFinite(e1) ||
		!qIsFinite(phaseEnergy) ||
		qAbs(phaseEnergy - sum) > 0.05) {
		// Stored reverse phase counters from old versions may not sum to the
		// meter total. Rebase them once, then distribute future deltas normally.
		setReverseEnergy(PhaseL1, sum / 3);
		setReverseEnergy(PhaseL2, sum / 3);
		setReverseEnergy(PhaseL3, sum / 3);
		setReverseEnergy(MultiPhase, sum);
		memset(mNegativePower, 0, sizeof(mNegativePower));
		return;
	}
	double prevEnergy = getReverseEnergy(MultiPhase);
	double delta = sum - prevEnergy;
	if (delta <= 0)
		return;
	setReverseEnergy(MultiPhase, sum);
	double phasePower =
		mNegativePower[PhaseL1] +
		mNegativePower[PhaseL2] +
		mNegativePower[PhaseL3];
	if (qIsFinite(phasePower) && phasePower > 0) {
		setReverseEnergy(PhaseL1,
			getReverseEnergy(PhaseL1) + delta * mNegativePower[PhaseL1] / phasePower);
		setReverseEnergy(PhaseL2,
			getReverseEnergy(PhaseL2) + delta * mNegativePower[PhaseL2] / phasePower);
		setReverseEnergy(PhaseL3,
			getReverseEnergy(PhaseL3) + delta * mNegativePower[PhaseL3] / phasePower);
	} else {
		setReverseEnergy(PhaseL1, getReverseEnergy(PhaseL1) + delta / 3);
		setReverseEnergy(PhaseL2, getReverseEnergy(PhaseL2) + delta / 3);
		setReverseEnergy(PhaseL3, getReverseEnergy(PhaseL3) + delta / 3);
	}
	memset(mNegativePower, 0, sizeof(mNegativePower));
}

void DataProcessor::setNegativeEnergy(Phase phase, double value)
{
	setReverseEnergy(phase, value);
}

void DataProcessor::setFrequency(double v)
{
	mAcSensor->setFrequency(v);
}

void DataProcessor::setPowerFactor(Phase phase, double f)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi != 0)
		pi->setPowerFactor(f);
}

void DataProcessor::updateEnergySettings()
{
	if (!mStoreReverseEnergy)
		return;
	updateEnergySettings(PhaseL1);
	updateEnergySettings(PhaseL2);
	updateEnergySettings(PhaseL3);
}

void DataProcessor::updateEnergySettings(Phase phase)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi == 0)
		return;
	double e = getReverseEnergy(phase);
	if (qIsFinite(e))
		mSettings->setReverseEnergy(phase, e);
}

double DataProcessor::getForwardEnergy(Phase phase)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi == 0)
		return 0;
	return pi->energyForward();
}

void DataProcessor::setForwardEnergy(Phase phase, double value)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi == 0)
		return;
	pi->setEnergyForward(value);
}

void DataProcessor::setInitialEnergy(Phase phase, double defaultValue)
{
	double v = mSettings->getReverseEnergy(phase);
	if (v == 0)
		v = defaultValue;
	setReverseEnergy(phase, v);
}

double DataProcessor::getReverseEnergy(Phase phase)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi == 0)
		return 0;
	return pi->energyReverse();
}

void DataProcessor::setReverseEnergy(Phase phase, double value)
{
	AcSensorPhase *pi = mAcSensor->getPhase(phase);
	if (pi == 0)
		return;
	pi->setEnergyReverse(value);
}
