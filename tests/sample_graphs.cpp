#include <iostream>
#include <vector>
#include "dag_generators.h"
#include "json.hpp"

// 生成三个可复现实例，输出 JSON：
//  case1: n=4, seed=101
//  case2: n=7, seed=202
//  case3: n=10, seed=303
//  m 仅作为标注，不影响 DAG 结构。
int main() {
    struct Case { int n; int m; unsigned seed; } cases[] = {
        {4, 3, 101},
        {7, 10, 202},
        {10, 13, 303},
    };

    nlohmann::json out;
    int idx = 1;
    for (auto &c : cases) {
        // p=0.3 增加连边密度，wcet 1..5，归一化利用率 0.4
        auto task = generate_erdos_renyi_dag(idx, c.n, 0.3, 1.0, 5.0, 0.4, c.seed);
        nlohmann::json jt;
        jt["n_vertices"] = c.n;
        jt["m"] = c.m;
        jt["seed"] = c.seed;
        jt["C"] = task.C;
        jt["L"] = task.L;
        jt["T"] = task.period;
        jt["U"] = task.U;
        jt["elasticity"] = task.elasticity;

        for (auto &[vid, v] : task.vertices) {
            jt["vertices"][std::to_string(vid)] = {
                {"wcet", v.wcet}, {"preds", v.preds}, {"succs", v.succs}
            };
        }

        // 拓扑顺序方便查看
        jt["topo"] = task.topo();
        out["case" + std::to_string(idx)] = jt;
        ++idx;
    }

    std::cout << out.dump(2) << std::endl;
    return 0;
}
