#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QThread>
#include <cassert>
#include <iostream>
#include "../software/src/ac_sensor.h"
#include "../software/src/ac_sensor_phase.h"
#include "../software/src/grid_net_energy.h"

static void near(double a, double b) { assert(std::abs(a - b) < 1e-8); }
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2 || !QString(argv[1]).startsWith("/tmp/")) return 2;
    qputenv("CGWACS_NET_STATE_DIR", argv[1]);
    AcSensor sensor("/dev/offline-test", 1);
    sensor.setSerial("OFFLINE_TEST");
    sensor.setDeviceType(345);
    int rawReverseChanges = 0, saveChanges = 0;
    QObject::connect(&sensor, &AcSensor::rawEnergyReverseChanged, [&]() { ++rawReverseChanges; });
    QObject::connect(&sensor, &AcSensor::netEnergyLastSaveChanged, [&]() { ++saveChanges; });
    double before, started;
    {
        GridNetEnergy energy(&sensor, nullptr);
        energy.rawEnergy(MultiPhase, 100, false);
        energy.rawEnergy(MultiPhase, 20, true);
        for (int i = 1; i <= 3; ++i) {
            energy.rawEnergy(static_cast<Phase>(i), 10 * i, false);
            energy.rawEnergy(static_cast<Phase>(i), i, true);
        }
        near(sensor.total()->energyForward(), 100);
        near(sensor.total()->energyReverse(), 20);
        assert(sensor.netEnergyActive() && sensor.netEnergyPersistenceOk());
        sensor.l1()->setPower(1000); sensor.l2()->setPower(-500); sensor.l3()->setPower(-500);
        energy.sample(); QThread::msleep(30); energy.sample();
        near(sensor.total()->energyForward(), 100);
        near(sensor.total()->energyReverse(), 20);
        // Raw arithmetic meter updates never overwrite corrected totals.
        energy.rawEnergy(MultiPhase, 101, false);
        energy.rawEnergy(MultiPhase, 21, true);
        assert(rawReverseChanges == 2);
        near(sensor.rawEnergyForward(), 101);
        near(sensor.total()->energyForward(), 100);
        sensor.l1()->setPower(2000);
        energy.sample(); QThread::msleep(100); energy.sample();
        assert(sensor.total()->energyForward() > 100);
        energy.save();
        assert(saveChanges >= 2);
        before = sensor.total()->energyForward();
        started = sensor.netEnergyStartedAt();
    }
    {
        GridNetEnergy restored(&sensor, nullptr);
        near(sensor.total()->energyForward(), before);
        near(sensor.netEnergyStartedAt(), started);
        assert(sensor.netEnergyPersistenceOk());
        restored.sample();
        near(sensor.total()->energyForward(), before); // No bridging process downtime.
        near(sensor.l1()->energyForward() + sensor.l2()->energyForward() + sensor.l3()->energyForward(), before);
    }
    // Also exercise sensor-owned QObject destruction, after derived fields die.
    auto owned = new AcSensor("/dev/offline-test", 1);
    owned->setSerial("OFFLINE_TEST");
    new GridNetEnergy(owned, owned);
    delete owned;
    std::cout << "Native persistence: atomic checkpoint, restore, no raw overwrite, phase sums and shutdown passed\n";
}
