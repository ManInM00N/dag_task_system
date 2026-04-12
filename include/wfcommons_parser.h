#pragma once
#include "dag_model.h"
#include "stg_parser.h"  // assign_period()
#include <string>
#include <vector>

/*
 * WfCommons / WfInstances 解析器
 * 数据来源: https://github.com/wfcommons/WfInstances
 *
 * WfInstances 提供来自真实科学工作流的 JSON 格式 DAG 实例，
 * 涵盖 Montage、Epigenomics、SIPHT、CyberShake、LIGO 等多种工作流。
 *
 * WfCommons JSON 格式 (WfFormat):
 *   {
 *     "name": "...",
 *     "schemaVersion": "1.x",
 *     "workflow": {
 *       "specification": { ... },
 *       "execution": {
 *         "makespanInSeconds": ...,
 *         "tasks": [
 *           {
 *             "name": "task_id_string",
 *             "type": "compute",
 *             "runtime": 12.34,        // 秒
 *             "parents": ["parent_task_name", ...],
 *             "children": ["child_task_name", ...],
 *             "files": [ ... ],
 *             "machines": [ ... ]
 *           },
 *           ...
 *         ]
 *       }
 *     }
 *   }
 *
 * 也支持旧版 WfCommons 格式:
 *   {
 *     "workflow": {
 *       "jobs": [
 *         {
 *           "name": "...",
 *           "runtime": ...,
 *           "parents": [...],
 *           "children": [...]
 *         }
 *       ]
 *     }
 *   }
 *
 * 解析策略:
 *   - 将每个 task/job 映射为 DAG 顶点
 *   - runtime 作为 WCET（单位：秒，可按需缩放）
 *   - parents/children 建立 DAG 边
 *   - 返回的 DAGTask 中 period=0，需后续调用 assign_period()
 */

// 从单个 WfInstances JSON 文件加载一个 DAG（仅拓扑 + WCET，不含周期）
// task_id: 分配给该 DAG 任务的编号
// wcet_scale: WCET 缩放因子（默认 1.0，即保持原始秒数）
// 返回的 DAGTask 中 period=0，需要后续调用 assign_period()
DAGTask load_wfinstances_file(const std::string &filepath, int task_id,
                               double wcet_scale = 1.0);

// 从一个目录批量加载 WfInstances JSON 文件
// 选取前 max_files 个 .json 文件，按 util_norm 分配周期
std::vector<DAGTask> load_wfinstances_directory(const std::string &dirpath,
                                                 double util_norm,
                                                 int max_files = 10,
                                                 unsigned seed = 42,
                                                 double wcet_scale = 1.0);
