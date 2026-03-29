#pragma once
#include "dag_model.h"
#include <map>
#include <string>
#include <sstream>

/*
 * 顶点重组（Sec. IV-E）
 * 分解后同一顶点会被切成多段零散任务。对 GEDF / GEDF-NP，可将它们重新合并：
 *   - 释放时间：包含该顶点最早片段的段起点
 *   - 截止时间：包含该顶点最晚片段的段终点
 *   - WCET：各片段工作量之和
 * 这样会改变 E_max 与 D_min，对 GEDF-NP 的判定（定理 6）尤为关键。
 * 适用性：GEDF / GEDF-NP 支持；GEDF-DS / GEDF-R / GEDF-RDS 不适用。
 */

struct ReassembledVertex {
    int vertex_id;
    int parent_task_id;
    double wcet;               // sum of all parts
    double release_offset;     // min release among all parts
    double deadline_offset;    // max deadline among all parts
    double relative_deadline;  // deadline_offset - release_offset
    double density;            // wcet / relative_deadline
    double period;
};

struct ReassembledResult {
    int task_id;
    std::vector<ReassembledVertex> vertices;
    double E_max = 0;   // max wcet among reassembled vertices
    double D_min = 1e18; // min relative deadline
    double rho   = 0;    // E_max / D_min
};

inline ReassembledResult reassemble_vertices(const DecompositionResult &dr) {
    ReassembledResult rr;
    rr.task_id = dr.task_id;

    // Group sporadic tasks by vertex_id
    // For each vertex: accumulate wcet, track min release, max deadline
    struct VInfo {
        double total_wcet = 0;
        double min_release = 1e18;
        double max_deadline = -1e18;
        double period = 0;
    };
    std::map<int, VInfo> vmap;

    for (auto &st : dr.sporadic_tasks) {
        auto &vi = vmap[st.vertex_id];
        vi.total_wcet   += st.wcet;
        vi.min_release   = std::min(vi.min_release, st.release_offset);
        vi.max_deadline  = std::max(vi.max_deadline, st.deadline_offset);
        vi.period        = st.period;
    }

    for (auto &[vid, vi] : vmap) {
        ReassembledVertex rv;
        rv.vertex_id        = vid;
        rv.parent_task_id   = dr.task_id;
        rv.wcet             = vi.total_wcet;
        rv.release_offset   = vi.min_release;
        rv.deadline_offset  = vi.max_deadline;
        rv.relative_deadline = vi.max_deadline - vi.min_release;
        rv.density          = (rv.relative_deadline > EPS)
                               ? rv.wcet / rv.relative_deadline : 0;
        rv.period           = vi.period;
        rr.vertices.push_back(rv);

        rr.E_max = std::max(rr.E_max, rv.wcet);
        rr.D_min = std::min(rr.D_min, rv.relative_deadline);
    }

    rr.rho = (rr.D_min > EPS) ? rr.E_max / rr.D_min : 1e18;
    return rr;
}

// 将重组后的顶点再转回零散任务（用于基于重组的仿真）
inline std::vector<SporadicTask> reassembled_to_sporadic(
    const ReassembledResult &rr) {
    std::vector<SporadicTask> tasks;
    int cnt = 0;
    for (auto &rv : rr.vertices) {
        SporadicTask st;
        std::ostringstream oss;
        oss << "t" << rr.task_id << "_rv" << rv.vertex_id << "_" << cnt;
        st.task_id           = oss.str();
        st.parent_task_id    = rr.task_id;
        st.vertex_id         = rv.vertex_id;
        st.segment_id        = -1;  // reassembled
        st.wcet              = rv.wcet;
        st.period            = rv.period;
        st.release_offset    = rv.release_offset;
        st.deadline_offset   = rv.deadline_offset;
        st.relative_deadline = rv.relative_deadline;
        st.density           = rv.density;
        tasks.push_back(st);
        ++cnt;
    }
    return tasks;
}

// 构造带重组零散任务的 DecompositionResult（段保持不变，仅替换 sporadic_tasks）
inline DecompositionResult make_reassembled_decomp(
    const DecompositionResult &orig) {
    DecompositionResult dr = orig;  // copy segments, omega, etc.
    auto rr = reassemble_vertices(orig);
    dr.sporadic_tasks = reassembled_to_sporadic(rr);
    return dr;
}
