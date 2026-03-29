#include "gedf_variants.h"
#include <algorithm>
#include <queue>
#include <sstream>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>

// ======================== 理论判定 ========================
// GEDF 变体的理论判定：NP、DS、R

GEDFNP_Result check_gedf_np(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m) {
    GEDFNP_Result r;

    double omega_top = 0, gamma_top = 0, U_sum = 0;
    for (auto &d : decomps) omega_top = std::max(omega_top, d.omega);
    for (auto &t : tasks) { gamma_top = std::max(gamma_top, t.elasticity); U_sum += t.U; }

    // 在零散任务中求最大 WCET (E_max) 与最小相对截止 D_min
    r.E_max = 0; r.D_min = 1e18;
    for (auto &d : decomps) {
        for (auto &st : d.sporadic_tasks) {
            r.E_max = std::max(r.E_max, st.wcet);
            r.D_min = std::min(r.D_min, st.relative_deadline);
        }
    }
    r.rho = (r.D_min > EPS) ? r.E_max / r.D_min : 1e18;

    // 定理 6 判定条件：(1-ρ)/Ω_⊤ - Γ_⊤ > 0
    double denom = (omega_top > EPS) ? (1.0 - r.rho) / omega_top - gamma_top : -1e18;

    if (denom <= EPS) {
        r.schedulable = false;
        r.m_required = 1e18;
    } else {
        r.m_required = (U_sum - gamma_top) / denom;
        r.schedulable = (m >= r.m_required);
    }
    return r;
}

GEDFDS_Result check_gedf_ds(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m) {
    GEDFDS_Result r;

    double omega_top = 0, gamma_top = 0, U_sum = 0;
    for (auto &d : decomps) omega_top = std::max(omega_top, d.omega);
    for (auto &t : tasks) { gamma_top = std::max(gamma_top, t.elasticity); U_sum += t.U; }

    // 从零散任务提取 δ_⊤
    r.delta_top = 0;
    for (auto &d : decomps)
        for (auto &st : d.sporadic_tasks)
            r.delta_top = std::max(r.delta_top, st.density);

    if (r.delta_top > 1.0 + EPS) {
        r.schedulable = false;
        r.m_required = 1e18;
        r.U_bound = 0;
        return r;
    }

    if (r.delta_top <= 0.5 + EPS) {
        // δ_⊤ ≤ 0.5 时等同 GEDF（定理 3）
        double denom = (omega_top > EPS) ? 1.0 / omega_top - gamma_top : 1e18;
        if (denom <= EPS) {
            r.schedulable = false;
            r.m_required = 1e18;
        } else {
            r.m_required = (U_sum - gamma_top) / denom;
            r.schedulable = (m >= r.m_required);
        }
        r.U_bound = 0;
    } else {
        // 另一种情况：U_Σ ≤ (m+1)/(2Ω_⊤)
        r.U_bound = (omega_top > EPS) ? (m + 1.0) / (2.0 * omega_top) : 1e18;
        r.schedulable = (U_sum <= r.U_bound + EPS);
        r.m_required = (omega_top > EPS) ? 2.0 * omega_top * U_sum - 1.0 : 1e18;
    }
    return r;
}

AnalyticalResult check_gedf_r(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m) {
    // Theorem 8: same condition as Theorem 3
    return check_schedulability_gedf(tasks, decomps, m);
}

// ======================== 开销叠加 ========================
// 将迁移/上下文切换开销叠加到每个切片，并重建 sporadic 任务

std::vector<DecompositionResult> apply_overhead(
    const std::vector<DecompositionResult> &decomps,
    const OverheadConfig &cfg,
    SchedulerType sched_type) {
    std::vector<DecompositionResult> result = decomps;  // deep copy

    for (auto &dr : result) {
        // 记录顶点切片是否为首片
        std::unordered_set<int> seen_vertices;

        for (auto &seg : dr.segments) {
            for (auto &[vid, w] : seg.assigned) {
                bool is_head = (seen_vertices.find(vid) == seen_vertices.end());
                seen_vertices.insert(vid);

                double overhead = 0;
                switch (sched_type) {
                case SchedulerType::GEDF:
                case SchedulerType::GEDF_DS:
                    // head: O_mig + 2*O_loc;  non-head: 2*O_mig + 2*O_loc
                    overhead = is_head ? (cfg.O_mig + 2 * cfg.O_loc)
                                       : (2 * cfg.O_mig + 2 * cfg.O_loc);
                    break;
                case SchedulerType::GEDF_R:
                case SchedulerType::GEDF_RDS:
                    // head: 2*O_loc;  non-head: 2*O_loc + O_mig
                    overhead = is_head ? (2 * cfg.O_loc)
                                       : (2 * cfg.O_loc + cfg.O_mig);
                    break;
                case SchedulerType::GEDF_NP:
                    // 非抢占：仅一次上下文开销
                    overhead = is_head ? cfg.O_loc : (cfg.O_loc + cfg.O_mig);
                    break;
                }
                w += overhead;
                seg.c += overhead;
            }
        }

        // 根据修改后的切片重建零散任务并更新 WCET
        dr.sporadic_tasks.clear();
        double cum = 0.0;
        int cnt = 0;
        for (auto &seg : dr.segments) {
            double seg_rel = cum;
            double seg_dl  = cum + seg.d;
            for (auto &[vid, w] : seg.assigned) {
                if (w < EPS) continue;
                SporadicTask st;
                std::ostringstream oss;
                oss << "t" << dr.task_id << "_v" << vid << "_s" << seg.seg_id << "_" << cnt;
                st.task_id = oss.str();
                st.parent_task_id = dr.task_id;
                st.vertex_id = vid;
                st.segment_id = seg.seg_id;
                st.wcet = w;
                st.period = seg.d;  // will be overwritten
                st.release_offset = seg_rel;
                st.deadline_offset = seg_dl;
                st.relative_deadline = seg.d;
                st.density = (seg.d > EPS) ? w / seg.d : 0;
                dr.sporadic_tasks.push_back(st);
                ++cnt;
            }
            cum += seg.d;
        }

        // Recompute delta_hat, ell_hat
        dr.delta_hat = 0; dr.ell_hat = 0;
        for (auto &seg : dr.segments) {
            if (seg.d > EPS) {
                dr.delta_hat = std::max(dr.delta_hat, seg.e / seg.d);
                dr.ell_hat   = std::max(dr.ell_hat,   seg.c / seg.d);
            }
        }
    }
    return result;
}

// ======================== GEDF-DS  ========================
// 密度分离：重任务最多占用 m-1 个核，其余按 EDF 排队

struct DSJob {
    std::string job_id;
    int parent_task_id;
    int vertex_id;
    int segment_id;
    int instance;
    double release_time;
    double absolute_deadline;
    double wcet;
    double remaining;
    double finish_time = -1.0;
    double density;
    bool is_heavy;  // density > 0.5

    bool operator>(const DSJob &o) const {
        if (std::abs(absolute_deadline - o.absolute_deadline) > EPS)
            return absolute_deadline > o.absolute_deadline;
        return job_id > o.job_id;
    }
};

SimResult simulate_gedf_ds(int m,
                            std::vector<DAGTask> &tasks,
                            std::vector<DecompositionResult> &decomps,
                            int num_periods) {
    SimResult result;
    std::unordered_map<int, DAGTask*> task_map;
    for (auto &t : tasks) task_map[t.task_id] = &t;

    struct REvent { double time; DSJob job; };
    std::vector<REvent> releases;
    std::map<std::string, DAGInstance> dag_instances;

    double sim_dur = 0;
    for (auto &t : tasks) sim_dur = std::max(sim_dur, t.period * num_periods);

    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            std::ostringstream key; key << "dag_" << d.task_id << "_" << k;
            DAGInstance di; di.task_id = d.task_id; di.instance = k;
            di.release_time = k * task->period;
            di.deadline = (k+1) * task->period;

            for (auto &st : d.sporadic_tasks) {
                double abs_rel = k * task->period + st.release_offset;
                double abs_dl  = k * task->period + st.deadline_offset;
                if (abs_rel >= sim_dur) continue;

                std::ostringstream jid; jid << st.task_id << "_i" << k;
                DSJob job;
                job.job_id = jid.str();
                job.parent_task_id = d.task_id;
                job.vertex_id = st.vertex_id;
                job.segment_id = st.segment_id;
                job.instance = k;
                job.release_time = abs_rel;
                job.absolute_deadline = abs_dl;
                job.wcet = st.wcet;
                job.remaining = st.wcet;
                job.density = st.density;
                job.is_heavy = (st.density > 0.5 + EPS);

                releases.push_back({abs_rel, job});
                di.job_ids.push_back(jid.str());
            }
            dag_instances[key.str()] = di;
        }
    }

    std::sort(releases.begin(), releases.end(),
              [](auto &a, auto &b){ return a.time < b.time; });

    // Store jobs
    std::unordered_map<std::string, std::unique_ptr<DSJob>> job_store;
    for (auto &re : releases) {
        auto p = std::make_unique<DSJob>(re.job);
        job_store[re.job.job_id] = std::move(p);
    }

    // Processors: heavy queue (up to m-1) + normal EDF
    std::vector<DSJob*> procs(m, nullptr);
    std::vector<DSJob*> ready_normal;
    std::vector<DSJob*> ready_heavy;

    int rel_idx = 0;
    double cur = 0.0;

    while (cur < sim_dur + EPS) {
        // Release
        while (rel_idx < (int)releases.size() && releases[rel_idx].time <= cur + EPS) {
            DSJob *j = job_store[releases[rel_idx].job.job_id].get();
            if (j->remaining > EPS) {
                if (j->is_heavy && (int)ready_heavy.size() < m - 1)
                    ready_heavy.push_back(j);
                else
                    ready_normal.push_back(j);
            }
            ++rel_idx;
        }

        // Collect running
        std::vector<DSJob*> running_heavy, running_normal;
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            if (procs[p]->is_heavy)
                running_heavy.push_back(procs[p]);
            else
                running_normal.push_back(procs[p]);
            procs[p] = nullptr;
        }

        // Merge heavy
        for (auto *j : running_heavy) {
            bool dup = false;
            for (auto *r : ready_heavy) if (r->job_id == j->job_id) { dup = true; break; }
            if (!dup && j->remaining > EPS) ready_heavy.push_back(j);
        }
        for (auto *j : running_normal) {
            bool dup = false;
            for (auto *r : ready_normal) if (r->job_id == j->job_id) { dup = true; break; }
            if (!dup && j->remaining > EPS) ready_normal.push_back(j);
        }

        // Assign: heavy first (up to m-1 procs), then EDF for normal
        // Heavy sorted by deadline
        std::sort(ready_heavy.begin(), ready_heavy.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });
        std::sort(ready_normal.begin(), ready_normal.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        int p_idx = 0;
        int mh = std::min((int)ready_heavy.size(), m - 1);
        for (int i = 0; i < mh; ++i)
            procs[p_idx++] = ready_heavy[i];

        std::vector<DSJob*> leftover_heavy(ready_heavy.begin() + mh, ready_heavy.end());
        // Merge leftover heavy into normal queue
        for (auto *j : leftover_heavy) ready_normal.push_back(j);
        std::sort(ready_normal.begin(), ready_normal.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        for (int i = 0; i < (int)ready_normal.size() && p_idx < m; ++i)
            procs[p_idx++] = ready_normal[i];

        ready_heavy.clear(); ready_normal.clear();

        // Next event
        double next = sim_dur + 1;
        if (rel_idx < (int)releases.size())
            next = std::min(next, releases[rel_idx].time);
        for (int p = 0; p < m; ++p)
            if (procs[p]) next = std::min(next, cur + procs[p]->remaining);
        if (next <= cur + EPS) next = cur + 1e-9;

        double dt = next - cur;
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            procs[p]->remaining -= dt;
            if (procs[p]->remaining <= EPS) {
                procs[p]->remaining = 0;
                procs[p]->finish_time = next;
                procs[p] = nullptr;
            }
        }
        cur = next;
    }

    // Compute results
    for (auto &[key, di] : dag_instances) {
        double mf = 0; bool ok = true;
        for (auto &jid : di.job_ids) {
            auto it = job_store.find(jid);
            if (it == job_store.end() || it->second->finish_time < 0) { ok = false; continue; }
            mf = std::max(mf, it->second->finish_time);
        }
        if (ok && mf > 0) {
            double rt = mf - di.release_time;
            result.response_times[di.task_id].push_back(rt);
            if (mf > di.deadline + EPS) { result.schedulable = false; ++result.deadline_misses; }
        }
    }
    for (auto &[tid, rts] : result.response_times) {
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double s = 0; for (double r : rts) s += r;
        result.avg_rt[tid] = s / rts.size();
    }
    return result;
}

// ======================== GEDF-NP Simulator ========================
// 非抢占：已运行的任务不可被换出，只在空闲核上启动新任务

struct NPJob {
    std::string job_id;
    int parent_task_id;
    int instance;
    double release_time;
    double absolute_deadline;
    double wcet;
    double remaining;
    double finish_time = -1.0;
    bool started = false;
};

SimResult simulate_gedf_np(int m,
                            std::vector<DAGTask> &tasks,
                            std::vector<DecompositionResult> &decomps,
                            int num_periods) {
    SimResult result;
    std::unordered_map<int, DAGTask*> task_map;
    for (auto &t : tasks) task_map[t.task_id] = &t;

    struct REvent { double time; NPJob job; };
    std::vector<REvent> releases;
    std::map<std::string, DAGInstance> dag_instances;

    double sim_dur = 0;
    for (auto &t : tasks) sim_dur = std::max(sim_dur, t.period * num_periods);

    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            std::ostringstream key; key << "dag_" << d.task_id << "_" << k;
            DAGInstance di; di.task_id = d.task_id; di.instance = k;
            di.release_time = k * task->period;
            di.deadline = (k+1) * task->period;

            for (auto &st : d.sporadic_tasks) {
                double abs_rel = k * task->period + st.release_offset;
                double abs_dl  = k * task->period + st.deadline_offset;
                if (abs_rel >= sim_dur) continue;

                std::ostringstream jid; jid << st.task_id << "_i" << k;
                NPJob job;
                job.job_id = jid.str();
                job.parent_task_id = d.task_id;
                job.instance = k;
                job.release_time = abs_rel;
                job.absolute_deadline = abs_dl;
                job.wcet = st.wcet;
                job.remaining = st.wcet;

                releases.push_back({abs_rel, job});
                di.job_ids.push_back(jid.str());
            }
            dag_instances[key.str()] = di;
        }
    }

    std::sort(releases.begin(), releases.end(),
              [](auto &a, auto &b){ return a.time < b.time; });

    std::unordered_map<std::string, std::unique_ptr<NPJob>> job_store;
    for (auto &re : releases)
        job_store[re.job.job_id] = std::make_unique<NPJob>(re.job);

    std::vector<NPJob*> procs(m, nullptr);
    std::vector<NPJob*> ready;
    int rel_idx = 0;
    double cur = 0.0;

    while (cur < sim_dur + EPS) {
        // Release
        while (rel_idx < (int)releases.size() && releases[rel_idx].time <= cur + EPS) {
            NPJob *j = job_store[releases[rel_idx].job.job_id].get();
            if (j->remaining > EPS) ready.push_back(j);
            ++rel_idx;
        }

        // Sort ready by EDF
        std::sort(ready.begin(), ready.end(),
                  [](NPJob *a, NPJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        // Assign to free processors (non-preemptive: don't touch running jobs)
        for (int p = 0; p < m; ++p) {
            if (procs[p]) continue;  // busy, can't preempt
            // Find first ready job not already running
            for (auto it = ready.begin(); it != ready.end(); ++it) {
                NPJob *j = *it;
                if (!j->started && j->remaining > EPS) {
                    procs[p] = j;
                    j->started = true;
                    ready.erase(it);
                    break;
                }
            }
        }

        // Next event
        double next = sim_dur + 1;
        if (rel_idx < (int)releases.size())
            next = std::min(next, releases[rel_idx].time);
        for (int p = 0; p < m; ++p)
            if (procs[p]) next = std::min(next, cur + procs[p]->remaining);
        if (next <= cur + EPS) next = cur + 1e-9;

        double dt = next - cur;
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            procs[p]->remaining -= dt;
            if (procs[p]->remaining <= EPS) {
                procs[p]->remaining = 0;
                procs[p]->finish_time = next;
                procs[p] = nullptr;
            }
        }
        cur = next;
    }

    // Results
    for (auto &[key, di] : dag_instances) {
        double mf = 0; bool ok = true;
        for (auto &jid : di.job_ids) {
            auto it = job_store.find(jid);
            if (it == job_store.end() || it->second->finish_time < 0) { ok = false; continue; }
            mf = std::max(mf, it->second->finish_time);
        }
        if (ok && mf > 0) {
            double rt = mf - di.release_time;
            result.response_times[di.task_id].push_back(rt);
            if (mf > di.deadline + EPS) { result.schedulable = false; ++result.deadline_misses; }
        }
    }
    for (auto &[tid, rts] : result.response_times) {
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double s = 0; for (double r : rts) s += r;
        result.avg_rt[tid] = s / rts.size();
    }
    return result;
}

// ======================== Precision Evaluation ========================
// 精度 = 仿真 WCRT / 理论周期上界，越接近 1 越紧

std::vector<PrecisionResult> evaluate_precision(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    const SimResult &sim_result,
    int m) {
    std::vector<PrecisionResult> results;

    for (size_t i = 0; i < tasks.size(); ++i) {
        auto &task = tasks[i];
        auto &dr   = decomps[i];
        int tid    = task.task_id;

        PrecisionResult pr;
        pr.task_id = tid;
        pr.period  = task.period;

        // Simulated WCRT
        auto it = sim_result.wcrt.find(tid);
        pr.wcrt_sim = (it != sim_result.wcrt.end()) ? it->second : 0;

        // Theoretical RT upper bound:
        // For implicit-deadline tasks under GEDF, the schedulability
        // condition guarantees all jobs meet deadlines.
        // Thus RT ≤ T_i (the period/deadline) is the theoretical bound.
        // Precision = WCRT_sim / T_i → closer to 1 = analysis is tight.
        double rt_bound = task.period;
        pr.rt_bound_theorem = rt_bound;

        // Precision = WCRT_sim / RT_bound
        // Closer to 1 (100%) → tighter analysis
        pr.precision = (rt_bound > EPS) ? pr.wcrt_sim / rt_bound : 0;

        results.push_back(pr);
    }
    return results;
}
