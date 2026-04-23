// STG 解析器单元测试
// 依赖真实数据 data/stg/{50,100,300}/*.stg（若不存在则相关断言被跳过）
#include "test_utils.h"
#include "stg_parser.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace fs = std::filesystem;

// 在多个候选路径里寻找 STG 数据根目录，支持从 repo root 或 out/ 运行测试。
static std::string find_stg_base() {
    const char *env = std::getenv("STG_BASE_DIR");
    if (env && fs::exists(env)) return env;
    for (const std::string &p : {
             std::string("data/stg"),
             std::string("../data/stg"),
             std::string("../../data/stg")
         }) {
        if (fs::exists(p) && fs::is_directory(p)) return p;
    }
    return "";
}

int main() {
    TestCtx ctx;
    std::string base = find_stg_base();
    bool has_data = !base.empty();

    // ============= 1) load_stg_file: 最基础的文件解析 =============
    if (has_data) {
        std::string sample = base + "/50/rand0000.stg";
        if (fs::exists(sample)) {
            DAGTask t = load_stg_file(sample, 7);
            ctx.expect(t.task_id == 7, "load_stg_file: task_id propagated");
            ctx.expect(!t.vertices.empty(),
                       "load_stg_file: non-empty vertices for 50-node DAG");
            // STG 文件首行 N=50，除去入口/出口 dummy 后真实顶点通常接近 50
            ctx.expect((int)t.vertices.size() >= 40 && (int)t.vertices.size() <= 52,
                       "load_stg_file: vertex count in expected range [40,52]");
            // 每个真实顶点 wcet > 0
            bool all_wcet_pos = true;
            for (auto &[vid, v] : t.vertices)
                if (v.wcet <= 0) { all_wcet_pos = false; break; }
            ctx.expect(all_wcet_pos, "load_stg_file: all real vertices have wcet>0");

            // 解析完成时 period=0（需要 assign_period 才生效）
            ctx.expect(t.period == 0.0, "load_stg_file: period unset before assign");

            // 调用 compute_parameters 后应得到合法的 C/L
            t.compute_parameters();
            ctx.expect(t.C > 0, "load_stg_file: positive total wcet");
            ctx.expect(t.L > 0 && t.L <= t.C,
                       "load_stg_file: critical path L in (0, C]");
        }
    }

    // ============= 2) 人造最小 STG 文件，验证拓扑 =============
    {
        std::string tmp = "/tmp/stg_parser_test_mini.stg";
        // 4 节点菱形: 0(dummy) -> 1 -> 2,3 -> 4(dummy)
        // 1: wcet=2, 2: wcet=3, 3: wcet=1
        {
            std::ofstream ofs(tmp);
            ofs << "5\n"
                << "0 0 0\n"
                << "1 2 1 0\n"
                << "2 3 1 1\n"
                << "3 1 1 1\n"
                << "4 0 2 2 3\n";
        }
        DAGTask t = load_stg_file(tmp, 0);
        ctx.expect(t.vertices.size() == 3,
                   "mini STG: 3 real vertices (dummies excluded)");
        ctx.expect(t.vertices.count(1) && t.vertices.count(2) && t.vertices.count(3),
                   "mini STG: vertices 1/2/3 present");
        ctx.expect(t.vertices.at(1).wcet == 2.0 &&
                   t.vertices.at(2).wcet == 3.0 &&
                   t.vertices.at(3).wcet == 1.0,
                   "mini STG: wcet values parsed correctly");

        // 1 -> 2, 1 -> 3 两条边
        auto &v1 = t.vertices.at(1);
        ctx.expect(v1.succs.size() == 2,
                   "mini STG: vertex 1 has 2 successors");
        ctx.expect(t.vertices.at(2).preds.size() == 1 &&
                   t.vertices.at(2).preds[0] == 1,
                   "mini STG: vertex 2 pred = 1");
        ctx.expect(t.vertices.at(3).preds.size() == 1 &&
                   t.vertices.at(3).preds[0] == 1,
                   "mini STG: vertex 3 pred = 1");

        t.compute_parameters();
        ctx.expect(std::fabs(t.C - 6.0) < 1e-9,
                   "mini STG: C = 2+3+1 = 6");
        // 关键路径 1->2 = 5 (2+3)
        ctx.expect(std::fabs(t.L - 5.0) < 1e-9,
                   "mini STG: L = 5 (critical path 1->2)");
        std::remove(tmp.c_str());
    }

    // ============= 3) load_stg_directory: 限量 & 全量 =============
    if (has_data && fs::exists(base + "/50")) {
        // 限量 5
        auto tasks5 = load_stg_directory(base + "/50", 0.4, 5, 42);
        ctx.expect((int)tasks5.size() == 5,
                   "load_stg_directory: max_files=5 returns exactly 5 tasks");
        // assign_period 应让每个 task.period > L 且 U > 0
        bool ok = true;
        for (auto &t : tasks5) {
            if (!(t.period > t.L - 1e-9 && t.U > 0 && t.U <= 1.0 + 1e-6)) {
                ok = false; break;
            }
        }
        ctx.expect(ok, "load_stg_directory: all tasks have period>=L and U in (0,1]");

        // 全量 (max_files = -1)
        auto tasks_all = load_stg_directory(base + "/50", 0.4, -1, 42);
        ctx.expect((int)tasks_all.size() > 100,
                   "load_stg_directory: max_files=-1 loads >100 files (data has 180)");
    }

    // ============= 4) load_stg_auto: 自动识别子目录 =============
    if (has_data) {
        auto groups = load_stg_auto(base, 3, 42);
        // 期待至少 2 组（50 与 100，300 可能存在）
        ctx.expect(groups.size() >= 2,
                   "load_stg_auto: finds >=2 groups under data/stg");
        // 每组至多 3 个任务
        for (auto &g : groups) {
            ctx.expect((int)g.tasks.size() <= 3,
                       "load_stg_auto: max_files_per_group=3 honored for " + g.label);
            ctx.expect(!g.label.empty(),
                       "load_stg_auto: non-empty group label");
            for (auto &t : g.tasks)
                ctx.expect(!t.vertices.empty(),
                           "load_stg_auto: task in group " + g.label + " non-empty");
        }

        // label 应包含 "50" 与 "100"（至少两个已知分组）
        bool has50 = false, has100 = false;
        for (auto &g : groups) {
            if (g.label == "50")  has50  = true;
            if (g.label == "100") has100 = true;
        }
        ctx.expect(has50 && has100,
                   "load_stg_auto: labels include both '50' and '100'");
    }

    // ============= 5) assign_period: U/m 目标 =============
    {
        // 用 mini 文件测 assign_period
        std::string tmp2 = "/tmp/stg_parser_test_assign.stg";
        {
            std::ofstream ofs(tmp2);
            ofs << "4\n"
                << "0 0 0\n"
                << "1 10 1 0\n"
                << "2 10 1 1\n"
                << "3 0 1 2\n";  
        }
        DAGTask t = load_stg_file(tmp2, 0);
        assign_period(t, 0.4, 123, 3.0);
        ctx.expect(t.period > t.L - 1e-9,
                   "assign_period: T >= L");
        ctx.expect(t.U > 0 && t.U < 1.0,
                   "assign_period: U in (0, 1)");
        // 公式 T = (L + C/(k·U_norm))·(1 + 0.25·γ)，
        // 随机性有限，期望 U 大致在 0.1~0.9 之间
        ctx.expect(t.U > 0.05 && t.U < 0.95,
                   "assign_period: U in plausible range");
        std::remove(tmp2.c_str());
    }

    if (!has_data) {
        std::cerr << "[WARN] data/stg not found; some tests skipped.\n";
    }

    print_summary("stg_parser", ctx);
    return ctx.exit_code();
}
