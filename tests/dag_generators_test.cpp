#include "test_utils.h"
#include "dag_generators.h"

int main() {
    TestCtx ctx;

    auto paper = create_paper_example();
    ctx.expect(paper.vertices.size() == 8, "paper example vertex count");
    ctx.expect_near(paper.period, 25.0, 1e-9, "paper example period");

    auto set = generate_taskset(3, /*m=*/4, 5, 8, 0.2, 0.4, 1.0, 3.0, 42);
    ctx.expect(set.size() == 3, "taskset size 3");
    for (size_t i = 0; i < set.size(); ++i) {
        ctx.expect(set[i].task_id == (int)i, "task id increments");
        ctx.expect(set[i].vertices.size() >= 5 && set[i].vertices.size() <= 8,
                   "vertex count within bounds");
        ctx.expect(set[i].period > set[i].L, "period > L");
    }

    print_summary("dag_generators", ctx);
    return ctx.exit_code();
}
