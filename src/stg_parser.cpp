#include "stg_parser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

// ================================================================
//  load_stg_file: 解析单个 .stg 文件
//
//  STG 格式 (无通信开销):
//    行1: N (总节点数，含 dummy)
//    行2: 0  0  0            ← 入口 dummy (id=0, wcet=0, 0个前驱)
//    行3: 1  20  1  0        ← id=1, wcet=20, 1个前驱: 节点0
//    ...
//    最后: N  0  k  p1 p2... ← 出口 dummy
//    '#'开头为注释行
// ================================================================
DAGTask load_stg_file(const std::string &filepath, int task_id) {
    DAGTask task;
    task.task_id = task_id;
    task.period = 0.0;  // 待后续分配

    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open STG file: " << filepath << std::endl;
        return task;
    }

    std::string line;
    int num_tasks = -1;

    while (std::getline(ifs, line)) {
        // 跳过注释行和空行
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);

        // 第一个有效行: 顶点数
        if (num_tasks < 0) {
            iss >> num_tasks;
            continue;
        }

        // 后续行: task_id  wcet  num_preds  [pred1  pred2  ...]
        int vid, num_preds;
        double wcet;
        if (!(iss >> vid >> wcet >> num_preds)) continue;

        // 跳过 dummy 入口/出口节点 (wcet=0)
        // 但保留它们的边关系
        // 策略: 只创建 wcet > 0 的顶点作为真实顶点
        //       dummy 的边会被"穿透"连接

        std::vector<int> preds;
        for (int j = 0; j < num_preds; ++j) {
            int pred;
            iss >> pred;
            preds.push_back(pred);
        }

        if (wcet > EPS) {
            // 真实顶点
            task.add_vertex(vid, wcet);
            for (int p : preds) {
                // 如果前驱是真实顶点，直接加边
                if (task.vertices.count(p)) {
                    task.add_edge(p, vid);
                }
                // 如果前驱是 dummy(wcet=0)，不加边（vid 成为源节点）
            }
        } else {
            // Dummy 出口节点: 记录它的前驱，用于后续确认汇聚
            // （不创建顶点，但如果它有前驱的前驱是真实的，需要处理）
            // 对于 dummy 出口，无需特殊处理——真实顶点之间的边已建好
        }
    }

    // 有些 STG 的边是通过 dummy 中转的:
    //   真实节点A → dummy → 真实节点B
    // 这种情况下 A→B 的边不会被上面的逻辑捕获。
    // 但 STG 标准格式中 dummy 只出现在入口(id=0)和出口(id=N+1)，
    // 真实节点之间的前驱关系直接记录，不经 dummy 中转。
    // 所以上述逻辑对标准 STG 是正确的。

    if (task.vertices.empty()) {
        std::cerr << "Warning: No real vertices loaded from " << filepath << std::endl;
    }

    return task;
}

// ================================================================
//  assign_period: 按论文公式为 DAG 分配周期
//
//  公式 (30): T = (L + C/(k·U_norm)) × (1 + 0.25·Gamma(2,1))
//  k=3  → 正常弹性 (Γ = L/T 较低)
//  k=60 → 高弹性   (Γ 接近 1)
//
//  保证 T ≥ L + 1（可调度必要条件 L ≤ T）
// ================================================================
void assign_period(DAGTask &task, double util_norm,
                   unsigned seed, double k_factor) {
    task.compute_parameters();

    if (task.vertices.empty() || task.C < EPS) return;

    std::mt19937 rng(seed);
    std::gamma_distribution<double> gamma(2.0, 1.0);
    double g = gamma(rng);

    double T;
    if (util_norm > EPS) {
        T = (task.L + task.C / (k_factor * util_norm)) * (1.0 + 0.25 * g);
    } else {
        T = task.L * 2.0;
    }

    // 保证 T > L（可调度必要条件）
    T = std::max(T, task.L + 1.0);

    task.period = T;
    task.compute_parameters();
}

// ================================================================
//  load_stg_directory: 从目录批量加载 .stg 文件
// ================================================================
std::vector<DAGTask> load_stg_directory(const std::string &dirpath,
                                         double util_norm,
                                         int max_files,
                                         unsigned seed) {
    std::vector<DAGTask> tasks;
    std::vector<std::string> stg_files;

    // 收集 .stg 文件
    if (!fs::exists(dirpath) || !fs::is_directory(dirpath)) {
        std::cerr << "STG directory not found: " << dirpath << std::endl;
        return tasks;
    }

    for (auto &entry : fs::directory_iterator(dirpath)) {
        if (entry.path().extension() == ".stg") {
            stg_files.push_back(entry.path().string());
        }
    }

    std::sort(stg_files.begin(), stg_files.end());

    int count = std::min(max_files, (int)stg_files.size());
    std::mt19937 rng(seed);

    for (int i = 0; i < count; ++i) {
        DAGTask t = load_stg_file(stg_files[i], i);
        if (t.vertices.empty()) continue;
        assign_period(t, util_norm, rng(), 3.0);
        tasks.push_back(t);
    }

    return tasks;
}

// ================================================================
//  load_stg_by_size: 按规模加载 STG
//  base_dir 下应有 rnc{size}/ 子目录
// ================================================================
std::vector<DAGTask> load_stg_by_size(const std::string &base_dir,
                                       int size,
                                       double util_norm,
                                       int max_files,
                                       unsigned seed) {
    std::string dir = base_dir + "/rnc" + std::to_string(size);
    return load_stg_directory(dir, util_norm, max_files, seed);
}