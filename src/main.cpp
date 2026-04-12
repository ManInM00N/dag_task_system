/*
 * DAG 分解与实时调度仿真器
 *
 * 主流程速览：
 *   1) 准备 DAG 任务：固定样例或随机生成。
 *   2) 运行分解算法：生成段、分配顶点，计算 Ω/松弛度，转化为周期性零散任务。
 *   3) 执行 GEDF 及其变体的可调度性分析与仿真。
 *   4) 汇总结果并写出 JSON（供绘图和后续分析）。
 */

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <random>
#include <filesystem>

#include "dag_model.h"
#include "decomposition.h"
#include "gedf_simulator.h"
#include "gedf_variants.h"
#include "vertex_reassemble.h"
#include "dag_generators.h"
#include "json.hpp"
#include "stg_parser.h"
#include "wfcommons_parser.h"

using std::cout;
using std::endl;

// =====================================================================
//  Part 1: 论文示例（Fig.1）
// =====================================================================
static nlohmann::json run_paper_example() {
    cout << std::string(70, '=') << endl;
    cout << "PART 1: Paper Example DAG (Fig.1)" << endl;
    cout << std::string(70, '=') << endl;

    DAGTask task = create_paper_example();
    task.compute_parameters();

    printf("\n--- DAG Task Parameters ---\n");
    printf("  C = %.0f, T = %.0f, L = %.0f\n", task.C, task.period, task.L);
    printf("  U = %.4f, Γ = %.4f, Laxity = %.0f, C/L = %.4f\n",
           task.U, task.elasticity, task.laxity, task.C / task.L);

    printf("\n--- Vertex Timing Diagram ---\n");
    auto topo = task.topo();
    for (int vid : topo) {
        auto &v = task.vertices[vid];
        printf("  v%d: wcet=%.0f, rdy=%.1f, fsh=%.1f, window=[%.1f, %.1f]\n",
               vid, v.wcet, v.rdy, v.fsh, v.rdy, v.fsh);
    }

    // 分解 DAG：生成段、分配顶点切片
    auto result = decompose_dag(task);
    double threshold = task.C / task.L;

    printf("\n--- Segments After Algorithm 1 ---\n");
    printf("  Threshold (C/L) = %.4f\n", threshold);
    for (auto &seg : result.segments) {
        printf("  s%d: [%.1f, %.1f] e=%.1f, c=%.2f, c/e=%.4f, %s, vertices=",
               seg.seg_id, seg.start, seg.end, seg.e, seg.c, seg.ratio(),
               seg.is_heavy ? "HEAVY" : "light");
        printf("[");
        for (int i = 0; i < (int)seg.assigned.size(); ++i) {
            if (i) printf(", ");
            printf("(v%d, %.1f)", seg.assigned[i].first, seg.assigned[i].second);
        }
        printf("]\n");
    }

    printf("\n--- Structure Characteristic Value ---\n");
    printf("  C_H = %.2f, L_L = %.2f\n", result.C_H, result.L_L);
    printf("  Ω = C_H/C + L_L/L = %.4f + %.4f = %.4f\n",
           result.C_H / task.C, result.L_L / task.L, result.omega);

    printf("\n--- Laxity Distribution ---\n");
    for (auto &seg : result.segments)
        printf("  s%d: d=%.4f, ℓ(s)=%.4f, δ(s)=%.4f\n",
               seg.seg_id, seg.d, seg.load(), seg.delta());
    printf("  δ̂ = %.4f, ℓ̂ = %.4f\n", result.delta_hat, result.ell_hat);

    printf("\n--- Sporadic Tasks (%d total) ---\n", (int)result.sporadic_tasks.size());
    for (auto &st : result.sporadic_tasks)
        printf("  %s: wcet=%.2f, rel=%.4f, dl=%.4f, d=%.4f, δ=%.4f\n",
               st.task_id.c_str(), st.wcet,
               st.release_offset, st.deadline_offset,
               st.relative_deadline, st.density);

    // GEDF 仿真
    int m = 4;
    printf("\n--- GEDF Simulation (m=%d) ---\n", m);

    auto sched = check_schedulability_gedf({task}, {result}, m);
    printf("  Analytical: schedulable=%s, m_required=%.4f\n",
           sched.schedulable ? "true" : "false", sched.m_required);
    printf("  Capacity augmentation bound: %.4f\n", sched.cap_aug_bound);

    std::vector<DAGTask> tasks_vec = {task};
    std::vector<DecompositionResult> decomps_vec = {result};
    auto sim = simulate_gedf(m, tasks_vec, decomps_vec, 10);

    printf("\n  Simulation: schedulable=%s\n", sim.schedulable ? "true" : "false");
    for (auto &[tid, rts] : sim.response_times) {
        printf("  Task τ%d: WCRT=%.4f, AvgRT=%.4f, T=%.0f, RTs=[",
               tid, sim.wcrt[tid], sim.avg_rt[tid], task.period);
        for (int i = 0; i < (int)rts.size(); ++i) {
            if (i) printf(", ");
            printf("%.2f", rts[i]);
        }
        printf("]\n");
    }
    printf("  Deadline misses: %d\n", sim.deadline_misses);

    nlohmann::json j;
    // task
    j["task"] = {
        {"C", task.C}, {"L", task.L}, {"T", task.period},
        {"U", task.U}, {"elasticity", task.elasticity}, {"laxity", task.laxity}
    };
    for (auto &[vid, v] : task.vertices) {
        j["task"]["vertices"][std::to_string(vid)] = {
            {"wcet", v.wcet}, {"rdy", v.rdy}, {"fsh", v.fsh}
        };
    }

    // decomposition
    j["decomposition"]["omega"] = result.omega;
    j["decomposition"]["C_H"] = result.C_H;
    j["decomposition"]["L_L"] = result.L_L;
    j["decomposition"]["delta_hat"] = result.delta_hat;
    j["decomposition"]["ell_hat"] = result.ell_hat;

    for (auto &seg : result.segments) {
        nlohmann::json js = {
            {"id", seg.seg_id}, {"start", seg.start}, {"end", seg.end},
            {"e", seg.e}, {"c", seg.c}, {"d", seg.d},
            {"is_heavy", seg.is_heavy}, {"load", seg.load()}, {"delta", seg.delta()}
        };
        for (auto &[vid, w] : seg.assigned)
            js["assigned"].push_back({{"v", vid}, {"w", w}});
        j["decomposition"]["segments"].push_back(js);
    }

    for (auto &st : result.sporadic_tasks) {
        j["decomposition"]["sporadic_tasks"].push_back({
            {"id", st.task_id}, {"vertex", st.vertex_id}, {"segment", st.segment_id},
            {"wcet", st.wcet}, {"release_offset", st.release_offset},
            {"deadline_offset", st.deadline_offset},
            {"relative_deadline", st.relative_deadline}, {"density", st.density}
        });
    }

    // simulation
    nlohmann::json jsim;
    jsim["schedulable"] = sim.schedulable;
    jsim["deadline_misses"] = sim.deadline_misses;
    for (auto &[tid, w] : sim.wcrt) jsim["wcrt"][std::to_string(tid)] = w;
    for (auto &[tid, a] : sim.avg_rt) jsim["avg_rt"][std::to_string(tid)] = a;
    for (auto &[tid, rts] : sim.response_times)
        jsim["response_times"][std::to_string(tid)] = rts;
    j["simulation"] = jsim;

    // analytical
    j["analytical"] = {
        {"omega_top", sched.omega_top}, {"gamma_top", sched.gamma_top},
        {"U_sum", sched.U_sum}, {"m_required", sched.m_required},
        {"schedulable", sched.schedulable}, {"cap_aug_bound", sched.cap_aug_bound}
    };

    return j;
}

// =====================================================================
//  Part 2: 随机任务集合
// =====================================================================
static nlohmann::json run_random_experiment() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 2: Random Task Set (n=4, m=8, p=0.1, U/m=0.4)" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;
    auto tasks = generate_taskset(4, m, 50, 100, 0.1, 0.4, 50, 100, 42);

    for (auto &t : tasks)
        printf("  Task τ%d: |V|=%d, C=%.1f, L=%.1f, T=%.1f, U=%.4f, Γ=%.4f\n",
               t.task_id, (int)t.vertices.size(), t.C, t.L, t.period, t.U, t.elasticity);

    auto decomps = decompose_taskset(tasks);

    printf("\n--- Decomposition Results ---\n");
    for (auto &r : decomps)
        printf("  Task τ%d: Ω=%.4f, #segs=%d, #sporadic=%d\n",
               r.task_id, r.omega, (int)r.segments.size(), (int)r.sporadic_tasks.size());

    auto sched = check_schedulability_gedf(tasks, decomps, m);
    printf("\n--- Analytical Test ---\n");
    printf("  1/Ω_⊤ - Γ_⊤ = %.4f, m_required = %.4f, schedulable = %s\n",
           sched.denom, sched.m_required, sched.schedulable ? "true" : "false");

    auto sim = simulate_gedf(m, tasks, decomps, 5);
    printf("\n--- Simulation ---\n");
    printf("  Schedulable: %s, deadline misses: %d\n",
           sim.schedulable ? "true" : "false", sim.deadline_misses);
    for (auto &[tid, w] : sim.wcrt) {
        auto &t = tasks[tid];
        printf("  Task τ%d: WCRT=%.2f, AvgRT=%.2f, T=%.2f, WCRT/T=%.4f\n",
               tid, w, sim.avg_rt[tid], t.period, w / t.period);
    }

    nlohmann::json j;
    for (auto &t : tasks) {
        j["tasks"].push_back({
            {"id", t.task_id}, {"n_vertices", (int)t.vertices.size()},
            {"C", t.C}, {"L", t.L}, {"T", t.period},
            {"U", t.U}, {"elasticity", t.elasticity}
        });
    }
    for (auto &r : decomps) {
        j["decomposition"].push_back({
            {"task_id", r.task_id}, {"omega", r.omega},
            {"n_segments", (int)r.segments.size()},
            {"n_sporadic", (int)r.sporadic_tasks.size()},
            {"C_H", r.C_H}, {"L_L", r.L_L},
            {"delta_hat", r.delta_hat}, {"ell_hat", r.ell_hat}
        });
    }
    j["analytical"] = {
        {"omega_top", sched.omega_top}, {"gamma_top", sched.gamma_top},
        {"U_sum", sched.U_sum}, {"m_required", sched.m_required},
        {"schedulable", sched.schedulable}
    };
    j["simulation"] = {
        {"schedulable", sim.schedulable},
        {"deadline_misses", sim.deadline_misses}
    };
    for (auto &[tid, w] : sim.wcrt) j["simulation"]["wcrt"][std::to_string(tid)] = w;
    for (auto &[tid, a] : sim.avg_rt) j["simulation"]["avg_rt"][std::to_string(tid)] = a;
    return j;
}

// =====================================================================
//  Part 3: 验收率 vs 归一化利用率
// =====================================================================
static nlohmann::json run_acceptance_ratio() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 3: Acceptance Ratio vs Normalized Utilization" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;
    double p = 0.1;
    int n_trials = 50;
    double util_levels[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};

    nlohmann::json arr = nlohmann::json::array();

    for (double unorm : util_levels) {
        int acc_ana = 0, acc_sim = 0, total_valid = 0;

        for (int trial = 0; trial < n_trials; ++trial) {
            unsigned seed = (unsigned)(unorm * 1000) + trial * 31;
            try {
                auto tasks = generate_taskset(4, m, 20, 60, p, unorm, 10, 50, seed);

                bool valid = true;
                double U_total = 0;
                for (auto &t : tasks) {
                    if (t.L > t.period + EPS) { valid = false; break; }
                    U_total += t.U;
                }
                if (!valid || U_total > m + EPS) continue;
                ++total_valid;

                auto decomps = decompose_taskset(tasks);
                auto sched = check_schedulability_gedf(tasks, decomps, m);
                if (sched.schedulable) ++acc_ana;

                auto sim = simulate_gedf(m, tasks, decomps, 3);
                if (sim.schedulable) ++acc_sim;
            } catch (...) { continue; }
        }

        double ra = (total_valid > 0) ? (double)acc_ana / total_valid : 0;
        double rs = (total_valid > 0) ? (double)acc_sim / total_valid : 0;

        printf("  U/m=%.1f: valid=%d, analytical=%d(%.0f%%), simulation=%d(%.0f%%)\n",
               unorm, total_valid, acc_ana, ra*100, acc_sim, rs*100);

        arr.push_back({
            {"util_norm", unorm}, {"total_valid", total_valid},
            {"accepted_analytical", acc_ana}, {"accepted_simulation", acc_sim},
            {"ratio_analytical", ra}, {"ratio_simulation", rs}
        });
    }
    return arr;
}

// =====================================================================
//  Part 4: 验收率 vs 处理器数量
// =====================================================================
static nlohmann::json run_processor_sweep() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 4: Acceptance Ratio vs Number of Processors" << endl;
    cout << std::string(70, '=') << endl;

    double p = 0.1;
    int n_trials = 40;
    int m_vals[] = {2, 4, 6, 8, 12, 16};

    nlohmann::json arr = nlohmann::json::array();

    for (int m : m_vals) {
        int acc_ana = 0, acc_sim = 0, total_valid = 0;

        for (int trial = 0; trial < n_trials; ++trial) {
            unsigned seed = m * 1000 + trial * 37;
            double unorm = 0.3 + 0.3 * ((double)trial / n_trials);
            int ntasks = std::max(2, m / 2);
            try {
                auto tasks = generate_taskset(ntasks, m, 20, 60, p, unorm, 10, 50, seed);

                bool valid = true;
                double U_total = 0;
                for (auto &t : tasks) {
                    if (t.L > t.period + EPS) { valid = false; break; }
                    U_total += t.U;
                }
                if (!valid || U_total > m + EPS) continue;
                ++total_valid;

                auto decomps = decompose_taskset(tasks);
                auto sched = check_schedulability_gedf(tasks, decomps, m);
                if (sched.schedulable) ++acc_ana;

                auto sim = simulate_gedf(m, tasks, decomps, 3);
                if (sim.schedulable) ++acc_sim;
            } catch (...) { continue; }
        }

        double ra = (total_valid > 0) ? (double)acc_ana / total_valid : 0;
        double rs = (total_valid > 0) ? (double)acc_sim / total_valid : 0;

        printf("  m=%d: valid=%d, analytical=%d(%.0f%%), simulation=%d(%.0f%%)\n",
               m, total_valid, acc_ana, ra*100, acc_sim, rs*100);

        arr.push_back({
            {"m", m}, {"total_valid", total_valid},
            {"accepted_analytical", acc_ana}, {"accepted_simulation", acc_sim},
            {"ratio_analytical", ra}, {"ratio_simulation", rs}
        });
    }
    return arr;
}


// =====================================================================
//  Part 5: GEDF 变体比较（NP 含顶点重组）
// =====================================================================
static nlohmann::json run_variants_comparison() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 5: GEDF Variants Comparison (with Vertex Reassembling)" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;
    double p = 0.1;
    int n_trials = 50;
    double util_levels[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};

    nlohmann::json arr = nlohmann::json::array();

    for (double unorm : util_levels) {
        int acc_gedf = 0, acc_np = 0, acc_ds = 0;
        int acc_gedf_sim = 0, acc_np_sim = 0, acc_ds_sim = 0;
        int valid = 0;

        for (int trial = 0; trial < n_trials; ++trial) {
            unsigned seed = (unsigned)(unorm * 10000) + trial * 41;
            try {
                auto tasks = generate_taskset(4, m, 20, 60, p, unorm, 10, 50, seed);
                bool ok = true; double Ut = 0;
                for (auto &t : tasks) { if (t.L > t.period+EPS) ok=false; Ut+=t.U; }
                if (!ok || Ut > m+EPS) continue;
                ++valid;

                auto decomps = decompose_taskset(tasks);

                // For GEDF-NP: use vertex reassembling (Table I: applicable)
                std::vector<DecompositionResult> reassembled;
                for (auto &d : decomps)
                    reassembled.push_back(make_reassembled_decomp(d));

                // 理论判定
                auto sg = check_schedulability_gedf(tasks, decomps, m);
                auto sn = check_gedf_np(tasks, reassembled, m);  // reassembled!
                auto sd = check_gedf_ds(tasks, decomps, m);      // no reassemble for DS

                if (sg.schedulable) ++acc_gedf;
                if (sn.schedulable) ++acc_np;
                if (sd.schedulable) ++acc_ds;

                // 仿真对比
                auto sim_g = simulate_gedf(m, tasks, decomps, 3);
                if (sim_g.schedulable) ++acc_gedf_sim;

                auto sim_np = simulate_gedf_np(m, tasks, reassembled, 3);
                if (sim_np.schedulable) ++acc_np_sim;

                auto sim_ds = simulate_gedf_ds(m, tasks, decomps, 3);
                if (sim_ds.schedulable) ++acc_ds_sim;

            } catch (...) { continue; }
        }

        printf("  U/m=%.1f: valid=%d | GEDF=%d/%d NP=%d/%d DS=%d/%d (ana/sim)\n",
               unorm, valid,
               acc_gedf, acc_gedf_sim,
               acc_np, acc_np_sim,
               acc_ds, acc_ds_sim);

        arr.push_back({
            {"util_norm", unorm}, {"total_valid", valid},
            {"gedf_analytical", acc_gedf}, {"gedf_simulation", acc_gedf_sim},
            {"np_analytical", acc_np}, {"np_simulation", acc_np_sim},
            {"ds_analytical", acc_ds}, {"ds_simulation", acc_ds_sim},
            {"gedf_ratio", valid>0?(double)acc_gedf/valid:0.0},
            {"np_ratio", valid>0?(double)acc_np/valid:0.0},
            {"ds_ratio", valid>0?(double)acc_ds/valid:0.0},
            {"gedf_sim_ratio", valid>0?(double)acc_gedf_sim/valid:0.0},
            {"np_sim_ratio", valid>0?(double)acc_np_sim/valid:0.0},
            {"ds_sim_ratio", valid>0?(double)acc_ds_sim/valid:0.0}
        });
    }
    return arr;
}

// =====================================================================
//  Part 6: 高弹性实验（Fig.8）+ 开销敏感性
// =====================================================================
static DAGTask gen_high_elasticity_dag(int task_id, int nv, double p,
                                        double wmin, double wmax,
                                        double target_util, unsigned seed) {
    // High elasticity: Γ = L/T close to 1
    // Strategy: set T just slightly above L, so Γ ≈ 0.7~0.98
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> wd(wmin, wmax);
    std::uniform_real_distribution<double> ed(0.0, 1.0);
    DAGTask task; task.task_id = task_id;
    for (int i = 1; i <= nv; ++i) task.add_vertex(i, wd(rng));
    // Use higher p for longer critical paths
    double p_hi = std::min(p + 0.15, 0.4);
    for (int i = 1; i <= nv; ++i)
        for (int j = i+1; j <= nv; ++j)
            if (ed(rng) < p_hi) task.add_edge(i, j);
    task.period = 1.0; task.compute_parameters();

    // T = L / target_gamma, where target_gamma is high (0.7~0.95)
    std::uniform_real_distribution<double> gamma_dist(0.7, 0.95);
    double target_gamma = gamma_dist(rng);
    double T = task.L / target_gamma;
    // Ensure U = C/T is reasonable (not too high for the system)
    // If U would be > target_util * 1.5, increase T
    double U_candidate = task.C / T;
    if (U_candidate > target_util * 1.5)
        T = task.C / (target_util * 1.5);
    T = std::max(T, task.L + 0.5);
    task.period = T; task.compute_parameters();
    return task;
}

static nlohmann::json run_high_elasticity_and_overhead() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 6: High-Elasticity (Fig.8) + Overhead (Fig.10)" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;
    double p_edge = 0.1;
    int n_trials = 50;
    double util_levels[] = {0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.35, 0.4, 0.5};

    // --- Part 6a: 高弹性验收率 ---
    printf("\n  --- High Elasticity Acceptance Ratio ---\n");
    nlohmann::json high_arr = nlohmann::json::array();

    for (double unorm : util_levels) {
        int acc_gedf = 0, acc_ds = 0, total_valid = 0;

        for (int trial = 0; trial < n_trials; ++trial) {
            unsigned seed = (unsigned)(unorm * 20000) + trial * 59;
            try {
                std::vector<DAGTask> tasks;
                std::mt19937 rng(seed);
                // Generate tasks with per-task utilization ≈ unorm * m / n_tasks
                int n_tasks = 4;
                double per_task_util = unorm * m / n_tasks;
                for (int i = 0; i < n_tasks; ++i)
                    tasks.push_back(gen_high_elasticity_dag(
                        i, 10 + rng() % 20, p_edge, 10, 50,
                        per_task_util, rng()));

                bool ok = true; double Ut = 0;
                for (auto &t : tasks) { if (t.L > t.period+EPS) ok=false; Ut+=t.U; }
                if (!ok || Ut > m+EPS) continue;
                ++total_valid;

                auto decomps = decompose_taskset(tasks);
                auto sg = check_schedulability_gedf(tasks, decomps, m);
                auto sd = check_gedf_ds(tasks, decomps, m);
                if (sg.schedulable) ++acc_gedf;
                if (sd.schedulable) ++acc_ds;
            } catch (...) { continue; }
        }

        double rg = total_valid > 0 ? (double)acc_gedf / total_valid : 0;
        double rd = total_valid > 0 ? (double)acc_ds / total_valid : 0;
        printf("  U/m=%.2f: valid=%d, D-XU=%d(%.0f%%), D-XU-DS=%d(%.0f%%)\n",
               unorm, total_valid, acc_gedf, rg*100, acc_ds, rd*100);

        high_arr.push_back({
            {"util_norm", unorm}, {"total_valid", total_valid},
            {"gedf", acc_gedf}, {"ds", acc_ds},
            {"gedf_ratio", rg}, {"ds_ratio", rd}
        });
    }
    // overhead part
    nlohmann::json ov_arr = nlohmann::json::array();

    // --- Part 6b: 考虑开销的对比（常规任务） ---
    printf("\n  --- Overhead-Aware Schedulability ---\n");
    OverheadConfig cfg; cfg.O_mig = 0.1; cfg.O_loc = 0.0008;
    double scales[] = {1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0};

    for (double scale : scales) {
        int acc_gedf = 0, acc_ds = 0, acc_r = 0, acc_rds = 0, acc_np = 0;
        int valid = 0;
        double wmin = cfg.O_mig * scale;
        double wmax = cfg.O_mig * scale * 100;

        for (int trial = 0; trial < 40; ++trial) {
            unsigned seed = (unsigned)(scale * 100) + trial * 53;
            try {
                auto tasks = generate_taskset(4, m, 20, 60, p_edge, 0.4,
                                              wmin, wmax, seed);

                bool ok = true; double Ut = 0;
                for (auto &t : tasks) { if (t.L > t.period+EPS) ok=false; Ut+=t.U; }
                if (!ok || Ut > m+EPS) continue;
                ++valid;

                auto decomps = decompose_taskset(tasks);

                auto d_gedf = apply_overhead(decomps, cfg, SchedulerType::GEDF);
                auto d_ds   = apply_overhead(decomps, cfg, SchedulerType::GEDF_DS);
                auto d_r    = apply_overhead(decomps, cfg, SchedulerType::GEDF_R);
                auto d_rds  = apply_overhead(decomps, cfg, SchedulerType::GEDF_RDS);

                auto d_np_raw = apply_overhead(decomps, cfg, SchedulerType::GEDF_NP);
                std::vector<DecompositionResult> d_np;
                for (auto &d : d_np_raw) d_np.push_back(make_reassembled_decomp(d));

                if (check_schedulability_gedf(tasks, d_gedf, m).schedulable) ++acc_gedf;
                if (check_gedf_ds(tasks, d_ds, m).schedulable) ++acc_ds;
                if (check_gedf_r(tasks, d_r, m).schedulable) ++acc_r;
                if (check_gedf_ds(tasks, d_rds, m).schedulable) ++acc_rds;
                if (check_gedf_np(tasks, d_np, m).schedulable) ++acc_np;
            } catch (...) { continue; }
        }

        printf("  scale=%.0f: valid=%d | GEDF=%d DS=%d R=%d RDS=%d NP=%d\n",
               scale, valid, acc_gedf, acc_ds, acc_r, acc_rds, acc_np);

        ov_arr.push_back({
            {"wcet_scale", scale}, {"total_valid", valid},
            {"gedf", acc_gedf}, {"ds", acc_ds}, {"r", acc_r},
            {"rds", acc_rds}, {"np", acc_np},
            {"gedf_ratio", valid>0?(double)acc_gedf/valid:0.0},
            {"ds_ratio", valid>0?(double)acc_ds/valid:0.0},
            {"r_ratio", valid>0?(double)acc_r/valid:0.0},
            {"rds_ratio", valid>0?(double)acc_rds/valid:0.0},
            {"np_ratio", valid>0?(double)acc_np/valid:0.0}
        });
    }

    nlohmann::json j;
    j["high_elasticity"] = high_arr;
    j["overhead_comparison"] = ov_arr;
    return j;
}

// =====================================================================
//  Part 7: 精度评估（WCRT_sim / T_i）
// =====================================================================
static nlohmann::json run_precision_evaluation() {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 7: Precision Evaluation" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;
    int n_trials = 30;

    nlohmann::json per_trial = nlohmann::json::array();
    std::vector<double> all_precisions;

    for (int trial = 0; trial < n_trials; ++trial) {
        unsigned seed = 7000 + trial * 71;
        double unorm = 0.3 + 0.4 * ((double)trial / n_trials);
        try {
            auto tasks = generate_taskset(4, m, 30, 80, 0.1, unorm, 20, 80, seed);
            bool ok = true; double Ut = 0;
            for (auto &t : tasks) { if (t.L > t.period+EPS) ok=false; Ut+=t.U; }
            if (!ok || Ut > m+EPS) continue;

            auto decomps = decompose_taskset(tasks);
            auto sim = simulate_gedf(m, tasks, decomps, 5);

            auto prec = evaluate_precision(tasks, decomps, sim, m);

            nlohmann::json jt;
            jt["trial"] = trial; jt["util_norm"] = unorm;
            for (auto &pr : prec) {
                jt["tasks"].push_back({
                    {"task_id", pr.task_id}, {"wcrt_sim", pr.wcrt_sim},
                    {"rt_bound", pr.rt_bound_theorem}, {"precision", pr.precision},
                    {"period", pr.period}
                });
                if (pr.precision > EPS && pr.precision <= 1.0 + EPS)
                    all_precisions.push_back(pr.precision);
            }
            per_trial.push_back(jt);
        } catch (...) { continue; }
    }

    double avg_prec = 0;
    if (!all_precisions.empty()) {
        for (double pp : all_precisions) avg_prec += pp;
        avg_prec /= all_precisions.size();
    }
    double min_prec = all_precisions.empty() ? 0 : *std::min_element(all_precisions.begin(), all_precisions.end());
    double max_prec = all_precisions.empty() ? 0 : *std::max_element(all_precisions.begin(), all_precisions.end());

    nlohmann::json j;
    j["per_trial"] = per_trial;
    j["summary"] = {
        {"avg_precision", avg_prec}, {"min_precision", min_prec},
        {"max_precision", max_prec}, {"n_samples", (int)all_precisions.size()}
    };

    printf("\n  === Precision Summary ===\n");
    printf("  Average: %.2f%%\n", avg_prec * 100);
    printf("  Min:     %.2f%%\n", min_prec * 100);
    printf("  Max:     %.2f%%\n", max_prec * 100);
    printf("  Samples: %d\n", (int)all_precisions.size());

    return j;
}

// =====================================================================
//  Part 8: STG Dataset Validation
// =====================================================================
static nlohmann::json run_stg_experiment(const std::string &stg_dir) {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 8: STG Dataset Validation (" << stg_dir << ")" << endl;
    cout << std::string(70, '=') << endl;
 
    int m = 8;
    auto stg_tasks_raw = load_stg_directory(stg_dir, 0.4, 10, 42);
    if (stg_tasks_raw.empty()) {
        printf("  No STG files found, skipping.\n");
        nlohmann::json j;
        j["status"] = "no_data";
        return j;
    }
    printf("  Loaded %d DAGs from STG\n", (int)stg_tasks_raw.size());
 
    nlohmann::json j;
    j["num_dags"] = (int)stg_tasks_raw.size();
 
    double util_levels[] = {0.2, 0.3, 0.4, 0.5, 0.6};
    nlohmann::json by_util = nlohmann::json::array();
 
    for (double u : util_levels) {
        auto tasks = stg_tasks_raw;
        std::mt19937 rng((unsigned)(u * 10000));
        for (auto &t : tasks) assign_period(t, u, rng(), 3.0);
 
        // Keep subset with U_sum <= m
        std::vector<DAGTask> subset;
        double Usub = 0;
        for (auto &t : tasks) {
            if (t.L > t.period + EPS) continue;
            if (Usub + t.U > m) break;
            subset.push_back(t); Usub += t.U;
        }
        if (subset.empty()) continue;
 
        auto decomps = decompose_taskset(subset);
        auto sched = check_schedulability_gedf(subset, decomps, m);
        auto sim = simulate_gedf(m, subset, decomps, 5);
        auto prec = evaluate_precision(subset, decomps, sim, m);
 
        double avg_p = 0; int pc = 0;
        for (auto &p : prec)
            if (p.precision > EPS && p.precision <= 1.0 + EPS) { avg_p += p.precision; ++pc; }
        if (pc > 0) avg_p /= pc;
 
        printf("  U/m=%.1f: %d tasks, ana=%s sim=%s prec=%.1f%%\n",
               u, (int)subset.size(), sched.schedulable?"Y":"N",
               sim.schedulable?"Y":"N", avg_p*100);
 
        nlohmann::json entry;
        entry["util_norm"] = u;
        entry["n_tasks"] = (int)subset.size();
        entry["analytical"] = sched.schedulable;
        entry["simulation"] = sim.schedulable;
        entry["avg_precision"] = avg_p;
        nlohmann::json tasks_arr = nlohmann::json::array();
        for (size_t i = 0; i < subset.size(); ++i) {
            nlohmann::json tj;
            tj["n_v"] = (int)subset[i].vertices.size();
            tj["C"] = subset[i].C;
            tj["L"] = subset[i].L;
            tj["T"] = subset[i].period;
            tj["U"] = subset[i].U;
            tj["omega"] = decomps[i].omega;
            if (sim.wcrt.count(subset[i].task_id))
                tj["wcrt"] = sim.wcrt.at(subset[i].task_id);
            tasks_arr.push_back(tj);
        }
        entry["tasks"] = tasks_arr;
        by_util.push_back(entry);
    }
    j["by_util"] = by_util;
    return j;
}

// =====================================================================
//  Part 9: WfInstances (WfCommons) Dataset Validation
// =====================================================================
static nlohmann::json run_wfinstances_experiment(const std::string &wf_path) {
    cout << "\n" << std::string(70, '=') << endl;
    cout << "PART 9: WfInstances Dataset Validation (" << wf_path << ")" << endl;
    cout << std::string(70, '=') << endl;

    int m = 8;

    // 尝试作为目录批量加载，或作为单个 JSON 文件加载
    std::vector<DAGTask> wf_tasks_raw;
    namespace fs = std::filesystem;
    if (fs::is_directory(wf_path)) {
        wf_tasks_raw = load_wfinstances_directory(wf_path, 0.4, 10, 42);
    } else {
        auto t = load_wfinstances_file(wf_path, 0);
        if (!t.vertices.empty()) {
            assign_period(t, 0.4, 42, 3.0);
            wf_tasks_raw.push_back(t);
        }
    }

    if (wf_tasks_raw.empty()) {
        printf("  No WfInstances data found, skipping.\n");
        nlohmann::json j;
        j["status"] = "no_data";
        return j;
    }
    printf("  Loaded %d DAGs from WfInstances\n", (int)wf_tasks_raw.size());

    nlohmann::json j;
    j["num_dags"] = (int)wf_tasks_raw.size();

    double util_levels[] = {0.2, 0.3, 0.4, 0.5, 0.6};
    nlohmann::json by_util = nlohmann::json::array();

    for (double u : util_levels) {
        auto tasks = wf_tasks_raw;
        std::mt19937 rng((unsigned)(u * 10000));
        for (auto &t : tasks) assign_period(t, u, rng(), 3.0);

        // 保留 U_sum <= m 的子集
        std::vector<DAGTask> subset;
        double Usub = 0;
        for (auto &t : tasks) {
            if (t.L > t.period + EPS) continue;
            if (Usub + t.U > m) break;
            subset.push_back(t); Usub += t.U;
        }
        if (subset.empty()) continue;

        auto decomps = decompose_taskset(subset);
        auto sched = check_schedulability_gedf(subset, decomps, m);
        auto sim = simulate_gedf(m, subset, decomps, 5);
        auto prec = evaluate_precision(subset, decomps, sim, m);

        double avg_p = 0; int pc = 0;
        for (auto &pr : prec)
            if (pr.precision > EPS && pr.precision <= 1.0 + EPS) { avg_p += pr.precision; ++pc; }
        if (pc > 0) avg_p /= pc;

        printf("  U/m=%.1f: %d tasks, ana=%s sim=%s prec=%.1f%%\n",
               u, (int)subset.size(), sched.schedulable?"Y":"N",
               sim.schedulable?"Y":"N", avg_p*100);

        nlohmann::json entry;
        entry["util_norm"] = u;
        entry["n_tasks"] = (int)subset.size();
        entry["analytical"] = sched.schedulable;
        entry["simulation"] = sim.schedulable;
        entry["avg_precision"] = avg_p;
        nlohmann::json tasks_arr = nlohmann::json::array();
        for (size_t i = 0; i < subset.size(); ++i) {
            nlohmann::json tj;
            tj["n_v"] = (int)subset[i].vertices.size();
            tj["C"] = subset[i].C;
            tj["L"] = subset[i].L;
            tj["T"] = subset[i].period;
            tj["U"] = subset[i].U;
            tj["omega"] = decomps[i].omega;
            if (sim.wcrt.count(subset[i].task_id))
                tj["wcrt"] = sim.wcrt.at(subset[i].task_id);
            tasks_arr.push_back(tj);
        }
        entry["tasks"] = tasks_arr;
        by_util.push_back(entry);
    }
    j["by_util"] = by_util;
    return j;
}

// =====================================================================
//  Main
// =====================================================================
int main(int argc, char *argv[]) {
    std::string output_path = "output/results.json";
    std::string stg_dir = "";
    std::string wf_path = "";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--stg" && i + 1 < argc) {
            stg_dir = argv[++i];
        } else if (arg == "--wf" && i + 1 < argc) {
            wf_path = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (output_path == "output/results.json") {
            output_path = arg;
        }
    }

    nlohmann::json root;

    root["paper_example"] = run_paper_example();
    root["random_taskset"] = run_random_experiment();
    root["acceptance_ratio_vs_util"] = run_acceptance_ratio();
    root["acceptance_ratio_vs_processors"] = run_processor_sweep();
    root["variants_comparison"] = run_variants_comparison();
    auto high_and_ov = run_high_elasticity_and_overhead();
    root["high_elasticity"] = high_and_ov["high_elasticity"];
    root["overhead_comparison"] = high_and_ov["overhead_comparison"];
    root["precision_evaluation"] = run_precision_evaluation();

    // STG 数据集实验
    if (!stg_dir.empty()) {
        root["stg_experiment"] = run_stg_experiment(stg_dir);
    }

    // WfInstances 数据集实验
    if (!wf_path.empty()) {
        root["wfinstances_experiment"] = run_wfinstances_experiment(wf_path);
    }

    std::ofstream ofs(output_path);
    if (!ofs) { std::cerr << "Failed to open " << output_path << endl; return 1; }
    ofs << root.dump(2) << std::endl;
    ofs.close();
    printf("\n✓ Results written to %s\n", output_path.c_str());
    return 0;
}
