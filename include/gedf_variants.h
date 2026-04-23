#pragma once
#include "dag_model.h"
#include "gedf_simulator.h"
#include <vector>
#include <map>

// ======================== Analytical Tests for Variants ========================

// 定理 6：GEDF-NP 判定
// 条件：(1-ρ)/Ω_⊤ - Γ_⊤ > 0 且 m ≥ (U_Σ - Γ_⊤)/((1-ρ)/Ω_⊤ - Γ_⊤)，ρ=E_max/D_min
struct GEDFNP_Result {
    double rho;          // E_max / D_min
    double E_max;
    double D_min;
    double m_required;
    bool   schedulable;
};

GEDFNP_Result check_gedf_np(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m);

// 定理 7：GEDF-DS 判定
//  δ_⊤ ≤ 1/2 同 GEDF；否则要求 U_Σ ≤ (m+1)/(2Ω_⊤)
struct GEDFDS_Result {
    double delta_top;
    bool   schedulable;
    double m_required;    // only meaningful when δ_⊤ ≤ 1/2
    double U_bound;       // (m+1)/(2Ω_⊤) for case 2
};

GEDFDS_Result check_gedf_ds(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m);

// 定理 8：GEDF-R 与 GEDF 条件一致，仅限制迁移
AnalyticalResult check_gedf_r(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m);

// ======================== 考虑开销的测试 ========================

struct OverheadConfig {
    double O_loc = 0.0008;   // 本地上下文切换：约 800ns ≈ 0.0008 ms
    double O_mig = 0.1;      // 迁移开销：约 100μs ≈ 0.1 ms
};

// 将开销叠加到零散任务上，返回修改后的分解结果
// GEDF/GEDF-DS：首片 O_mig + 2*O_loc，非首片 2*O_mig + 2*O_loc
// GEDF-R/GEDF-RDS：首片 2*O_loc，非首片 2*O_loc + O_mig
enum class SchedulerType { GEDF, GEDF_DS, GEDF_R, GEDF_RDS, GEDF_NP };

std::vector<DecompositionResult> apply_overhead(
    const std::vector<DecompositionResult> &decomps,
    const OverheadConfig &cfg,
    SchedulerType sched_type);

// 同步版本：在叠加开销的同时，把同一顶点所有切片新增的开销汇总到对应 DAG
// 顶点的 WCET 上，并调用 compute_parameters() 让 C、L、U、Γ、rdy/fsh 全部一致更新。
// 这样后续的理论判定 (check_schedulability_gedf / check_gedf_ds / ...) 才会用
// 到“含开销”的 DAG 参数，避免过乐观。
//
// 输入：tasks / decomps 一一对应（task_id 相同）；输出：同尺寸的 pair。
struct OverheadSyncResult {
    std::vector<DAGTask> tasks;
    std::vector<DecompositionResult> decomps;
};

OverheadSyncResult apply_overhead_sync(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    const OverheadConfig &cfg,
    SchedulerType sched_type);

// ======================== GEDF-DS  ========================
// 密度分离：重任务（密度 > 1/2）最高优先级，占最多 m-1 个核
SimResult simulate_gedf_ds(int m,
                            std::vector<DAGTask> &tasks,
                            std::vector<DecompositionResult> &decomps,
                            int num_periods);

// ======================== GEDF-NP  ========================
// 非抢占：任务一旦开始便运行至完成
SimResult simulate_gedf_np(int m,
                            std::vector<DAGTask> &tasks,
                            std::vector<DecompositionResult> &decomps,
                            int num_periods);

// ======================== GEDF-R  ========================
// 受限迁移：同一 DAG 顶点的所有切片被绑定到同一个核上（顶点内部可抢占，跨顶点可迁移）。
// 分配策略：Worst-Fit，按累积负载把顶点依次放入当前负载最低的核，减轻负载倾斜。
// 同样保持 DAG 依赖。
SimResult simulate_gedf_r(int m,
                           std::vector<DAGTask> &tasks,
                           std::vector<DecompositionResult> &decomps,
                           int num_periods);

// ======================== GEDF-RDS  ========================
// 受限迁移 + 密度分离：在 GEDF-R 的基础上，重任务（density > 0.5）
// 在 ready 队列内获得最高优先级。
SimResult simulate_gedf_rds(int m,
                             std::vector<DAGTask> &tasks,
                             std::vector<DecompositionResult> &decomps,
                             int num_periods);

// ======================== 精度评估 ========================
// 精度 = WCRT_sim / RT_bound
//  取值范围 (0, 1]，越接近 1 表示理论分析越紧；
//  若 > 1 说明仿真出现超过理论上界的响应时间（理论不安全，需检查）。
// 我们同时提供三种常见 RT 上界：
//   (1) Period  :  T_i  —— 最宽松的上界（可调度 ⇒ RT ≤ T）
//   (2) Omega*T :  Ω_i · T_i  —— 基于分解结构特征值的紧界
//   (3) Graham  :  L_i + (C_i - L_i)/m  —— 经典 Graham 型并行上界
// 默认 precision 字段沿用 Omega*T 这个最有意义的紧界。
struct PrecisionResult {
    int task_id;
    double wcrt_sim;          // 仿真得到的最坏响应时间
    double period;            // T_i

    // 三种理论上界
    double rt_bound_period;   // = T_i
    double rt_bound_omega;    // = Ω_i * T_i
    double rt_bound_graham;   // = L_i + (C_i - L_i) / m

    // 对应精度 = WCRT_sim / bound
    double precision_period;
    double precision_omega;
    double precision_graham;

    // 兼容旧字段（= 基于 Ω·T 的紧界版本）
    double rt_bound_theorem;  // = rt_bound_omega
    double precision;         // = precision_omega
};

// 计算理论响应时间上界：
//   period  : RT ≤ T_i（可调度性的最弱保证）
//   omega*T : RT ≤ Ω_i · T_i（基于分解结构特征值）
//   graham  : RT ≤ L_i + (C_i − L_i)/m（Graham 型并行上界）
// 默认 precision = WCRT_sim / (Ω_i · T_i)，其余以独立字段一并输出，便于实验分析。
std::vector<PrecisionResult> evaluate_precision(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    const SimResult &sim_result,
    int m);
