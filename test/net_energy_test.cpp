#include "../software/src/net_energy.h"
#include <cassert>
#include <iostream>
#include <limits>

static void near(double actual, double expected) { assert(std::abs(actual - expected) < 1e-8); }
static NetEnergy meter(double forward = 0, double reverse = 0)
{
    NetEnergy m;
    m.seed(forward, reverse, {{1, 2, 3}}, {{3, 2, 1}});
    return m;
}

int main()
{
    // The real recorded mixed-phase example: net 41.6 W, not two gross flows.
    auto m = meter();
    for (int t = 0; t <= 3600; t += 2) m.sample({{1744.1, -895.6, -806.9}}, t);
    near(NetEnergy::total(m.forward), 0.0416);
    near(NetEnergy::total(m.reverse), 0);
    // Reverse direction and exact phase cancellation.
    auto reverse = meter();
    for (int t = 0; t <= 3600; t += 2) reverse.sample({{-2000, 500, 500}}, t);
    near(NetEnergy::total(reverse.reverse), 1);
    near(NetEnergy::total(reverse.forward), 0);
    auto balanced = meter();
    for (int t = 0; t <= 3600; t += 2) balanced.sample({{2000, -1000, -1000}}, t);
    near(NetEnergy::total(balanced.forward), 0);
    near(NetEnergy::total(balanced.reverse), 0);
    // Split the trapezoid at a zero crossing, so neither direction is lost.
    auto crossing = meter();
    crossing.sample({{1000, 0, 0}}, 0);
    crossing.sample({{-1000, 0, 0}}, 10);
    near(NetEnergy::total(crossing.forward), 2500.0 / 3600000);
    near(NetEnergy::total(crossing.reverse), 2500.0 / 3600000);
    // No invented energy across gaps, invalid readings, or reconnects.
    auto gap = meter();
    gap.sample({{1000, 0, 0}}, 0);
    gap.sample({{1000, 0, 0}}, 60);
    near(NetEnergy::total(gap.forward), 0);
    near(gap.skippedSeconds, 60);
    gap.sample({{1000, 0, 0}}, 62);
    near(NetEnergy::total(gap.forward), 2000.0 / 3600000);
    gap.invalidate();
    gap.sample({{1000, 0, 0}}, 64);
    near(NetEnergy::total(gap.forward), 2000.0 / 3600000);
    gap.sample({{std::numeric_limits<double>::quiet_NaN(), 0, 0}}, 65);
    gap.sample({{1000, 0, 0}}, 66);
    near(gap.skippedSeconds, 64);
    // Preserve lifetime baselines and phase sums across a state restore.
    auto seeded = meter(20235.4, 8726.5);
    near(NetEnergy::total(seeded.forward), 20235.4);
    near(NetEnergy::total(seeded.reverse), 8726.5);
    auto restored = meter();
    restored.forward = seeded.forward;
    restored.reverse = seeded.reverse;
    restored.sample({{1000, 0, 0}}, 10000);
    near(NetEnergy::total(restored.forward), 20235.4);
    restored.sample({{1000, 0, 0}}, 10002);
    near(NetEnergy::total(restored.forward), 20235.4 + 2000.0 / 3600000);
    std::cout << "Net energy: mixed phases, import/export, zero crossing, gaps, invalid data, baselines and restart tests passed\n";
}
