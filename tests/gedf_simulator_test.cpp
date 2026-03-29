#include "test_utils.h"
#include "decomposition.h"
#include "gedf_simulator.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();
    auto dec = decompose_dag(t);
    std::vector<DAGTask> tasks = {t};
    std::vector<DecompositionResult> decomps = {dec};

    auto ana = check_schedulability_gedf(tasks, decomps, 2);
    ctx.expect(ana.schedulable, "GEDF analytical schedulable");

    auto sim = simulate_gedf(2, tasks, decomps, 3);
    ctx.expect(sim.schedulable, "GEDF simulation schedulable");
    ctx.expect(sim.deadline_misses == 0, "no deadline miss");
    ctx.expect(sim.response_times[0].size() == 3, "three instances simulated");
    ctx.expect(sim.wcrt[0] <= t.period + 1e-6, "wcrt within period");

    print_summary("gedf_simulator", ctx);
    return ctx.exit_code();
}
