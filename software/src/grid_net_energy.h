#ifndef GRID_NET_ENERGY_H
#define GRID_NET_ENERGY_H
#include <QObject>
#include <QElapsedTimer>
#include "net_energy.h"
#include "defines.h"

class AcSensor;
class AcSensorSettings;

class GridNetEnergy : public QObject
{
public:
    static bool enabledFor(AcSensor *sensor, AcSensorSettings *settings);
    GridNetEnergy(AcSensor *sensor, QObject *parent);
    ~GridNetEnergy();
    void rawEnergy(Phase phase, double energy, bool reverse);
    void sample();
    void invalidate() { mEnergy.invalidate(); }
    void save();

private:
    void load();
    void publish();
    QString stateFile() const;
    AcSensor *mSensor;
    NetEnergy mEnergy;
    QElapsedTimer mClock;
    std::array<double, 4> mRawForward, mRawReverse;
    double mStartedAt = 0;
};
#endif
