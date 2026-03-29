#include "test_utils.h"
#include "decomposition.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();
    auto dec = decompose_dag(t);

    ctx.expect(dec.segments.size() == 3, "segments size");
    ctx.expect(dec.sporadic_tasks.size() == 3, "sporadic size");

    double wcet_sum = 0;
    for (auto &st : dec.sporadic_tasks) wcet_sum += st.wcet;
    ctx.expect_near(wcet_sum, t.C, 1e-9, "sporadic wcet sum");

    double d_sum = 0;
    for (auto &s : dec.segments) d_sum += s.d;
    ctx.expect_near(d_sum, t.period, 1e-6, "segments d sum == period");

    ctx.expect(dec.omega >= 0.9 && dec.omega <= 1.1, "omega approx 1");
    ctx.expect(dec.delta_hat <= 0.6, "delta_hat reasonable");
    ctx.expect(dec.ell_hat <= 0.6, "ell_hat reasonable");

    print_summary("decomposition", ctx);
    return ctx.exit_code();
}
