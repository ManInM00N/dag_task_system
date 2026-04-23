#include "decomposition.h"
#include <algorithm>
#include <cmath>
#include <sstream>

// ============================================================
//  Step 1: 根据 rdy/fsh 边界生成时间段
//  收集所有 rdy/fsh，排序后相邻边界构成段。
// ============================================================
static std::vector<Segment> build_segments(const DAGTask &task) {
    std::set<double> bset;
    for (auto &[vid, v] : task.vertices) {
        bset.insert(v.rdy);
        bset.insert(v.fsh);
    }
    std::vector<double> bounds(bset.begin(), bset.end());
    std::sort(bounds.begin(), bounds.end());

    std::vector<Segment> segs;
    for (int i = 0; i + 1 < (int)bounds.size(); ++i) {
        Segment s;
        s.seg_id = i;
        s.start  = bounds[i];
        s.end    = bounds[i + 1];
        s.e      = s.end - s.start;
        segs.push_back(s);
    }
    return segs;
}

// Does vertex v's lifetime window [rdy, fsh] cover segment s?
static bool covers(const Vertex &v, const Segment &s) {
    return v.rdy <= s.start + EPS && s.end <= v.fsh + EPS;
}

// ============================================================
//  Step 2: 三阶段分配算法（Algorithm 1）
//    1) 单段覆盖的顶点切片直接放入该段；
//    2) 轻段按 EDF 顺序填充，尽量达到阈值 C/L；
//    3) 剩余部分均匀撒入可覆盖的段，标记重/轻。
// ============================================================
struct SEntry {
    int vid;
    double remaining;
    double fsh;
    double rdy;          // original rdy for coverage test
};

static void algorithm1(DAGTask &task,
                        std::vector<Segment> &segs) {
    double threshold = (task.L > EPS) ? task.C / task.L : 1e18;

    // Build S: unassigned vertex entries, sorted by fsh ascending
    std::vector<SEntry> S;
    for (auto &[vid, v] : task.vertices)
        S.push_back({vid, v.wcet, v.fsh, v.rdy});
    std::sort(S.begin(), S.end(), [](auto &a, auto &b){
        return (a.fsh != b.fsh) ? a.fsh < b.fsh : a.vid < b.vid;
    });

    // ---------- Phase 1: assign vertices covering a single segment ----------
    auto covers_single = [&](const SEntry &e) -> int {
        int cnt = 0, idx = -1;
        for (int i = 0; i < (int)segs.size(); ++i) {
            if (e.rdy <= segs[i].start + EPS && segs[i].end <= e.fsh + EPS) {
                ++cnt; idx = i;
            }
        }
        return (cnt == 1) ? idx : -1;
    };

    std::vector<bool> remove_flag(S.size(), false);
    for (int i = 0; i < (int)S.size(); ++i) {
        int si = covers_single(S[i]);
        if (si >= 0) {
            segs[si].assigned.emplace_back(S[i].vid, S[i].remaining);
            segs[si].c += S[i].remaining;
            remove_flag[i] = true;
        }
    }
    // compact S
    {
        std::vector<SEntry> tmp;
        for (int i = 0; i < (int)S.size(); ++i)
            if (!remove_flag[i]) tmp.push_back(S[i]);
        S = std::move(tmp);
    }

    // ---------- Phase 2: fill light segments (EDF order) ----------
    for (auto &seg : segs) {
        int i = 0;
        while (i < (int)S.size()) {
            auto &e = S[i];
            // coverage check
            if (!(e.rdy <= seg.start + EPS && seg.end <= e.fsh + EPS)) {
                ++i; continue;
            }
            double cur_ratio = (seg.e > EPS) ? seg.c / seg.e : 1e18;
            if (cur_ratio >= threshold - EPS) break;   // already at threshold

            double new_ratio = (seg.e > EPS) ? (seg.c + e.remaining) / seg.e : 1e18;
            if (new_ratio < threshold - EPS) {
                // assign entirely
                seg.assigned.emplace_back(e.vid, e.remaining);
                seg.c += e.remaining;
                S.erase(S.begin() + i);
                // don't increment i
            } else {
                // split
                double space = threshold * seg.e - seg.c;
                if (space < EPS) break;
                double part   = space;
                double remain = e.remaining - part;
                if (part > EPS) {
                    seg.assigned.emplace_back(e.vid, part);
                    seg.c += part;
                }
                if (remain > EPS) {
                    e.remaining = remain;
                } else {
                    S.erase(S.begin() + i);
                }
                break;  // move to next segment
            }
        }
    }

    // ---------- Phase 3: assign remainder to heavy segments ----------
    for (auto &e : S) {
        double rem = e.remaining;
        for (auto &seg : segs) {
            if (rem <= EPS) break;
            if (!(e.rdy <= seg.start + EPS && seg.end <= e.fsh + EPS)) continue;
            double can = std::min(rem, seg.e);
            if (can > EPS) {
                seg.assigned.emplace_back(e.vid, can);
                seg.c += can;
                rem -= can;
            }
        }
    }

    // classify
    for (auto &seg : segs)
        seg.is_heavy = (seg.ratio() > threshold + EPS);
}

// ============================================================
//  Step 3: 分配松弛度 + 计算 Ω、δ̂、ℓ̂
//  重段按工作量分摊周期，轻段按区间长度分摊周期。
// ============================================================
static void laxity_distribution(DAGTask &task,
                                 DecompositionResult &res) {
    auto &segs = res.segments;

    double C_H = 0, L_L = 0;
    for (auto &s : segs) {
        if (s.is_heavy) C_H += s.c;
        else            L_L += s.e;
    }
    res.C_H = C_H;
    res.L_L = L_L;

    // Ω = C_H / C + L_L / L
    double omega = 0.0;
    if (task.C > EPS) omega += C_H / task.C;
    if (task.L > EPS) omega += L_L / task.L;
    if (omega < EPS)  omega = 1.0;
    res.omega = omega;

    // λ = ρ = Ω
    double lam = omega, rho = omega;

    for (auto &seg : segs) {
        if (seg.is_heavy) {
            // d(s^h) = c(s^h) * T / (λ * C)
            seg.d = (lam * task.C > EPS) ? seg.c * task.period / (lam * task.C)
                                          : seg.e;
        } else {
            // d(s^l) = e(s^l) * T / (ρ * L)
            seg.d = (rho * task.L > EPS) ? seg.e * task.period / (rho * task.L)
                                          : seg.e;
        }
    }

    // numerical fix: sum d should equal T
    double total_d = 0;
    for (auto &s : segs) total_d += s.d;
    if (std::abs(total_d - task.period) > 1e-6 && total_d > EPS) {
        double scale = task.period / total_d;
        for (auto &s : segs) s.d *= scale;
    }

    res.delta_hat = 0;
    res.ell_hat   = 0;
    for (auto &s : segs) {
        res.delta_hat = std::max(res.delta_hat, s.delta());
        res.ell_hat   = std::max(res.ell_hat,   s.load());
    }
}

// ============================================================
//  Step 4: 生成零散任务集
//  每个分配片段变成一个 sporadic job，记录释放/截止偏移。
// ============================================================
static void generate_sporadic_tasks(DAGTask &task,
                                     DecompositionResult &res) {
    // cumulative release offsets
    std::vector<double> seg_rel, seg_dl;
    double cum = 0.0;
    for (auto &seg : res.segments) {
        seg_rel.push_back(cum);
        seg_dl.push_back(cum + seg.d);
        cum += seg.d;
    }

    int cnt = 0;
    for (auto &seg : res.segments) {
        for (auto &[vid, w] : seg.assigned) {
            if (w < EPS) continue;
            SporadicTask st;
            std::ostringstream oss;
            oss << "t" << task.task_id << "_v" << vid
                << "_s" << seg.seg_id << "_" << cnt;
            st.task_id          = oss.str();
            st.parent_task_id   = task.task_id;
            st.vertex_id        = vid;
            st.segment_id       = seg.seg_id;
            st.wcet             = w;
            st.period           = task.period;
            st.release_offset   = seg_rel[seg.seg_id];
            st.deadline_offset  = seg_dl[seg.seg_id];
            st.relative_deadline = seg.d;
            st.density          = (seg.d > EPS) ? w / seg.d : 0.0;
            res.sporadic_tasks.push_back(st);
            ++cnt;
        }
    }
}

// ============================================================
//  Public ：单任务分解与批量分解
// ============================================================
DecompositionResult decompose_dag(DAGTask &task) {
    task.compute_parameters();

    DecompositionResult res;
    res.task_id = task.task_id;

    // 1. build segments
    res.segments = build_segments(task);

    // 2. Algorithm 1
    algorithm1(task, res.segments);

    // 3. classify (redundant but explicit)
    double thr = (task.L > EPS) ? task.C / task.L : 1e18;
    for (auto &s : res.segments)
        s.is_heavy = (s.ratio() > thr + EPS);

    // 4. laxity distribution + Ω
    laxity_distribution(task, res);

    // 5. sporadic tasks
    generate_sporadic_tasks(task, res);

    return res;
}

std::vector<DecompositionResult> decompose_taskset(std::vector<DAGTask> &tasks) {
    std::vector<DecompositionResult> results;
    for (auto &t : tasks)
        results.push_back(decompose_dag(t));
    return results;
}
