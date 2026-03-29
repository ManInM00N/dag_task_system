#pragma once
#include "dag_model.h"
#include <map>
#include <string>
#include <vector>

// 追踪单次 DAG 实例的释放/截止及完成情况
struct DAGInstance {
    int task_id;
    int instance;
    double release_time;
    double deadline;
    std::vector<std::string> job_ids;
    double last_finish = 0.0;
    double response_time = 0.0;
};

// 仿真结果：WCRT/平均响应时间、是否违例
struct SimResult {
    bool schedulable = true;
    // task_id -> list of per-instance response times
    std::map<int, std::vector<double>> response_times;
    // task_id -> worst-case response time
    std::map<int, double> wcrt;
    std::map<int, double> avg_rt;
    int deadline_misses = 0;
};

// 理论可调度性判定（定理 3）
struct AnalyticalResult {
    double omega_top;
    double gamma_top;
    double U_sum;
    double denom;      // 1/Ω_⊤ - Γ_⊤
    double m_required;
    bool   schedulable;
    double cap_aug_bound;
};

// 运行 GEDF 仿真，返回每个任务的响应时间序列
SimResult simulate_gedf(int m,
                         std::vector<DAGTask> &tasks,
                         std::vector<DecompositionResult> &decomps,
                         int num_periods);

// GEDF 理论判定（基于 Ω 和弹性 Γ）
AnalyticalResult check_schedulability_gedf(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m);
