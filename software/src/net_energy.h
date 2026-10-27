#ifndef NET_ENERGY_H
#define NET_ENERGY_H

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

// No clock or I/O dependencies: integrate a complete, successful phase sample.
class NetEnergy
{
public:
    using Phases = std::array<double, 3>;
    Phases forward{{0, 0, 0}}, reverse{{0, 0, 0}};
    double skippedSeconds = 0;
    bool initialized = false;

    static double total(const Phases &p) { return std::accumulate(p.begin(), p.end(), 0.0); }

    void seed(double imported, double exported, const Phases &rawForward, const Phases &rawReverse)
    {
        forward = distributeSeed(imported, rawForward);
        reverse = distributeSeed(exported, rawReverse);
        initialized = true;
        mHaveTime = mValid = false;
    }

    void invalidate() { mValid = false; }

    bool sample(const Phases &power, double seconds, double maxGap = 10)
    {
        if (!initialized) return false;
        if (!std::isfinite(seconds) || !std::all_of(power.begin(), power.end(),
                [](double p) { return std::isfinite(p); })) {
            invalidate();
            return false;
        }
        const double dt = seconds - mLastTime;
        bool integrated = false;
        if (mHaveTime && dt > 0) {
            if (mValid && dt <= maxGap) {
                double a = total(mLastPower), b = total(power);
                if (a * b < 0) {
                    double crossing = a / (a - b);
                    integrate(power, dt, 0, crossing);
                    integrate(power, dt, crossing, 1);
                } else {
                    integrate(power, dt, 0, 1);
                }
                integrated = true;
            } else {
                skippedSeconds += dt;
            }
        }
        mLastTime = seconds;
        mLastPower = power;
        mHaveTime = mValid = true;
        return integrated;
    }

private:
    bool mHaveTime = false, mValid = false;
    double mLastTime = 0;
    Phases mLastPower{{0, 0, 0}};

    static Phases distributeSeed(double energy, const Phases &raw)
    {
        Phases result;
        double sum = total(raw);
        for (unsigned i = 0; i < 3; ++i)
            result[i] = energy * (sum > 0 ? raw[i] / sum : 1.0 / 3);
        return result;
    }

    void integrate(const Phases &power, double dt, double lo, double hi)
    {
        double a = total(mLastPower), b = total(power);
        double from = a + (b - a) * lo, to = a + (b - a) * hi;
        double middle = (lo + hi) / 2;
        bool importing = a + (b - a) * middle >= 0;
        double energy = (std::abs(from) + std::abs(to)) / 2 * dt * (hi - lo) / 3600000;
        if (energy == 0) return;
        Phases weights;
        for (unsigned i = 0; i < 3; ++i) {
            double p = mLastPower[i] + (power[i] - mLastPower[i]) * middle;
            weights[i] = std::max(importing ? p : -p, 0.0);
        }
        double sum = total(weights);
        Phases &destination = importing ? forward : reverse;
        for (unsigned i = 0; i < 3; ++i)
            destination[i] += energy * (sum > 0 ? weights[i] / sum : 1.0 / 3);
    }
};
#endif
