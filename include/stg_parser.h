#pragma once
#include "dag_model.h"
#include <string>
#include <vector>

/*
 * STG (Standard Task Graph Set) 解析器
 * 数据来源: 早稻田大学笠原实验室
 * https://www.kasahara.cs.waseda.ac.jp/schedule/
 *
 * STG 文件只提供 DAG 拓扑 + 顶点 WCET，不含周期/截止时间。
 * 实时调度论文的标准做法是从数据集读取 DAG 结构，
 * 再按目标利用率合成周期 T（隐式截止期 D = T）。
 *
 * STG 格式（无通信开销版本）:
 *   第 1 行: N（顶点数，不含 dummy 入口/出口）
 *   后续行: task_id  wcet  num_preds  pred1  pred2  ...
 *   - 第一个节点(id=0)为入口 dummy（wcet=0），最后一个为出口 dummy
 */

// 从 STG 文件加载一个 DAG（仅拓扑 + WCET，不含周期）
// task_id: 分配给该 DAG 任务的编号
// 返回的 DAGTask 中 period=0，需要后续调用 assign_period()
DAGTask load_stg_file(const std::string &filepath, int task_id);

// 为已加载的 DAG 分配周期（隐式截止期 D = T）
// 使用论文公式 (30): T = ⌈L + C/(k·U_norm)⌉ × (1 + 0.25·Gamma(2,1))
// k=3 对应正常弹性，k=60 对应高弹性（Γ接近1）
// seed 用于 Gamma 分布随机性
void assign_period(DAGTask &task, double util_norm,
                   unsigned seed, double k_factor = 3.0);

// 从一个目录批量加载 STG 文件
// 选取前 max_files 个 .stg 文件，按 util_norm 分配周期
// max_files < 0 表示"不限数量，全部加载"
std::vector<DAGTask> load_stg_directory(const std::string &dirpath,
                                         double util_norm,
                                         int max_files = 10,
                                         unsigned seed = 42);

// 从一个 .tgz 压缩包的解压目录中加载指定规模的 STG
// size: 50, 100, 300, 500, ... 对应 STG 的顶点规模
std::vector<DAGTask> load_stg_by_size(const std::string &base_dir,
                                       int size,
                                       double util_norm,
                                       int max_files = 5,
                                       unsigned seed = 42);

// 自动探测 STG 数据布局并加载：
//   - 若 base_dir 自身含 .stg 文件，直接加载
//   - 否则扫描 base_dir 下所有子目录（含子目录中的 .stg）
//     典型布局：base_dir/50/*.stg、base_dir/100/*.stg、base_dir/rnc50/*.stg
// 返回按目录分组的结果（分组名即目录名，如 "50"、"100"）。
struct StgGroup {
    std::string label;          // 目录名或 "root"
    std::vector<DAGTask> tasks; // 该目录下加载的 DAG（period=0，待 assign_period）
};

std::vector<StgGroup> load_stg_auto(const std::string &base_dir,
                                     int max_files_per_group = -1,
                                     unsigned seed = 42);