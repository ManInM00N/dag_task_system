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

// ======================== 精度评估 ========================
struct PrecisionResult {
    int task_id;
    double wcrt_sim;          // 仿真得到的最坏响应时间
    double rt_bound_theorem;  // 理论响应时间上界
    double precision;         // wcrt_sim / rt_bound，越接近 1 越紧
    double period;
};

// 计算理论响应时间上界：
//  Bound = Ω_i * T_i（源自 ℓ̂_i ≤ Ω_i * U_i 与截止结构）；
//  分解后响应时间上界不超过 T_i，此处利用分解参数给出更紧的界。
std::vector<PrecisionResult> evaluate_precision(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    const SimResult &sim_result,
    int m);
