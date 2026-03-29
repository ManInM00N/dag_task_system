#include "dag_generators.h"
#include <random>
#include <cmath>
#include <algorithm>

DAGTask create_paper_example() {
    // 固定图例：8 个顶点 + 指定边，周期 25
    DAGTask task;
    task.task_id = 0;
    task.period  = 25.0;

    int verts[][2] = {{1,1},{2,3},{3,4},{4,8},{5,5},{6,4},{7,4},{8,1}};
    for (auto &vv : verts) task.add_vertex(vv[0], vv[1]);

    int edges[][2] = {{1,2},{1,3},{1,4},{3,5},{5,6},{4,7},{6,8},{7,8}};
    for (auto &ee : edges) task.add_edge(ee[0], ee[1]);

    return task;
}

DAGTask generate_erdos_renyi_dag(int task_id, int n_vertices, double p,
                                  double wcet_min, double wcet_max,
                                  double util_norm, unsigned seed) {
    // 按 G(n,p) 随机建边，WCET 服从均匀分布
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> wcet_dist(wcet_min, wcet_max);
    std::uniform_real_distribution<double> edge_dist(0.0, 1.0);

    DAGTask task;
    task.task_id = task_id;

    for (int i = 1; i <= n_vertices; ++i)
        task.add_vertex(i, wcet_dist(rng));

    for (int i = 1; i <= n_vertices; ++i)
        for (int j = i + 1; j <= n_vertices; ++j)
            if (edge_dist(rng) < p)
                task.add_edge(i, j);

    // 先设周期为 1，计算关键路径 L，后续再推导真实周期
    task.period = 1.0;
    task.compute_parameters();

    // 周期公式 (式 30)：T = (L + C/(3·U_norm)) * (1 + 0.25·Gamma(2,1))
    std::gamma_distribution<double> gamma(2.0, 1.0);
    double g = gamma(rng);
    double T = (util_norm > EPS)
               ? (task.L + task.C / (3.0 * util_norm)) * (1.0 + 0.25 * g)
               : task.L * 2.0;
    T = std::max(T, task.L + 1.0);

    task.period = T;
    task.compute_parameters();
    return task;
}

std::vector<DAGTask> generate_taskset(int n_tasks, int m,
                                       int nv_min, int nv_max,
                                       double p, double util_norm,
                                       double wcet_min, double wcet_max,
                                       unsigned seed) {
    // 生成 n_tasks 个 DAG，每个顶点数随机，复用 generate_erdos_renyi_dag
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> nv_dist(nv_min, nv_max);
    std::vector<DAGTask> tasks;
    for (int i = 0; i < n_tasks; ++i) {
        int nv = nv_dist(rng);
        unsigned sub_seed = rng();
        tasks.push_back(generate_erdos_renyi_dag(
            i, nv, p, wcet_min, wcet_max, util_norm, sub_seed));
    }
    return tasks;
}
