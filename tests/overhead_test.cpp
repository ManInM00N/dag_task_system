#include "test_utils.h"
#include "decomposition.h"
#include "gedf_variants.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();
    auto dec = decompose_dag(t);
    OverheadConfig cfg; cfg.O_loc = 0.001; cfg.O_mig = 0.01;

    auto with_ov = apply_overhead({dec}, cfg, SchedulerType::GEDF);
    double wcet_before = 0, wcet_after = 0;
    for (auto &st : dec.sporadic_tasks) wcet_before += st.wcet;
    for (auto &st : with_ov[0].sporadic_tasks) wcet_after += st.wcet;
    ctx.expect(wcet_after > wcet_before, "overhead increases wcet");

    // ---- 验证 apply_overhead_sync 同步更新 DAG 顶点 WCET / C / L ----
    // 原始 linear: C=4, L=4。同步后 C 必然增加（每个切片额外叠加开销），
    // compute_parameters() 会刷新 C/L/U/Γ，关键路径长度 L 也应 ≥ 原值。
    double C_before = t.C, L_before = t.L;
    auto sync = apply_overhead_sync({t}, {dec}, cfg, SchedulerType::GEDF);
    ctx.expect(sync.tasks.size() == 1 && sync.decomps.size() == 1,
               "sync returns paired tasks/decomps");
    ctx.expect(sync.tasks[0].C > C_before + 1e-9,
               "sync updates DAG.C after overhead");
    ctx.expect(sync.tasks[0].L >= L_before - 1e-9,
               "sync preserves or increases DAG.L");
    // U 也应随 C 变化
    double U_new = (sync.tasks[0].period > 1e-9)
                       ? sync.tasks[0].C / sync.tasks[0].period : 0;
    ctx.expect(std::fabs(sync.tasks[0].U - U_new) < 1e-9,
               "sync updates DAG.U consistently");

    print_summary("overhead", ctx);
    return ctx.exit_code();
}
