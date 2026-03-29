#pragma once
#include "dag_model.h"

// 论文示例（Fig.1）：8 个顶点，周期 T=25
DAGTask create_paper_example();

// 随机 DAG 生成：Erdős–Rényi G(n, p)
DAGTask generate_erdos_renyi_dag(int task_id, int n_vertices, double p,
                                  double wcet_min, double wcet_max,
                                  double util_norm, unsigned seed);

// 生成任务集合：按给定顶点数区间与目标利用率生成若干 DAG 任务
std::vector<DAGTask> generate_taskset(int n_tasks, int m,
                                       int nv_min, int nv_max,
                                       double p, double util_norm,
                                       double wcet_min, double wcet_max,
                                       unsigned seed);
