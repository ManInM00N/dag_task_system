#include "gedf_simulator.h"
#include <algorithm>
#include <queue>
#include <sstream>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

// ======================== Job ========================
// GEDF 仿真中的单个零散任务实例，带绝对释放/截止时间
// 为正确处理 DAG 依赖，引入 ready_time：必须同时满足
//   1) cur_time >= release_time
//   2) 原 DAG 中该顶点的所有前驱顶点都已完成
//   3) 同一顶点的上一个 segment 切片已完成
struct Job {
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

    // --- DAG 依赖 ---
    int pending_preds = 0;      // 还有多少个前驱 job 尚未完成
    std::vector<Job*> unblocks; // 本 job 完成后需要通知的下游 job
    bool released = false;      // 已经在 release_time 之后
    bool in_ready = false;      // 已加入就绪队列

    bool is_ready() const {
        return released && pending_preds == 0 && remaining > EPS && finish_time < 0;
    }

    bool operator>(const Job &o) const {
        if (std::abs(absolute_deadline - o.absolute_deadline) > EPS)
            return absolute_deadline > o.absolute_deadline;
        return job_id > o.job_id;
    }
};

// ======================== Simulation ========================
// 主循环：按时间事件推进
//   1) 到 release_time 的 job 先标记 released；
//   2) 若同时 pending_preds == 0 则加入 ready 队列；
//   3) EDF 选 m 个执行；
//   4) 推进到下一事件（新 release 或 job 完成）；
//   5) 完成的 job 递减下游 pending_preds，可能触发更多 ready。
SimResult simulate_gedf(int m,
                         std::vector<DAGTask> &tasks,
                         std::vector<DecompositionResult> &decomps,
                         int num_periods) {
    SimResult result;

    // 快速索引 DAG 与分解结果
    std::unordered_map<int, DAGTask*> task_map;
    std::unordered_map<int, DecompositionResult*> decomp_map;
    for (auto &t : tasks) task_map[t.task_id] = &t;
    for (auto &d : decomps) decomp_map[d.task_id] = &d;

    // DAG instance tracking
    std::map<std::string, DAGInstance> dag_instances;

    double sim_duration = 0;
    for (auto &t : tasks)
        sim_duration = std::max(sim_duration, t.period * num_periods);

    // 预分配所有 job：每个 (decomp, sporadic_task, k) 一个 Job
    std::vector<std::unique_ptr<Job>> all_jobs;
    std::unordered_map<std::string, Job*> job_map;

    // 辅助索引: (task_id, instance, vertex_id) -> 本顶点的所有 segment job 指针（按 segment_id 升序）
    struct VKey { int tid, inst, vid;
        bool operator==(const VKey &o) const { return tid==o.tid && inst==o.inst && vid==o.vid; } };
    struct VKeyHash { size_t operator()(const VKey &k) const {
        return std::hash<long long>()(((long long)k.tid*131LL + k.inst)*131LL + k.vid); } };
    std::unordered_map<VKey, std::vector<Job*>, VKeyHash> vertex_jobs;

    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            std::ostringstream key;
            key << "dag_" << d.task_id << "_" << k;
            DAGInstance di;
            di.task_id      = d.task_id;
            di.instance     = k;
            di.release_time = k * task->period;
            di.deadline     = (k + 1) * task->period;

            for (auto &st : d.sporadic_tasks) {
                double abs_rel = k * task->period + st.release_offset;
                double abs_dl  = k * task->period + st.deadline_offset;
                if (abs_rel >= sim_duration) continue;

                std::ostringstream jid;
                jid << st.task_id << "_i" << k;

                auto jptr = std::make_unique<Job>();
                Job *j = jptr.get();
                j->job_id            = jid.str();
                j->parent_task_id    = d.task_id;
                j->vertex_id         = st.vertex_id;
                j->segment_id        = st.segment_id;
                j->instance          = k;
                j->release_time      = abs_rel;
                j->absolute_deadline = abs_dl;
                j->wcet              = st.wcet;
                j->remaining         = st.wcet;

                job_map[j->job_id] = j;
                di.job_ids.push_back(j->job_id);
                vertex_jobs[{d.task_id, k, st.vertex_id}].push_back(j);
                all_jobs.push_back(std::move(jptr));
            }
            dag_instances[key.str()] = di;
        }
    }

    // 建立 DAG 依赖：
    //  (a) 同一顶点的多个 segment job 按 segment_id 升序串行；
    //  (b) 顶点 v 的第一个 segment job 等其所有 DAG 前驱顶点的最后一个 segment job 完成。
    for (auto &[vk, jobs] : vertex_jobs) {
        // 按 segment_id 排序
        std::sort(jobs.begin(), jobs.end(), [](Job *a, Job *b){
            return a->segment_id < b->segment_id;
        });
        // 链式串行
        for (size_t i = 1; i < jobs.size(); ++i) {
            jobs[i-1]->unblocks.push_back(jobs[i]);
            jobs[i]->pending_preds += 1;
        }
    }
    // DAG 前驱依赖
    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            for (auto &[vid, v] : task->vertices) {
                auto it_cur = vertex_jobs.find({d.task_id, k, vid});
                if (it_cur == vertex_jobs.end() || it_cur->second.empty()) continue;
                Job *first = it_cur->second.front();
                for (int p : v.preds) {
                    auto it_p = vertex_jobs.find({d.task_id, k, p});
                    if (it_p == vertex_jobs.end() || it_p->second.empty()) continue;
                    Job *last_pred = it_p->second.back();
                    last_pred->unblocks.push_back(first);
                    first->pending_preds += 1;
                }
            }
        }
    }

    // 预生成按 release_time 升序的指针列表
    std::vector<Job*> releases_sorted;
    releases_sorted.reserve(all_jobs.size());
    for (auto &up : all_jobs) releases_sorted.push_back(up.get());
    std::sort(releases_sorted.begin(), releases_sorted.end(),
              [](Job *a, Job *b){ return a->release_time < b->release_time; });

    // Simulation state
    std::vector<Job*> processors(m, nullptr);
    std::vector<Job*> ready;                   // 就绪队列（DAG 依赖已满足）

    auto try_push_ready = [&](Job *j) {
        if (!j->in_ready && j->is_ready()) {
            j->in_ready = true;
            ready.push_back(j);
        }
    };

    int rel_idx = 0;
    double cur_time = 0.0;

    while (cur_time < sim_duration + EPS) {
        // 1. 处理 release 事件
        while (rel_idx < (int)releases_sorted.size() &&
               releases_sorted[rel_idx]->release_time <= cur_time + EPS) {
            Job *j = releases_sorted[rel_idx];
            j->released = true;
            try_push_ready(j);
            ++rel_idx;
        }

        // 2. 回收当前处理器上的 job 到 ready（EDF 需要重排）
        for (int p = 0; p < m; ++p) {
            if (processors[p]) {
                Job *j = processors[p];
                processors[p] = nullptr;
                if (j->finish_time < 0 && j->remaining > EPS) {
                    if (!j->in_ready) { j->in_ready = true; ready.push_back(j); }
                }
            }
        }

        // 3. EDF 排序 ready
        std::sort(ready.begin(), ready.end(),
                  [](Job *a, Job *b){
                      if (std::abs(a->absolute_deadline - b->absolute_deadline) > EPS)
                          return a->absolute_deadline < b->absolute_deadline;
                      return a->job_id < b->job_id;
                  });

        // 4. 分配 top m 到处理器
        int picked = std::min(m, (int)ready.size());
        for (int i = 0; i < picked; ++i) {
            processors[i] = ready[i];
            ready[i]->in_ready = false;
        }
        // 剩余保留在 ready（标记 in_ready=true）
        std::vector<Job*> leftover(ready.begin() + picked, ready.end());
        ready = std::move(leftover);

        // 5. 找下一事件时间
        double next_time = sim_duration + 1;
        if (rel_idx < (int)releases_sorted.size())
            next_time = std::min(next_time, releases_sorted[rel_idx]->release_time);
        for (int p = 0; p < m; ++p) {
            if (processors[p])
                next_time = std::min(next_time, cur_time + processors[p]->remaining);
        }
        if (next_time <= cur_time + EPS)
            next_time = cur_time + 1e-9;
        if (next_time > sim_duration + EPS) break;

        // 6. 推进执行
        double dt = next_time - cur_time;
        std::vector<Job*> just_finished;
        for (int p = 0; p < m; ++p) {
            if (!processors[p]) continue;
            processors[p]->remaining -= dt;
            if (processors[p]->remaining <= EPS) {
                processors[p]->remaining = 0;
                processors[p]->finish_time = next_time;
                just_finished.push_back(processors[p]);
                processors[p] = nullptr;
            }
        }

        // 7. 完成触发下游 pending_preds 减一
        for (Job *j : just_finished) {
            for (Job *dn : j->unblocks) {
                dn->pending_preds -= 1;
                // 如果已 release 且前驱全部完成，加入 ready
                if (dn->released) try_push_ready(dn);
            }
        }

        cur_time = next_time;
    }

    // Compute results
    for (auto &[key, di] : dag_instances) {
        double max_finish = 0;
        bool all_done = true;
        for (auto &jid : di.job_ids) {
            auto it = job_map.find(jid);
            if (it == job_map.end()) { all_done = false; continue; }
            Job *j = it->second;
            if (j->finish_time < 0) { all_done = false; continue; }
            max_finish = std::max(max_finish, j->finish_time);
        }
        if (all_done && max_finish > 0) {
            double rt = max_finish - di.release_time;
            di.response_time = rt;
            result.response_times[di.task_id].push_back(rt);

            if (max_finish > di.deadline + EPS) {
                result.schedulable = false;
                ++result.deadline_misses;
            }
        } else {
            // 有 job 未完成视为不可调度
            result.schedulable = false;
        }
    }

    for (auto &[tid, rts] : result.response_times) {
        if (rts.empty()) continue;
        result.wcrt[tid] = *std::max_element(rts.begin(), rts.end());
        double sum = 0; for (double r : rts) sum += r;
        result.avg_rt[tid] = sum / rts.size();
    }

    return result;
}

// ======================== Analytical Test ========================
AnalyticalResult check_schedulability_gedf(
    const std::vector<DAGTask> &tasks,
    const std::vector<DecompositionResult> &decomps,
    int m) {
    AnalyticalResult r;
    r.omega_top = 0; r.gamma_top = 0; r.U_sum = 0;

    for (auto &d : decomps) r.omega_top = std::max(r.omega_top, d.omega);
    for (auto &t : tasks) {
        r.gamma_top = std::max(r.gamma_top, t.elasticity);
        r.U_sum += t.U;
    }

    r.denom = (r.omega_top > EPS) ? 1.0 / r.omega_top - r.gamma_top : 1e18;

    if (r.denom <= EPS) {
        r.schedulable = false;
        r.m_required  = 1e18;
    } else {
        r.m_required  = (r.U_sum - r.gamma_top) / r.denom;
        r.schedulable = (m >= r.m_required);
    }

    r.cap_aug_bound = (m > 0) ? (2.0 - 1.0 / m) * r.omega_top : 1e18;
    return r;
}
