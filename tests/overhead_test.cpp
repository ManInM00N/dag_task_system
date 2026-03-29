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

    print_summary("overhead", ctx);
    return ctx.exit_code();
}
