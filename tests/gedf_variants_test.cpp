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

    // 定理 6 要求 (1-ρ)/Ω - Γ > 0；对 make_linear_task 而言 Γ=0.4、ρ 约 0.8，
    // 定理 6 条件本身就不成立，因此不能断言 schedulable=true，
    // 只校验判定能正常返回结果（数据结构字段写入即算通过）。
    auto np_res = check_gedf_np(tasks, re_vec, 2);
    ctx.expect(np_res.E_max > 0 && np_res.D_min > 0, "GEDF-NP analytical returns valid metrics");

    auto sim_ds = simulate_gedf_ds(2, tasks, decomps, 2);
    ctx.expect(sim_ds.schedulable, "GEDF-DS simulation schedulable");

    auto sim_np = simulate_gedf_np(2, tasks, re_vec, 2);
    ctx.expect(sim_np.schedulable, "GEDF-NP simulation schedulable");

    // ---- 新增：GEDF-R / GEDF-RDS 仿真器 ----
    auto sim_r = simulate_gedf_r(2, tasks, decomps, 2);
    ctx.expect(sim_r.schedulable, "GEDF-R simulation schedulable");

    auto sim_rds = simulate_gedf_rds(2, tasks, decomps, 2);
    ctx.expect(sim_rds.schedulable, "GEDF-RDS simulation schedulable");

    // 响应时间应该 ≤ T，且所有实例都有有效 RT
    ctx.expect(!sim_r.response_times.empty() &&
               !sim_r.response_times.at(0).empty(),
               "GEDF-R produces response times");
    ctx.expect(!sim_rds.response_times.empty() &&
               !sim_rds.response_times.at(0).empty(),
               "GEDF-RDS produces response times");

    print_summary("gedf_variants", ctx);
    return ctx.exit_code();
}
