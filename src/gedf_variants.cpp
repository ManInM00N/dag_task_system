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

// ======================== apply_overhead_sync ========================
// 同时更新 decomps 与 tasks：把开销叠加产生的额外 WCET 汇总到对应 DAG 顶点，
// 再调用 compute_parameters()，使 C / L / U / Γ / rdy / fsh 全部一致刷新；
// Ω 则按新的 DAG 参数重新 decompose 更新，以免后续判定用到过期值。
OverheadSyncResult apply_overhead_sync(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    const OverheadConfig &cfg,
    SchedulerType sched_type) {
    OverheadSyncResult out;
    out.decomps = apply_overhead(decomps, cfg, sched_type);
    out.tasks.reserve(tasks.size());

    // 建立 tid -> index 索引
    std::unordered_map<int, size_t> idx_of;
    for (size_t i = 0; i < decomps.size(); ++i) idx_of[decomps[i].task_id] = i;

    for (const auto &t_in : tasks) {
        DAGTask t = t_in; // 拷贝
        auto it = idx_of.find(t.task_id);
        if (it == idx_of.end()) { out.tasks.push_back(t); continue; }

        // 按顶点汇总新 WCET = Σ 所有切片调整后的 w
        std::unordered_map<int, double> new_wcet;
        for (const auto &seg : out.decomps[it->second].segments)
            for (const auto &[vid, w] : seg.assigned)
                new_wcet[vid] += w;

        for (auto &[vid, v] : t.vertices) {
            auto jt = new_wcet.find(vid);
            if (jt != new_wcet.end()) v.wcet = jt->second;
        }
        // 刷新派生参数
        t.compute_parameters();
        out.tasks.push_back(std::move(t));
    }
    return out;
}

// ======================== GEDF-DS  ========================
// 密度分离：重任务最多占用 m-1 个核，其余按 EDF 排队
// 同时显式维护 DAG 依赖（同一顶点多段串行 + 跨顶点前驱关系）

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

    // --- DAG 依赖 ---
    int pending_preds = 0;
    std::vector<DSJob*> unblocks;
    bool released = false;
    bool in_ready = false;

    bool is_ready() const {
        return released && pending_preds == 0 && remaining > EPS && finish_time < 0;
    }

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

    std::map<std::string, DAGInstance> dag_instances;

    double sim_dur = 0;
    for (auto &t : tasks) sim_dur = std::max(sim_dur, t.period * num_periods);

    // 预分配所有 job 并建立 vertex 索引
    std::unordered_map<std::string, std::unique_ptr<DSJob>> job_store;
    struct VKey { int tid, inst, vid;
        bool operator==(const VKey &o) const { return tid==o.tid && inst==o.inst && vid==o.vid; } };
    struct VKeyHash { size_t operator()(const VKey &k) const {
        return std::hash<long long>()(((long long)k.tid*131LL + k.inst)*131LL + k.vid); } };
    std::unordered_map<VKey, std::vector<DSJob*>, VKeyHash> vertex_jobs;

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
                auto p = std::make_unique<DSJob>();
                DSJob *j = p.get();
                j->job_id = jid.str();
                j->parent_task_id = d.task_id;
                j->vertex_id = st.vertex_id;
                j->segment_id = st.segment_id;
                j->instance = k;
                j->release_time = abs_rel;
                j->absolute_deadline = abs_dl;
                j->wcet = st.wcet;
                j->remaining = st.wcet;
                j->density = st.density;
                j->is_heavy = (st.density > 0.5 + EPS);

                di.job_ids.push_back(j->job_id);
                vertex_jobs[{d.task_id, k, st.vertex_id}].push_back(j);
                job_store[j->job_id] = std::move(p);
            }
            dag_instances[key.str()] = di;
        }
    }

    // DAG 依赖: 同一顶点 segment 串行 + 跨顶点前驱
    for (auto &[vk, jobs] : vertex_jobs) {
        std::sort(jobs.begin(), jobs.end(), [](DSJob *a, DSJob *b){
            return a->segment_id < b->segment_id;
        });
        for (size_t i = 1; i < jobs.size(); ++i) {
            jobs[i-1]->unblocks.push_back(jobs[i]);
            jobs[i]->pending_preds += 1;
        }
    }
    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            for (auto &[vid, v] : task->vertices) {
                auto it_cur = vertex_jobs.find({d.task_id, k, vid});
                if (it_cur == vertex_jobs.end() || it_cur->second.empty()) continue;
                DSJob *first = it_cur->second.front();
                for (int p : v.preds) {
                    auto it_p = vertex_jobs.find({d.task_id, k, p});
                    if (it_p == vertex_jobs.end() || it_p->second.empty()) continue;
                    DSJob *last_pred = it_p->second.back();
                    last_pred->unblocks.push_back(first);
                    first->pending_preds += 1;
                }
            }
        }
    }

    // 按 release_time 排序的指针
    std::vector<DSJob*> releases_sorted;
    for (auto &kv : job_store) releases_sorted.push_back(kv.second.get());
    std::sort(releases_sorted.begin(), releases_sorted.end(),
              [](DSJob *a, DSJob *b){ return a->release_time < b->release_time; });

    std::vector<DSJob*> procs(m, nullptr);
    std::vector<DSJob*> ready_heavy, ready_normal;

    auto try_push = [&](DSJob *j) {
        if (j->in_ready || !j->is_ready()) return;
        j->in_ready = true;
        if (j->is_heavy) ready_heavy.push_back(j);
        else             ready_normal.push_back(j);
    };

    int rel_idx = 0;
    double cur = 0.0;

    while (cur < sim_dur + EPS) {
        // Release
        while (rel_idx < (int)releases_sorted.size() &&
               releases_sorted[rel_idx]->release_time <= cur + EPS) {
            DSJob *j = releases_sorted[rel_idx];
            j->released = true;
            try_push(j);
            ++rel_idx;
        }

        // 回收当前 procs（便于重新 EDF 排序）
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            DSJob *j = procs[p]; procs[p] = nullptr;
            if (j->finish_time < 0 && j->remaining > EPS && !j->in_ready) {
                j->in_ready = true;
                if (j->is_heavy) ready_heavy.push_back(j);
                else             ready_normal.push_back(j);
            }
        }

        // EDF 排序
        std::sort(ready_heavy.begin(), ready_heavy.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });
        std::sort(ready_normal.begin(), ready_normal.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        // 分派：heavy 先占 min(|heavy|, m-1)
        int p_idx = 0;
        int mh = std::min((int)ready_heavy.size(), m - 1);
        for (int i = 0; i < mh; ++i) {
            procs[p_idx++] = ready_heavy[i];
            ready_heavy[i]->in_ready = false;
        }
        std::vector<DSJob*> leftover_heavy(ready_heavy.begin() + mh, ready_heavy.end());
        ready_heavy.clear();
        // leftover heavy 退化为 normal 等级参与剩余 p_idx..m-1 核的 EDF 竞争
        for (auto *j : leftover_heavy) ready_normal.push_back(j);
        std::sort(ready_normal.begin(), ready_normal.end(),
                  [](DSJob *a, DSJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        int taken = 0;
        for (auto *j : ready_normal) {
            if (p_idx >= m) break;
            procs[p_idx++] = j;
            j->in_ready = false;
            ++taken;
        }
        // 剩下的保留在 ready_normal
        std::vector<DSJob*> leftover(ready_normal.begin() + taken, ready_normal.end());
        ready_normal = std::move(leftover);
        // 但 leftover_heavy 中未被选中的也被并入 ready_normal（保持 in_ready=true）
        // 下一轮开始时会按 normal 处理 ——为了维持 heavy 标识，把它们放回 ready_heavy：
        {
            std::vector<DSJob*> stay_normal;
            for (auto *j : ready_normal) {
                if (j->is_heavy) { ready_heavy.push_back(j); }
                else             { stay_normal.push_back(j); }
            }
            ready_normal = std::move(stay_normal);
        }

        // Next event
        double next = sim_dur + 1;
        if (rel_idx < (int)releases_sorted.size())
            next = std::min(next, releases_sorted[rel_idx]->release_time);
        for (int p = 0; p < m; ++p)
            if (procs[p]) next = std::min(next, cur + procs[p]->remaining);
        if (next <= cur + EPS) next = cur + 1e-9;
        if (next > sim_dur + EPS) break;

        double dt = next - cur;
        std::vector<DSJob*> finished;
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            procs[p]->remaining -= dt;
            if (procs[p]->remaining <= EPS) {
                procs[p]->remaining = 0;
                procs[p]->finish_time = next;
                finished.push_back(procs[p]);
                procs[p] = nullptr;
            }
        }
        for (DSJob *j : finished) {
            for (DSJob *dn : j->unblocks) {
                dn->pending_preds -= 1;
                if (dn->released) try_push(dn);
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
        } else {
            result.schedulable = false;
        }
    }
    for (auto &[tid, rts] : result.response_times) {
        if (rts.empty()) continue;
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double s = 0; for (double r : rts) s += r;
        result.avg_rt[tid] = s / rts.size();
    }
    return result;
}

// ======================== GEDF-NP Simulator ========================
// 非抢占：已运行的任务不可被换出，只在空闲核上启动新任务
// 同样维护 DAG 依赖：顶点切片串行 + 跨顶点前驱

struct NPJob {
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
    bool started = false;

    // DAG 依赖
    int pending_preds = 0;
    std::vector<NPJob*> unblocks;
    bool released = false;

    bool is_ready() const {
        return released && pending_preds == 0 && !started &&
               remaining > EPS && finish_time < 0;
    }
};

SimResult simulate_gedf_np(int m,
                            std::vector<DAGTask> &tasks,
                            std::vector<DecompositionResult> &decomps,
                            int num_periods) {
    SimResult result;
    std::unordered_map<int, DAGTask*> task_map;
    for (auto &t : tasks) task_map[t.task_id] = &t;

    std::map<std::string, DAGInstance> dag_instances;

    double sim_dur = 0;
    for (auto &t : tasks) sim_dur = std::max(sim_dur, t.period * num_periods);

    std::unordered_map<std::string, std::unique_ptr<NPJob>> job_store;
    struct VKey { int tid, inst, vid;
        bool operator==(const VKey &o) const { return tid==o.tid && inst==o.inst && vid==o.vid; } };
    struct VKeyHash { size_t operator()(const VKey &k) const {
        return std::hash<long long>()(((long long)k.tid*131LL + k.inst)*131LL + k.vid); } };
    std::unordered_map<VKey, std::vector<NPJob*>, VKeyHash> vertex_jobs;

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
                auto p = std::make_unique<NPJob>();
                NPJob *j = p.get();
                j->job_id = jid.str();
                j->parent_task_id = d.task_id;
                j->vertex_id = st.vertex_id;
                j->segment_id = st.segment_id;
                j->instance = k;
                j->release_time = abs_rel;
                j->absolute_deadline = abs_dl;
                j->wcet = st.wcet;
                j->remaining = st.wcet;

                di.job_ids.push_back(j->job_id);
                vertex_jobs[{d.task_id, k, st.vertex_id}].push_back(j);
                job_store[j->job_id] = std::move(p);
            }
            dag_instances[key.str()] = di;
        }
    }

    // DAG 依赖
    for (auto &[vk, jobs] : vertex_jobs) {
        std::sort(jobs.begin(), jobs.end(), [](NPJob *a, NPJob *b){
            return a->segment_id < b->segment_id;
        });
        for (size_t i = 1; i < jobs.size(); ++i) {
            jobs[i-1]->unblocks.push_back(jobs[i]);
            jobs[i]->pending_preds += 1;
        }
    }
    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            for (auto &[vid, v] : task->vertices) {
                auto it_cur = vertex_jobs.find({d.task_id, k, vid});
                if (it_cur == vertex_jobs.end() || it_cur->second.empty()) continue;
                NPJob *first = it_cur->second.front();
                for (int p : v.preds) {
                    auto it_p = vertex_jobs.find({d.task_id, k, p});
                    if (it_p == vertex_jobs.end() || it_p->second.empty()) continue;
                    NPJob *last_pred = it_p->second.back();
                    last_pred->unblocks.push_back(first);
                    first->pending_preds += 1;
                }
            }
        }
    }

    std::vector<NPJob*> releases_sorted;
    for (auto &kv : job_store) releases_sorted.push_back(kv.second.get());
    std::sort(releases_sorted.begin(), releases_sorted.end(),
              [](NPJob *a, NPJob *b){ return a->release_time < b->release_time; });

    std::vector<NPJob*> procs(m, nullptr);
    std::vector<NPJob*> ready;

    int rel_idx = 0;
    double cur = 0.0;

    auto try_add_ready = [&](NPJob *j) {
        if (!j->is_ready()) return;
        for (auto *r : ready) if (r == j) return;
        ready.push_back(j);
    };

    while (cur < sim_dur + EPS) {
        // Release
        while (rel_idx < (int)releases_sorted.size() &&
               releases_sorted[rel_idx]->release_time <= cur + EPS) {
            NPJob *j = releases_sorted[rel_idx];
            j->released = true;
            try_add_ready(j);
            ++rel_idx;
        }

        // Sort ready by EDF
        std::sort(ready.begin(), ready.end(),
                  [](NPJob *a, NPJob *b){ return a->absolute_deadline < b->absolute_deadline; });

        // Assign to free processors (non-preemptive)
        for (int p = 0; p < m; ++p) {
            if (procs[p]) continue;
            auto it = ready.begin();
            while (it != ready.end()) {
                NPJob *j = *it;
                if (j->is_ready()) {
                    procs[p] = j;
                    j->started = true;
                    ready.erase(it);
                    break;
                }
                ++it;
            }
        }

        // Next event
        double next = sim_dur + 1;
        if (rel_idx < (int)releases_sorted.size())
            next = std::min(next, releases_sorted[rel_idx]->release_time);
        for (int p = 0; p < m; ++p)
            if (procs[p]) next = std::min(next, cur + procs[p]->remaining);
        if (next <= cur + EPS) next = cur + 1e-9;
        if (next > sim_dur + EPS) break;

        double dt = next - cur;
        std::vector<NPJob*> finished;
        for (int p = 0; p < m; ++p) {
            if (!procs[p]) continue;
            procs[p]->remaining -= dt;
            if (procs[p]->remaining <= EPS) {
                procs[p]->remaining = 0;
                procs[p]->finish_time = next;
                finished.push_back(procs[p]);
                procs[p] = nullptr;
            }
        }
        for (NPJob *j : finished) {
            for (NPJob *dn : j->unblocks) {
                dn->pending_preds -= 1;
                if (dn->released) try_add_ready(dn);
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
        } else {
            result.schedulable = false;
        }
    }
    for (auto &[tid, rts] : result.response_times) {
        if (rts.empty()) continue;
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double s = 0; for (double r : rts) s += r;
        result.avg_rt[tid] = s / rts.size();
    }
    return result;
}

// ======================== GEDF-R / GEDF-RDS Simulator ========================
// 受限迁移（Restricted Migration）：同一 DAG 顶点的所有切片必须绑定到同一核。
// 分配策略：Worst-Fit，按顶点总负载 (sum of wcet)/T 作为密度权重，依次放入
// 当前累计密度最低的核，兼顾负载均衡。
// 每个核维护本地 EDF 队列；DAG 依赖通过完成事件跨核通知。
//
// RDS 在 R 的基础上添加密度分离：当核上存在多个 ready job 时，
//   优先选 is_heavy=true（density > 0.5）的；若没有，再按 EDF。
//
// 说明：这是简化实现，反映主要调度语义；严格论文级 R 还包括作业偏移层级，
// 但对毕设级的定性趋势验证足够。

struct RJob {
    std::string job_id;
    int parent_task_id;
    int vertex_id;
    int segment_id;
    int instance;
    int assigned_core = -1;
    double release_time;
    double absolute_deadline;
    double wcet;
    double remaining;
    double finish_time = -1.0;
    double density = 0;
    bool is_heavy = false;

    int pending_preds = 0;
    std::vector<RJob*> unblocks;
    bool released = false;
    bool in_ready = false;

    bool is_ready() const {
        return released && pending_preds == 0 && remaining > EPS && finish_time < 0;
    }
};

// 内部共用仿真逻辑；density_separation=true 即 RDS
static SimResult simulate_gedf_r_impl(int m,
                                       std::vector<DAGTask> &tasks,
                                       std::vector<DecompositionResult> &decomps,
                                       int num_periods,
                                       bool density_separation) {
    SimResult result;
    std::unordered_map<int, DAGTask*> task_map;
    for (auto &t : tasks) task_map[t.task_id] = &t;

    double sim_dur = 0;
    for (auto &t : tasks) sim_dur = std::max(sim_dur, t.period * num_periods);

    // Step 1: 按顶点分配核。Worst-Fit 按顶点密度排序，放入负载最低核。
    //  vertex_core[(tid, vid)] = core_idx
    struct VKey { int tid, vid;
        bool operator==(const VKey &o) const { return tid==o.tid && vid==o.vid; } };
    struct VKeyHash { size_t operator()(const VKey &k) const {
        return std::hash<long long>()((long long)k.tid * 1000003LL + k.vid); } };
    std::unordered_map<VKey, int, VKeyHash> vertex_core;

    struct VItem { int tid, vid; double load; };
    std::vector<VItem> vitems;
    for (auto &d : decomps) {
        DAGTask *t = task_map[d.task_id];
        // 计算每个顶点的总 wcet（Σ 切片），并转为核上的利用贡献
        std::unordered_map<int, double> vw;
        for (auto &seg : d.segments)
            for (auto &[vid, w] : seg.assigned) vw[vid] += w;
        for (auto &[vid, w] : vw)
            vitems.push_back({d.task_id, vid, (t->period > EPS) ? w / t->period : w});
    }
    std::sort(vitems.begin(), vitems.end(),
              [](const VItem &a, const VItem &b){ return a.load > b.load; });

    std::vector<double> core_load(m, 0.0);
    for (auto &it : vitems) {
        int best = 0;
        for (int c = 1; c < m; ++c)
            if (core_load[c] < core_load[best] - EPS) best = c;
        vertex_core[{it.tid, it.vid}] = best;
        core_load[best] += it.load;
    }

    // Step 2: 构建所有 jobs + DAG 依赖
    std::unordered_map<std::string, std::unique_ptr<RJob>> job_store;
    struct JKey { int tid, inst, vid;
        bool operator==(const JKey &o) const { return tid==o.tid && inst==o.inst && vid==o.vid; } };
    struct JKeyHash { size_t operator()(const JKey &k) const {
        return std::hash<long long>()(((long long)k.tid*131LL + k.inst)*131LL + k.vid); } };
    std::unordered_map<JKey, std::vector<RJob*>, JKeyHash> vertex_jobs;

    std::map<std::string, DAGInstance> dag_instances;

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
                auto p = std::make_unique<RJob>();
                RJob *j = p.get();
                j->job_id = jid.str();
                j->parent_task_id = d.task_id;
                j->vertex_id = st.vertex_id;
                j->segment_id = st.segment_id;
                j->instance = k;
                j->release_time = abs_rel;
                j->absolute_deadline = abs_dl;
                j->wcet = st.wcet;
                j->remaining = st.wcet;
                j->density = st.density;
                j->is_heavy = (st.density > 0.5 + EPS);
                auto vit = vertex_core.find({d.task_id, st.vertex_id});
                j->assigned_core = (vit != vertex_core.end()) ? vit->second : 0;

                di.job_ids.push_back(j->job_id);
                vertex_jobs[{d.task_id, k, st.vertex_id}].push_back(j);
                job_store[j->job_id] = std::move(p);
            }
            dag_instances[key.str()] = di;
        }
    }

    // DAG 依赖
    for (auto &[vk, jobs] : vertex_jobs) {
        std::sort(jobs.begin(), jobs.end(), [](RJob *a, RJob *b){
            return a->segment_id < b->segment_id;
        });
        for (size_t i = 1; i < jobs.size(); ++i) {
            jobs[i-1]->unblocks.push_back(jobs[i]);
            jobs[i]->pending_preds += 1;
        }
    }
    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            for (auto &[vid, v] : task->vertices) {
                auto it_cur = vertex_jobs.find({d.task_id, k, vid});
                if (it_cur == vertex_jobs.end() || it_cur->second.empty()) continue;
                RJob *first = it_cur->second.front();
                for (int p : v.preds) {
                    auto it_p = vertex_jobs.find({d.task_id, k, p});
                    if (it_p == vertex_jobs.end() || it_p->second.empty()) continue;
                    RJob *last_pred = it_p->second.back();
                    last_pred->unblocks.push_back(first);
                    first->pending_preds += 1;
                }
            }
        }
    }

    // Step 3: 事件循环——每核独立 EDF 队列，允许抢占
    std::vector<RJob*> releases_sorted;
    for (auto &kv : job_store) releases_sorted.push_back(kv.second.get());
    std::sort(releases_sorted.begin(), releases_sorted.end(),
              [](RJob *a, RJob *b){ return a->release_time < b->release_time; });

    std::vector<RJob*> procs(m, nullptr);
    std::vector<std::vector<RJob*>> core_ready(m);

    auto core_cmp = [&](RJob *a, RJob *b){
        if (density_separation && a->is_heavy != b->is_heavy)
            return a->is_heavy;  // heavy 排前
        if (std::abs(a->absolute_deadline - b->absolute_deadline) > EPS)
            return a->absolute_deadline < b->absolute_deadline;
        return a->job_id < b->job_id;
    };

    auto try_push_ready = [&](RJob *j) {
        if (j->in_ready || !j->is_ready()) return;
        j->in_ready = true;
        core_ready[j->assigned_core].push_back(j);
    };

    int rel_idx = 0;
    double cur = 0.0;

    while (cur < sim_dur + EPS) {
        // Release
        while (rel_idx < (int)releases_sorted.size() &&
               releases_sorted[rel_idx]->release_time <= cur + EPS) {
            RJob *j = releases_sorted[rel_idx];
            j->released = true;
            try_push_ready(j);
            ++rel_idx;
        }

        // 回收 procs
        for (int c = 0; c < m; ++c) {
            if (!procs[c]) continue;
            RJob *j = procs[c]; procs[c] = nullptr;
            if (j->finish_time < 0 && j->remaining > EPS && !j->in_ready) {
                j->in_ready = true;
                core_ready[c].push_back(j);
            }
        }

        // 每核独立排序 + 挑头
        for (int c = 0; c < m; ++c) {
            auto &q = core_ready[c];
            if (q.empty()) continue;
            std::sort(q.begin(), q.end(), core_cmp);
            procs[c] = q.front();
            procs[c]->in_ready = false;
            q.erase(q.begin());
        }

        // Next event
        double next = sim_dur + 1;
        if (rel_idx < (int)releases_sorted.size())
            next = std::min(next, releases_sorted[rel_idx]->release_time);
        for (int c = 0; c < m; ++c)
            if (procs[c]) next = std::min(next, cur + procs[c]->remaining);
        if (next <= cur + EPS) next = cur + 1e-9;
        if (next > sim_dur + EPS) break;

        double dt = next - cur;
        std::vector<RJob*> finished;
        for (int c = 0; c < m; ++c) {
            if (!procs[c]) continue;
            procs[c]->remaining -= dt;
            if (procs[c]->remaining <= EPS) {
                procs[c]->remaining = 0;
                procs[c]->finish_time = next;
                finished.push_back(procs[c]);
                procs[c] = nullptr;
            }
        }
        for (RJob *j : finished) {
            for (RJob *dn : j->unblocks) {
                dn->pending_preds -= 1;
                if (dn->released) try_push_ready(dn);
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
        } else {
            result.schedulable = false;
        }
    }
    for (auto &[tid, rts] : result.response_times) {
        if (rts.empty()) continue;
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double s = 0; for (double r : rts) s += r;
        result.avg_rt[tid] = s / rts.size();
    }
    return result;
}

SimResult simulate_gedf_r(int m,
                           std::vector<DAGTask> &tasks,
                           std::vector<DecompositionResult> &decomps,
                           int num_periods) {
    return simulate_gedf_r_impl(m, tasks, decomps, num_periods, false);
}

SimResult simulate_gedf_rds(int m,
                             std::vector<DAGTask> &tasks,
                             std::vector<DecompositionResult> &decomps,
                             int num_periods) {
    return simulate_gedf_r_impl(m, tasks, decomps, num_periods, true);
}

// ======================== Precision Evaluation ========================
// 精度 = WCRT_sim / RT_bound；提供三种理论上界（见头文件注释）。
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

        // ---- 三种理论上界 ----
        // (1) 最宽松：T_i
        pr.rt_bound_period = task.period;

        // (2) 紧界：Ω_i * T_i（通常 Ω ∈ [1, 2-1/m]，故更紧）
        double omega_i = (dr.omega > EPS) ? dr.omega : 1.0;
        pr.rt_bound_omega = omega_i * task.period;

        // (3) Graham 型并行上界：L_i + (C_i - L_i)/m
        double graham = (m > 0) ? task.L + std::max(0.0, task.C - task.L) / m
                                : task.L + (task.C - task.L);
        pr.rt_bound_graham = graham;

        // ---- 精度（越接近 1 说明分析越紧；> 1 说明上界失效）----
        auto safe_div = [](double num, double den) {
            return (den > EPS) ? num / den : 0.0;
        };
        pr.precision_period = safe_div(pr.wcrt_sim, pr.rt_bound_period);
        pr.precision_omega  = safe_div(pr.wcrt_sim, pr.rt_bound_omega);
        pr.precision_graham = safe_div(pr.wcrt_sim, pr.rt_bound_graham);

        // 默认 precision = 基于 Ω·T 的紧界（保持与旧接口兼容）
        pr.rt_bound_theorem = pr.rt_bound_omega;
        pr.precision        = pr.precision_omega;

        results.push_back(pr);
    }
    return results;
}
