#include "grid_net_energy.h"
#include "ac_sensor.h"
#include "ac_sensor_phase.h"
#include "ac_sensor_settings.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QsLog.h>
#include <limits>

static double epoch() { return QDateTime::currentMSecsSinceEpoch() / 1000.0; }

bool GridNetEnergy::enabledFor(AcSensor *sensor, AcSensorSettings *settings)
{
    return sensor->protocolType() == AcSensor::Et340Protocol && settings->isMultiPhase()
        && !settings->piggyEnabled() && settings->serviceType() == "grid"
        && !sensor->serial().isEmpty()
        && qEnvironmentVariable("CGWACS_NET_METER_SERIAL") == sensor->serial();
}

GridNetEnergy::GridNetEnergy(AcSensor *sensor, QObject *parent) : QObject(parent), mSensor(sensor)
{
    mRawForward.fill(qQNaN());
    mRawReverse.fill(qQNaN());
    connect(mSensor, &QObject::destroyed, this, [this]() { mSensor = nullptr; });
    mClock.start();
    load();
    auto timer = new QTimer(this);
    timer->setInterval(60000);
    connect(timer, &QTimer::timeout, this, &GridNetEnergy::save);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &GridNetEnergy::save);
    timer->start();
}

GridNetEnergy::~GridNetEnergy() { save(); }

QString GridNetEnergy::stateFile() const
{
    QString directory = qEnvironmentVariable("CGWACS_NET_STATE_DIR", "/data/smart-grid-energy");
    // The serial is supplied by the physical meter, not a filesystem path.
    QString serial = mSensor->serial();
    serial.replace('/', '_');
    return directory + '/' + serial + ".json";
}

void GridNetEnergy::load()
{
    QFile file(stateFile());
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read grid net-energy state; refusing to reset counters");
    QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    auto readPhases = [&](const char *key, NetEnergy::Phases &values) {
        QJsonArray array = object[key].toArray();
        if (array.size() != 3) return false;
        for (int i = 0; i < 3; ++i) {
            if (!array[i].isDouble()) return false;
            values[i] = array[i].toDouble();
            if (!qIsFinite(values[i]) || values[i] < 0) return false;
        }
        return true;
    };
    double savedAt = object["saved_at"].toDouble(-1);
    double skipped = object["skipped_seconds"].toDouble(-1);
    mStartedAt = object["started_at"].toDouble(-1);
    if (object["version"].toInt() != 1 || object["serial"].toString() != mSensor->serial()
        || !readPhases("forward", mEnergy.forward) || !readPhases("reverse", mEnergy.reverse)
        || savedAt <= 0 || skipped < 0 || mStartedAt <= 0)
        qFatal("Invalid grid net-energy state; refusing to reset counters");
    mEnergy.initialized = true;
    // Never bridge restarts with an old power value. Checkpoint loss/downtime is visible.
    mEnergy.skippedSeconds = skipped + std::max(0.0, epoch() - savedAt);
    mSensor->setNetEnergyPersistence(true, savedAt);
    publish();
    QLOG_INFO() << "Restored persistent phase-netted grid energy for" << mSensor->serial();
}

void GridNetEnergy::rawEnergy(Phase phase, double energy, bool reverse)
{
    if (!qIsFinite(energy) || energy < 0) return;
    (reverse ? mRawReverse : mRawForward)[phase] = energy;
    if (phase == MultiPhase) mSensor->setRawEnergy(energy, reverse);
    if (mEnergy.initialized) return;
    if (!std::all_of(mRawForward.begin(), mRawForward.end(), [](double v) { return qIsFinite(v); })
        || !std::all_of(mRawReverse.begin(), mRawReverse.end(), [](double v) { return qIsFinite(v); })) return;
    mEnergy.seed(mRawForward[0], mRawReverse[0],
        {{mRawForward[1], mRawForward[2], mRawForward[3]}},
        {{mRawReverse[1], mRawReverse[2], mRawReverse[3]}});
    mStartedAt = epoch();
    publish();
    save();
    QLOG_INFO() << "Started phase-netted grid energy from existing hardware totals for" << mSensor->serial();
}

void GridNetEnergy::sample()
{
    mEnergy.sample({{mSensor->l1()->power(), mSensor->l2()->power(), mSensor->l3()->power()}},
        mClock.nsecsElapsed() / 1e9);
    publish();
}

void GridNetEnergy::publish()
{
    if (!mEnergy.initialized) return;
    mSensor->total()->setEnergyForward(NetEnergy::total(mEnergy.forward));
    mSensor->total()->setEnergyReverse(NetEnergy::total(mEnergy.reverse));
    for (int i = 0; i < 3; ++i) {
        auto phase = mSensor->getPhase(static_cast<Phase>(i + 1));
        phase->setEnergyForward(mEnergy.forward[i]);
        phase->setEnergyReverse(mEnergy.reverse[i]);
    }
    mSensor->setNetEnergyStatus(true, mStartedAt, mEnergy.skippedSeconds);
}

void GridNetEnergy::save()
{
    if (!mSensor || !mEnergy.initialized) return;
    QDir().mkpath(QFileInfo(stateFile()).absolutePath());
    QJsonArray forward, reverse;
    for (double v : mEnergy.forward) forward.append(v);
    for (double v : mEnergy.reverse) reverse.append(v);
    double now = epoch();
    QJsonObject object{{"version", 1}, {"serial", mSensor->serial()},
        {"forward", forward}, {"reverse", reverse}, {"started_at", mStartedAt},
        {"saved_at", now}, {"skipped_seconds", mEnergy.skippedSeconds},
        {"hardware_forward", mRawForward[0]}, {"hardware_reverse", mRawReverse[0]}};
    QSaveFile file(stateFile());
    QByteArray bytes = QJsonDocument(object).toJson();
    bool ok = file.open(QIODevice::WriteOnly)
        && file.write(bytes) == bytes.size() && file.commit();
    mSensor->setNetEnergyPersistence(ok, ok ? now : mSensor->netEnergyLastSave());
    if (!ok) { QLOG_ERROR() << "Cannot checkpoint phase-netted energy:" << file.errorString(); }
}
