#include "wfcommons_parser.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <random>
#include <filesystem>
#include <unordered_map>

namespace fs = std::filesystem;

// ================================================================
//  load_wfinstances_file: 解析单个 WfInstances JSON 文件
//
//  支持两种格式:
//    1) WfFormat v1.x: workflow.execution.tasks[]
//    2) 旧版格式:      workflow.jobs[]
//
//  每个 task/job 包含:
//    - name: 字符串标识
//    - runtime: 执行时间（秒）
//    - parents: 前驱任务名列表
//    - children: 后继任务名列表（可选，用 parents 即可重建边）
// ================================================================
DAGTask load_wfinstances_file(const std::string &filepath, int task_id,
                               double wcet_scale) {
    DAGTask task;
    task.task_id = task_id;
    task.period = 0.0;  // 待后续分配

    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        std::cerr << "Cannot open WfInstances file: " << filepath << std::endl;
        return task;
    }

    nlohmann::json root;
    try {
        ifs >> root;
    } catch (const nlohmann::json::parse_error &e) {
        std::cerr << "JSON parse error in " << filepath << ": " << e.what() << std::endl;
        return task;
    }

    // 定位 tasks 数组：尝试多种路径
    nlohmann::json tasks_array;
    if (root.contains("workflow")) {
        auto &wf = root["workflow"];
        if (wf.contains("execution") && wf["execution"].contains("tasks")) {
            // WfFormat v1.x: workflow.execution.tasks
            tasks_array = wf["execution"]["tasks"];
        } else if (wf.contains("tasks")) {
            // 简化格式: workflow.tasks
            tasks_array = wf["tasks"];
        } else if (wf.contains("jobs")) {
            // 旧版格式: workflow.jobs
            tasks_array = wf["jobs"];
        }
    } else if (root.contains("tasks")) {
        // 顶层 tasks
        tasks_array = root["tasks"];
    } else if (root.contains("jobs")) {
        // 顶层 jobs
        tasks_array = root["jobs"];
    }

    if (tasks_array.empty() || !tasks_array.is_array()) {
        std::cerr << "No tasks/jobs found in " << filepath << std::endl;
        return task;
    }

    // 第一遍：建立 name -> vid 映射，创建顶点
    std::unordered_map<std::string, int> name_to_vid;
    int vid = 1;

    for (auto &jt : tasks_array) {
        std::string name;
        if (jt.contains("name")) {
            name = jt["name"].get<std::string>();
        } else if (jt.contains("id")) {
            name = jt["id"].get<std::string>();
        } else {
            name = "task_" + std::to_string(vid);
        }

        double runtime = 0.0;
        if (jt.contains("runtime")) {
            runtime = jt["runtime"].get<double>();
        } else if (jt.contains("runtimeInSeconds")) {
            runtime = jt["runtimeInSeconds"].get<double>();
        }

        // 跳过 runtime <= 0 的辅助任务（如 create_dir、cleanup 等）
        // 但保留映射以便建边
        double wcet = std::max(runtime * wcet_scale, 0.0);

        name_to_vid[name] = vid;
        // 即使 wcet=0 也先创建顶点，后续再清理
        task.add_vertex(vid, wcet);
        ++vid;
    }

    // 第二遍：根据 parents/children 建立边
    vid = 1;
    for (auto &jt : tasks_array) {
        // 通过 parents 建边
        if (jt.contains("parents") && jt["parents"].is_array()) {
            for (auto &p : jt["parents"]) {
                std::string pname = p.get<std::string>();
                if (name_to_vid.count(pname)) {
                    task.add_edge(name_to_vid[pname], vid);
                }
            }
        }

        // 通过 children 建边（如果 parents 不存在时的备选）
        if (jt.contains("children") && jt["children"].is_array()) {
            for (auto &c : jt["children"]) {
                std::string cname = c.get<std::string>();
                if (name_to_vid.count(cname)) {
                    int cvid = name_to_vid[cname];
                    // 避免重复边：检查是否已存在
                    auto &succs = task.vertices[vid].succs;
                    if (std::find(succs.begin(), succs.end(), cvid) == succs.end()) {
                        task.add_edge(vid, cvid);
                    }
                }
            }
        }

        ++vid;
    }

    // 清理 wcet=0 的顶点（穿透连接，类似 STG 的 dummy 处理）
    std::vector<int> zero_vids;
    for (auto &[v, vtx] : task.vertices) {
        if (vtx.wcet < EPS) zero_vids.push_back(v);
    }

    for (int zv : zero_vids) {
        auto &zvtx = task.vertices[zv];
        // 将 zero 顶点的前驱直接连接到后继
        for (int pred : zvtx.preds) {
            for (int succ : zvtx.succs) {
                auto &pred_succs = task.vertices[pred].succs;
                if (std::find(pred_succs.begin(), pred_succs.end(), succ) == pred_succs.end()) {
                    task.add_edge(pred, succ);
                }
            }
            // 从前驱的后继列表中移除 zero 顶点
            auto &ps = task.vertices[pred].succs;
            ps.erase(std::remove(ps.begin(), ps.end(), zv), ps.end());
        }
        for (int succ : zvtx.succs) {
            // 从后继的前驱列表中移除 zero 顶点
            auto &sp = task.vertices[succ].preds;
            sp.erase(std::remove(sp.begin(), sp.end(), zv), sp.end());
            // 如果前驱列表为空且 zero 顶点有前驱，已在上面添加
        }
        task.vertices.erase(zv);
    }

    if (task.vertices.empty()) {
        std::cerr << "Warning: No real vertices loaded from " << filepath << std::endl;
    }

    return task;
}

// ================================================================
//  load_wfinstances_directory: 从目录批量加载 WfInstances JSON 文件
// ================================================================
std::vector<DAGTask> load_wfinstances_directory(const std::string &dirpath,
                                                 double util_norm,
                                                 int max_files,
                                                 unsigned seed,
                                                 double wcet_scale) {
    std::vector<DAGTask> tasks;
    std::vector<std::string> json_files;

    if (!fs::exists(dirpath) || !fs::is_directory(dirpath)) {
        std::cerr << "WfInstances directory not found: " << dirpath << std::endl;
        return tasks;
    }

    // 收集 .json 文件（递归搜索子目录）
    for (auto &entry : fs::recursive_directory_iterator(dirpath)) {
        if (entry.path().extension() == ".json") {
            json_files.push_back(entry.path().string());
        }
    }

    std::sort(json_files.begin(), json_files.end());

    int count = std::min(max_files, (int)json_files.size());
    std::mt19937 rng(seed);

    for (int i = 0; i < count; ++i) {
        DAGTask t = load_wfinstances_file(json_files[i], i, wcet_scale);
        if (t.vertices.empty()) continue;
        assign_period(t, util_norm, rng(), 3.0);
        tasks.push_back(t);
    }

    return tasks;
}
