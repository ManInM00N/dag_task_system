#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include "dag_generators.h"
#include "json.hpp"

// CLI 用法：
//   gen_tool <out.json> <n_vertices> <p> <wcet_min> <wcet_max> <util_norm> <seed> [task_id]
// 说明：
//   - 使用 Erdős–Rényi G(n,p) 随机生成 DAG，WCET 在 [wcet_min, wcet_max] 均匀分布。
//   - util_norm 控制周期与总工作量的比值（越大周期越短，利用率越高）。
//   - seed 确保可复现；task_id 可选，默认为 0。
// 输出 JSON 结构（单任务）：
//   {
//     "task": {
//       "id": ..., "C": ..., "L": ..., "T": ..., "U": ..., "elasticity": ...,
//       "vertices": {
//          "vid": {"wcet": x, "preds": [...], "succs": [...]}, ...
//       }
//     }
//   }
// 其他函数可直接加载该 JSON，取用 task 信息与图结构。

int main(int argc, char* argv[]) {
    if (argc < 8) {
        std::cerr << "Usage: gen_tool <out.json> <n> <p> <wcet_min> <wcet_max> <util_norm> <seed> [task_id]\n";
        return 1;
    }

    const std::string out_path = argv[1];
    int n = std::atoi(argv[2]);
    double p = std::atof(argv[3]);
    double wcet_min = std::atof(argv[4]);
    double wcet_max = std::atof(argv[5]);
    double util_norm = std::atof(argv[6]);
    unsigned seed = static_cast<unsigned>(std::strtoul(argv[7], nullptr, 10));
    int task_id = (argc >= 9) ? std::atoi(argv[8]) : 0;

    if (n <= 0 || p < 0 || p > 1 || wcet_min <= 0 || wcet_max < wcet_min || util_norm < 0) {
        std::cerr << "Invalid arguments.\n";
        return 1;
    }

    auto task = generate_erdos_renyi_dag(task_id, n, p, wcet_min, wcet_max, util_norm, seed);

    nlohmann::json j;
    j["task"]["id"] = task.task_id;
    j["task"]["C"] = task.C;
    j["task"]["L"] = task.L;
    j["task"]["T"] = task.period;
    j["task"]["U"] = task.U;
    j["task"]["elasticity"] = task.elasticity;

    for (auto &[vid, v] : task.vertices) {
        j["task"]["vertices"][std::to_string(vid)] = {
            {"wcet", v.wcet}, {"preds", v.preds}, {"succs", v.succs}
        };
    }

    std::ofstream ofs(out_path);
    if (!ofs) {
        std::cerr << "Cannot open output file: " << out_path << "\n";
        return 1;
    }
    ofs << j.dump(2) << std::endl;
    ofs.close();

    std::cout << "Written DAG to " << out_path << " (n=" << n << ", seed=" << seed << ")\n";
    return 0;
}
