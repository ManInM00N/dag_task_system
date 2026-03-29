#include "test_utils.h"
#include "decomposition.h"
#include "gedf_simulator.h"
#include "gedf_variants.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();
    auto dec = decompose_dag(t);
    std::vector<DAGTask> tasks = {t};
    std::vector<DecompositionResult> decomps = {dec};
    auto sim = simulate_gedf(2, tasks, decomps, 2);
    auto prec = evaluate_precision(tasks, decomps, sim, 2);
    ctx.expect(prec.size() == 1, "precision size 1");
    ctx.expect(prec[0].precision > 0 && prec[0].precision <= 1.0 + 1e-9, "precision within bounds");

    print_summary("precision", ctx);
    return ctx.exit_code();
}
