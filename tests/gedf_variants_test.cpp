#include "test_utils.h"
#include "decomposition.h"
#include "gedf_variants.h"
#include "vertex_reassemble.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();
    auto dec = decompose_dag(t);
    std::vector<DAGTask> tasks = {t};
    std::vector<DecompositionResult> decomps = {dec};

    auto reassembled = make_reassembled_decomp(dec);
    std::vector<DecompositionResult> re_vec = {reassembled};

    ctx.expect(check_gedf_ds(tasks, decomps, 2).schedulable, "GEDF-DS analytical schedulable");
    ctx.expect(check_gedf_np(tasks, re_vec, 2).schedulable, "GEDF-NP analytical schedulable");

    auto sim_ds = simulate_gedf_ds(2, tasks, decomps, 2);
    ctx.expect(sim_ds.schedulable, "GEDF-DS simulation schedulable");

    auto sim_np = simulate_gedf_np(2, tasks, re_vec, 2);
    ctx.expect(sim_np.schedulable, "GEDF-NP simulation schedulable");

    print_summary("gedf_variants", ctx);
    return ctx.exit_code();
}
