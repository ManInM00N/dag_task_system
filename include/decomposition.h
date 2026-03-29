#pragma once
#include "dag_model.h"

// 单个 DAG 的全流程分解：生成段、分配顶点、分配松弛度、生成零散任务
DecompositionResult decompose_dag(DAGTask &task);

// 批量分解任务集合，返回每个任务的分解结果
std::vector<DecompositionResult> decompose_taskset(std::vector<DAGTask> &tasks);
