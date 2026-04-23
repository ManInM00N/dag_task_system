#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <cassert>
#include <set>

constexpr double EPS = 1e-9;

// ======================== Vertex ========================
// DAG 顶点：记录 wcet 及前驱/后继、就绪/完成时间窗口
struct Vertex {
    int vid;
    double wcet;               // c(v)
    std::vector<int> preds;    // predecessor vertex ids
    std::vector<int> succs;    // successor vertex ids
    double rdy = 0.0;          // earliest ready time
    double fsh = 0.0;          // latest finish time
};

// ======================== DAG Task ========================
// DAG 任务：存储顶点集合与周期/关键路径/利用率等派生参数
struct DAGTask {
    int task_id;
    std::unordered_map<int, Vertex> vertices;
    double period  = 0.0;       // T_i
    double C       = 0.0;       // total execution time
    double L       = 0.0;       // critical path length
    double U       = 0.0;       // utilization  C/T
    double elasticity = 0.0;    // Γ_i = L/T
    double laxity  = 0.0;       // T - L

    void add_vertex(int vid, double wcet) {
        vertices[vid] = Vertex{vid, wcet, {}, {}, 0.0, 0.0};
    }

    void add_edge(int src, int dst) {
        vertices[src].succs.push_back(dst);
        vertices[dst].preds.push_back(src);
    }

    // Kahn 拓扑排序
    std::vector<int> topo() const {
        std::unordered_map<int, int> in_deg;
        for (auto &[vid, v] : vertices) in_deg[vid] = (int)v.preds.size();

        std::vector<int> queue, result;
        for (auto &[vid, d] : in_deg) if (d == 0) queue.push_back(vid);

        while (!queue.empty()) {
            std::sort(queue.begin(), queue.end());
            int vid = queue.front();
            queue.erase(queue.begin());
            result.push_back(vid);
            for (int s : vertices.at(vid).succs) {
                if (--in_deg[s] == 0) queue.push_back(s);
            }
        }
        return result;
    }

    // 计算派生参数：C、L、各顶点 rdy/fsh、U、弹性 Γ、松弛度
    void compute_parameters() {
        C = 0.0;
        for (auto &[vid, v] : vertices) C += v.wcet;
        if (period > EPS) U = C / period; else U = 0.0;

        auto topo = this->topo();

        // forward pass: rdy
        for (int vid : topo) {
            auto &v = vertices[vid];
            if (v.preds.empty()) { v.rdy = 0.0; continue; }
            double mx = 0.0;
            for (int p : v.preds)
                mx = std::max(mx, vertices[p].rdy + vertices[p].wcet);
            v.rdy = mx;
        }

        // critical path length
        L = 0.0;
        for (auto &[vid, v] : vertices) L = std::max(L, v.rdy + v.wcet);
        laxity = period - L;
        elasticity = (period > EPS) ? L / period : 0.0;

        // backward pass: fsh
        // fsh(v) = 顶点 v 必须完成的最晚时刻
        //   叶子: fsh = L  (隐式截止期下即任务截止期意义下的关键路径长度)
        //   其他: fsh(v) = min_{s ∈ succs(v)} ( fsh(s) - c(s) )
        // 这里保持 fsh ≤ L，便于与 rdy 一同构造分解段边界。
        for (auto it = topo.rbegin(); it != topo.rend(); ++it) {
            auto &v = vertices[*it];
            if (v.succs.empty()) { v.fsh = L; continue; }
            double mn = std::numeric_limits<double>::max();
            for (int s : v.succs) {
                const auto &sv = vertices[s];
                mn = std::min(mn, sv.fsh - sv.wcet);
            }
            v.fsh = mn;
        }
    }
};

// ======================== Segment ========================
// 分段结果：在顶点时间窗口交叠处形成的区间，存放分配的顶点切片
struct Segment {
    int seg_id;
    double start;   // b_x
    double end;      // b_{x+1}
    double e;        // length = end - start
    double c = 0.0;  // total workload
    double d = 0.0;  // length after laxity distribution
    bool is_heavy = false;

    // (vertex_id, wcet_part)
    std::vector<std::pair<int,double>> assigned;

    double ratio()  const { return (e > EPS) ? c / e : 1e18; }
    double load()   const { return (d > EPS) ? c / d : 1e18; }
    double delta()  const { return (d > EPS) ? e / d : 1e18; }
};

// ======================== Sporadic Task ========================
// 分解后得到的零散任务：继承父 DAG 顶点的片段
struct SporadicTask {
    std::string task_id;
    int parent_task_id;
    int vertex_id;
    int segment_id;
    double wcet;
    double period;
    double release_offset;
    double deadline_offset;
    double relative_deadline;
    double density;
};

// ======================== Decomposition Result ========================
// 单个 DAG 的完整分解产物
struct DecompositionResult {
    int task_id;
    std::vector<Segment> segments;
    std::vector<SporadicTask> sporadic_tasks;
    double omega  = 0.0;   // Ω_i
    double C_H    = 0.0;
    double L_L    = 0.0;
    double delta_hat = 0.0;
    double ell_hat   = 0.0;
};
