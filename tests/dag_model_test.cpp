#include "test_utils.h"

int main() {
    TestCtx ctx;
    auto t = make_linear_task();

    ctx.expect_near(t.C, 4.0, 1e-9, "C sum");
    ctx.expect_near(t.L, 4.0, 1e-9, "critical path L");
    ctx.expect_near(t.U, 0.4, 1e-9, "util U");
    ctx.expect_near(t.elasticity, 0.4, 1e-9, "elasticity Γ");
    ctx.expect_near(t.laxity, 6.0, 1e-9, "laxity");
    ctx.expect(t.topological_sort() == std::vector<int>({1,2,3}), "topological order");
    ctx.expect_near(t.vertices[1].rdy, 0.0, 1e-9, "v1 rdy");
    ctx.expect_near(t.vertices[2].rdy, 1.0, 1e-9, "v2 rdy");
    ctx.expect_near(t.vertices[3].rdy, 3.0, 1e-9, "v3 rdy");
    ctx.expect_near(t.vertices[1].fsh, 1.0, 1e-9, "v1 fsh");
    ctx.expect_near(t.vertices[2].fsh, 3.0, 1e-9, "v2 fsh");
    ctx.expect_near(t.vertices[3].fsh, 4.0, 1e-9, "v3 fsh");

    print_summary("dag_model", ctx);
    return ctx.exit_code();
}
