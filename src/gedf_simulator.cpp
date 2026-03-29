#include "gedf_simulator.h"
#include <algorithm>
#include <queue>
#include <sstream>
#include <cmath>
#include <limits>
#include <memory>

// ======================== Job ========================
// GEDF 仿真中的单个零散任务实例，带绝对释放/截止时间
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

    bool operator>(const Job &o) const {
        if (std::abs(absolute_deadline - o.absolute_deadline) > EPS)
            return absolute_deadline > o.absolute_deadline;
        return job_id > o.job_id;
    }
};

// ======================== Simulation ========================
// 主循环：按时间事件推进，先释放→整理就绪队列→EDF 分派→推进到下一事件
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

    // 预生成所有释放事件，便于按时间排序处理
    struct ReleaseEvent {
        double time;
        Job job;
    };
    std::vector<ReleaseEvent> releases;

    // DAG instance tracking
    // key = "dag_{task_id}_{instance}"
    std::map<std::string, DAGInstance> dag_instances;

    double sim_duration = 0;
    for (auto &t : tasks)
        sim_duration = std::max(sim_duration, t.period * num_periods);

    for (auto &d : decomps) {
        auto *task = task_map[d.task_id];
        for (int k = 0; k < num_periods; ++k) {
            // DAG instance
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

                Job job;
                job.job_id            = jid.str();
                job.parent_task_id    = d.task_id;
                job.vertex_id         = st.vertex_id;
                job.segment_id        = st.segment_id;
                job.instance          = k;
                job.release_time      = abs_rel;
                job.absolute_deadline = abs_dl;
                job.wcet              = st.wcet;
                job.remaining         = st.wcet;

                releases.push_back({abs_rel, job});
                di.job_ids.push_back(jid.str());
            }
            dag_instances[key.str()] = di;
        }
    }

    // Sort releases
    std::sort(releases.begin(), releases.end(),
              [](auto &a, auto &b){ return a.time < b.time; });

    // Simulation state
    std::vector<Job*> processors(m, nullptr);  // current job on each proc
    std::vector<Job> ready;                     // ready queue
    // Store all jobs for lifetime management
    std::vector<std::unique_ptr<Job>> all_jobs;
    // Map from job_id to Job*
    std::unordered_map<std::string, Job*> job_map;

    // Pre-create all jobs
    for (auto &re : releases) {
        auto jptr = std::make_unique<Job>(re.job);
        job_map[re.job.job_id] = jptr.get();
        all_jobs.push_back(std::move(jptr));
    }

    int rel_idx = 0;
    double cur_time = 0.0;

    while (cur_time < sim_duration + EPS) {
        // 1. Release jobs at cur_time
        while (rel_idx < (int)releases.size() &&
               releases[rel_idx].time <= cur_time + EPS) {
            Job *j = job_map[releases[rel_idx].job.job_id];
            if (j->remaining > EPS) ready.push_back(*j);
            ++rel_idx;
        }

        // 2. Collect all runnable: running + ready
        std::vector<Job*> runnable;
        for (int p = 0; p < m; ++p) {
            if (processors[p]) {
                // find this job in job_map
                Job *j = job_map[processors[p]->job_id];
                runnable.push_back(j);
                processors[p] = nullptr;
            }
        }
        for (auto &rj : ready) {
            Job *j = job_map[rj.job_id];
            if (j->remaining > EPS && j->finish_time < 0) {
                // avoid duplicates
                bool found = false;
                for (auto *r : runnable) if (r->job_id == j->job_id) { found = true; break; }
                if (!found) runnable.push_back(j);
            }
        }
        ready.clear();

        // Sort by EDF (earliest deadline first)
        std::sort(runnable.begin(), runnable.end(),
                  [](Job *a, Job *b){
                      if (std::abs(a->absolute_deadline - b->absolute_deadline) > EPS)
                          return a->absolute_deadline < b->absolute_deadline;
                      return a->job_id < b->job_id;
                  });

        // Assign top m to processors
        for (int i = 0; i < std::min(m, (int)runnable.size()); ++i)
            processors[i] = runnable[i];

        // Put rest back to ready
        for (int i = m; i < (int)runnable.size(); ++i) {
            Job rj = *runnable[i];
            ready.push_back(rj);
        }

        // 3. Find next event time
        double next_time = sim_duration + 1;

        // Next release
        if (rel_idx < (int)releases.size())
            next_time = std::min(next_time, releases[rel_idx].time);

        // Next completion
        for (int p = 0; p < m; ++p) {
            if (processors[p])
                next_time = std::min(next_time, cur_time + processors[p]->remaining);
        }

        if (next_time <= cur_time + EPS)
            next_time = cur_time + 1e-9;

        // 4. Advance: execute
        double dt = next_time - cur_time;
        for (int p = 0; p < m; ++p) {
            if (!processors[p]) continue;
            processors[p]->remaining -= dt;
            if (processors[p]->remaining <= EPS) {
                processors[p]->remaining = 0;
                processors[p]->finish_time = next_time;
                processors[p] = nullptr;
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
