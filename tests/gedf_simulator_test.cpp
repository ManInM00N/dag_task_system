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

    // ==== 验证 DAG 依赖：菱形 DAG 1 -> {2,3} -> 4 ====
    // wcet: v1=2, v2=3, v3=1, v4=2; C=8, L=max(2+3+2, 2+1+2)=7, T=20。
    // 在 m=1 的条件下由于 DAG 依赖，v4 开始必须晚于 v2 和 v3 完成；
    // 总执行顺序只能是严格串行，因此 WCRT 不应超过 T，且三周期响应时间都相同。
    {
        DAGTask d; d.task_id = 7; d.period = 20.0;
        d.add_vertex(1, 2.0);
        d.add_vertex(2, 3.0);
        d.add_vertex(3, 1.0);
        d.add_vertex(4, 2.0);
        d.add_edge(1, 2);
        d.add_edge(1, 3);
        d.add_edge(2, 4);
        d.add_edge(3, 4);
        d.compute_parameters();
        auto dec2 = decompose_dag(d);
        std::vector<DAGTask> ts = {d};
        std::vector<DecompositionResult> decs = {dec2};

        auto s = simulate_gedf(1, ts, decs, 2);
        ctx.expect(s.schedulable, "diamond DAG schedulable on m=1");
        ctx.expect(s.response_times.count(7) && s.response_times[7].size() == 2,
                   "diamond DAG: 2 instances simulated");
        // 在 m=1 上严格串行执行，整个 DAG 做完需要 C=8 单位；
        // WCRT 应 >= C-ε 且 <= T。
        ctx.expect(s.wcrt[7] + 1e-6 >= d.C && s.wcrt[7] <= d.period + 1e-6,
                   "diamond DAG: WCRT in [C, T]");
    }

    print_summary("gedf_simulator", ctx);
    return ctx.exit_code();
}
