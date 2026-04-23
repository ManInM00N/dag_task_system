# DAG 分解与实时调度仿真系统

基于 DAG（有向无环图）任务模型的实时调度分解与仿真系统。本项目实现了：
- Algorithm 1 的 DAG 分解（段划分 + 松弛度分配 + 零散任务生成）；
- GEDF 及其变体（**NP / DS / R / RDS**）的**理论可调度性判定**；
- 事件驱动 GEDF / GEDF-NP / GEDF-DS **仿真器**（显式维护 DAG 前驱依赖）；
- Ω·T / Graham / Period 三种 WCRT 上界下的**精度评估**；
- STG 与 WfCommons/WfInstances 真实任务图数据集的导入与验证。

> 项目同时提供 **Bazel** 与 **GNU Make** 两套构建入口，两者结果一致。

---

## 目录

1. [项目结构](#项目结构)
2. [环境要求](#环境要求)
3. [从零配置环境](#从零配置环境)
4. [构建](#构建)
5. [运行与 CLI 参数](#运行与-cli-参数)
6. [实验模块一览](#实验模块一览)
7. [支持的数据格式与下载](#支持的数据格式与下载)
8. [输出 JSON 结构](#输出-json-结构)
9. [单元测试](#单元测试)
10. [常见问题](#常见问题)

---

## 项目结构

```
dag_task_system/
├── WORKSPACE / BUILD / .bazelrc    # Bazel 构建入口
├── Makefile                        # Make 构建入口（输出目录为 out/）
├── include/                        # 公共头文件
│   ├── dag_model.h                 #   DAG 数据模型：Vertex / DAGTask / Segment / SporadicTask
│   ├── decomposition.h             #   Algorithm 1 分解接口
│   ├── gedf_simulator.h            #   GEDF 仿真 + 基线理论判定
│   ├── gedf_variants.h             #   NP / DS / R / RDS 判定 + 开销叠加 + 精度评估
│   ├── vertex_reassemble.h         #   顶点重组（GEDF-NP 专用）
│   ├── dag_generators.h            #   随机 DAG 生成器
│   ├── stg_parser.h                #   STG 数据集解析
│   ├── wfcommons_parser.h          #   WfCommons/WfInstances JSON 解析
│   └── json.hpp                    #   nlohmann/json 单头文件
├── src/                            # 对应实现
├── tests/                          # 8 个单元测试 + sample_graphs
├── utils/gen_tool.cpp              # DAG 生成 CLI 工具
└── plot_results.py                 # 实验结果可视化脚本（可选）
```

---

## 环境要求

| 类别 | 最低要求 | 推荐 |
|---|---|---|
| 编译器 | GCC 7 / Clang 10 | GCC 11+ / Clang 15+ |
| C++ 标准 | C++17 | C++17 |
| 构建工具 | GNU Make 3.8 | Bazel 6.x + Bazelisk |
| OS | Linux / macOS / WSL2 | Ubuntu 22.04 / macOS 13+ |
| Python（绘图可选） | 3.8+ | 3.10+ 含 `matplotlib numpy` |



---

## 从零配置环境

下面给出 Ubuntu / macOS 两个典型环境的一键配置命令。

### Ubuntu / Debian / WSL2

```bash
# 1. 基础工具链
sudo apt update
sudo apt install -y build-essential g++ make git python3 python3-pip

# 2.（可选）绘图依赖
pip3 install --user matplotlib numpy

# 3.（可选）Bazel via Bazelisk
sudo curl -fL https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-amd64 \
     -o /usr/local/bin/bazel && sudo chmod +x /usr/local/bin/bazel
bazel --version

# 4. 拉取本项目
git clone git@github.com:ManInM00N/dag_task_system.git
cd dag_task_system
```

### macOS（Apple Silicon / Intel）

```bash
# 1. Xcode Command Line Tools（含 clang / make / git）
xcode-select --install

# 2. Homebrew（若尚未安装）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 3.（可选）Bazelisk、Python
brew install bazelisk python@3.11
pip3 install matplotlib numpy

# 4. 拉取项目
git clone git@github.com:ManInM00N/dag_task_system.git
cd dag_task_system
```


---

## 构建

### 方式 A：GNU Make

```bash
# 1. 构建主程序（产物：out/dag_simulator）
make

# 2. 构建所有单元测试（产物：out/<test_name>）
make tests

# 3. 构建 + 运行主程序（不带 STG 数据，输出到 output/results.json）
make run

# 4. 构建 + 运行完整实验（含 STG 数据自动加载 data/stg/）
make run-stg

# 5. 仅基于已有 output/results.json 绘图
make plot-only

# 6. 构建 → 完整实验（含 STG）→ 绘图到 output/plots/
make plot

# 7. 清理
make clean
```
### 方式 B：Bazel

```bash
# 构建主程序
bazel build //:dag_simulator           # 默认 fastbuild
bazel build --config=opt //:dag_simulator   # -O2 优化
bazel build --config=dbg //:dag_simulator   # -g 调试

# 运行所有测试
bazel test //...

# 构建 DAG 生成工具
bazel build //:gen_tool
```

构建产物：
- Make：`out/dag_simulator`、`out/<test_name>`
- Bazel：`bazel-bin/dag_simulator`、`bazel-bin/<test_name>`

---

## 运行与 CLI 参数

### 主程序

```bash
# Make 构建产物
./out/dag_simulator [options] [output.json]

# Bazel 方式
bazel run //:dag_simulator -- [options]
```

| 参数 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `-o <path>` | string | `output/results.json` | 指定实验结果 JSON 的输出路径 |
| `--stg <dir>` | string | *未设置* | 启用 **Part 8**：STG 数据根目录；**自动探测布局**——目录下直接含 `*.stg` 即作为单组，否则遍历一级子目录（如 `50/`、`100/`、`rnc50/`）各自作为一组 |
| `--stg-max <N>` | int | `-1`（全量） | 每个 STG 分组加载的最大文件数（例如 180 个 `.stg` 只加载前 `N` 个） |
| `--stg-sizes <list>` | csv | *全部* | 仅保留指定分组（逗号分隔的目录名），例如 `--stg-sizes 50,100` |
| `--wf <path>` | string | *未设置* | 启用 **Part 9**：WfCommons JSON，可以是单文件或目录（递归扫描 `.json`） |
| `<positional>` | string | — | 首个未识别的位置参数也被视为输出路径（兼容旧调用） |

#### 运行示例

```bash
# 1. 最小运行：只做内置实验（Part 1–7）
./out/dag_simulator -o output/results.json

# 2. 加上 STG 数据集验证（Part 8）——自动识别 data/stg 下的 50/、100/ 子目录
./out/dag_simulator --stg data/stg

# 2b. 仅加载 50 顶点那组，每组最多 20 个文件（快速探路）
./out/dag_simulator --stg data/stg --stg-sizes 50 --stg-max 20

# 3. 加上 WfInstances 数据集验证（Part 9）
./out/dag_simulator --wf data/wfinstances/montage

# 4. 全量运行（三种实验一起做）
./out/dag_simulator --stg data/stg --wf data/wfinstances -o output/full.json

# 5. Bazel 方式（注意 -- 之后才是程序参数）
bazel run --config=opt //:dag_simulator -- \
    --stg "$(pwd)/data/stg" \
    --wf  "$(pwd)/data/wfinstances/montage"
```

### DAG 生成工具 `gen_tool`

```bash
./out/gen_tool <out.json> <n_vertices> <p_edge> <wcet_min> <wcet_max> <util_norm> <seed> [task_id]
```

| 参数 | 含义 |
|---|---|
| `n_vertices` | 顶点数（≥ 3，建议 10–200） |
| `p_edge` | Erdős–Rényi 边概率 ∈ [0, 1]，建议 0.1–0.3 |
| `wcet_min / wcet_max` | 每个顶点 WCET 的采样区间（µs 或任意单位） |
| `util_norm` | 归一化利用率 U/m，用于反推周期 |
| `seed` | 随机种子（保证可复现） |
| `task_id` | 可选，默认 0 |

示例：
```bash
./out/gen_tool dag.json 30 0.15 10 50 0.4 42
```

---

## 实验模块一览

主程序依次执行以下 9 个模块：

| Part | 名称 | 触发条件 | 关键输出 |
|---|---|---|---|
| 1 | Paper Example (Fig. 1) | 总是运行 | 单任务完整分解 + 仿真时序 |
| 2 | Random Task Set | 总是运行 | 4 任务集的 Ω、WCRT、m_required |
| 3 | Acceptance Ratio vs U/m | 总是运行 | U/m ∈ {0.1…0.9} 下的验收率曲线 |
| 4 | Acceptance Ratio vs m | 总是运行 | m ∈ {2,4,6,8,12,16} 的验收率 |
| 5 | GEDF / NP / DS 对比 | 总是运行 | 三种变体在不同 U/m 下的 ana / sim 结果 |
| 6 | High-Elasticity + Overhead | 总是运行 | 高弹性任务验收率 + 开销放大下的各算法对比 |
| 7 | Precision Evaluation | 总是运行 | WCRT_sim / RT_bound 的三种精度统计 |
| 8 | STG Dataset | `--stg` 设置时 | 对真实 STG DAG 在多利用率下做判定与仿真 |
| 9 | WfInstances Dataset | `--wf` 设置时 | 对 WfCommons 科学工作流做判定与仿真 |

---

## 支持的数据格式与下载

### 1. STG（Standard Task Graph Set，早稻田大学）

- 下载：<https://www.kasahara.cs.waseda.ac.jp/schedule/>
- 推荐子集：`Random Task Graphs for Fixed Number of Tasks (no comm)`，选 50、100、300 节点的版本。
- **目录布局（示例）**：
  ```
  data/stg/
  ├── 50/
  │   ├── rand0000.stg   # 50 顶点规模的 DAG
  │   ├── rand0001.stg
  │   └── ... 共 180 个
  └── 100/
      ├── rand0000.stg   # 100 顶点规模的 DAG
      └── ... 共 180 个
  ```
- 使用方式：`--stg data/stg` 自动识别 `50/` 与 `100/` 两组；也可 `--stg-sizes 50` 指定只用 50 顶点组。

STG 文件格式（无通信版本）：

```
10                          # 顶点数（含 dummy 入/出口）
0   0   0                   # id  wcet  #preds
1   20  1   0               # id  wcet  #preds  preds...
2   15  1   0
3   10  2   1   2
...
```

### 2. WfCommons / WfInstances（JSON）

- 下载：<https://github.com/wfcommons/WfInstances>
- 推荐实例：`montage`、`epigenomics`、`cycles`、`seismology`，文件后缀 `.json`。
- 同时支持 WfFormat v1.x 与旧版 `workflow.jobs` 格式。
- 使用方式：
  - 单文件：`--wf data/wfinstances/montage/montage-chameleon-small.json`
  - 目录（递归扫 `.json`）：`--wf data/wfinstances`

WfFormat v1.x 示意（节选）：

```json
{
  "schemaVersion": "1.5",
  "workflow": {
    "execution": {
      "tasks": [
        { "name": "mProjectPP_0", "runtime": 12.34,
          "parents": [], "children": ["mDiffFit_0"] },
        { "name": "mDiffFit_0",  "runtime":  5.67,
          "parents": ["mProjectPP_0"], "children": ["mConcatFit_0"] }
      ]
    }
  }
}
```

旧版格式：

```json
{ "workflow": { "jobs": [
    { "name": "task_0", "runtime": 10.5, "parents": [], "children": ["task_1"] }
]}}
```

### 3. 内置随机生成（无需数据）

Part 1–7 全部使用 Erdős–Rényi `G(n, p)` 随机 DAG，可直接运行，无需外部文件。

---

## 输出 JSON 结构

```jsonc
{
  "paper_example":            { "task": {...}, "decomposition": {...}, "simulation": {...}, "analytical": {...} },
  "random_taskset":           { "tasks": [...], "decomps": [...], "sim": {...} },
  "acceptance_ratio_vs_util": [ { "util_norm": 0.1, "analytical_ratio": 1.0, "simulation_ratio": 1.0, ... }, ... ],
  "acceptance_ratio_vs_processors": [ ... ],
  "variants_comparison":      [ ... ],
  "high_elasticity":          [ ... ],
  "overhead_comparison":      [ ... ],
  "precision_evaluation": {
    "per_trial": [ ... ],
    "summary": {
      "period":  { "avg": 0.95, "min": 0.87, "max": 0.99, "n": 120 },
      "omega":   { "avg": 0.95, "min": 0.87, "max": 0.99, "n": 120 },
      "graham":  { "avg": 4.31, "min": 2.43, "max": 7.87, "n": 120 }
    }
  },
  "stg_experiment":           { "num_dags": N, "by_util": [ ... ] },
  "wfinstances_experiment":   { "num_dags": N, "by_util": [ ... ] }
}
```

> 若 `--stg` / `--wf` 未启用或未找到数据，对应字段为 `{ "status": "no_data" }`。

---

## 可视化脚本

仓库根目录提供 [`plot_results.py`](plot_results.py)，根据 `output/results.json` 生成 10 张图：

```bash
# 依赖
pip3 install --user numpy matplotlib

# 绘图（默认读 output/results.json，输出到 output/plots/）
python3 plot_results.py

# 指定输入输出
python3 plot_results.py output/stg_full.json output/plots

# 或通过 Makefile 一键出图
make plot         # 跑完整实验 + 绘图
make plot-only    # 仅基于已有 JSON 绘图
```

生成文件对照：

| 文件 | 对应实验 |
|---|---|
| `acceptance_vs_util.png`        | Part 3：U/m 扫描验收率 |
| `acceptance_vs_processors.png`  | Part 4：m 扫描验收率 |
| `variants_comparison.png`       | Part 5：GEDF / NP / DS / R / RDS 对比（左 ana、右 sim 两子图） |
| `high_elasticity.png`           | Part 6a：高弹性任务 D-XU vs D-XU-DS |
| `overhead_comparison.png`       | Part 6b：开销敏感（横轴 log scale） |
| `precision_summary.png`         | Part 7：三路精度上界直方图 + 80% 目标线 |
| `stg_<label>.png`               | Part 8 的每个 STG 分组（左 5-变体仿真、右 Ω·T 精度曲线） |
| `stg_precision_bars.png`        | Part 8 所有分组的精度汇总 |
| `wfinstances.png`               | Part 9（若启用 `--wf`） |

---

## 单元测试

```bash
make tests                                      # 构建全部测试
for t in dag_model_test decomposition_test \
         gedf_simulator_test gedf_variants_test \
         overhead_test precision_test \
         dag_generators_test stg_parser_test; do
    ./out/$t || exit 1
done
```

或使用 Bazel 一键跑：

```bash
bazel test //...
```

当前共 **92 个**（54 核心 + 38 STG 解析器），覆盖：
- DAG 模型（拓扑、`rdy`/`fsh`/C/L/U/Γ）
- 分解算法（段边界、Ω、零散任务生成、`delta_hat`/`ell_hat`）
- GEDF 仿真（多周期 + DAG 依赖约束 + 菱形 DAG 串行正确性）
- GEDF-DS / GEDF-NP / **GEDF-R / GEDF-RDS** 理论判定与仿真
- 开销叠加（基础 + `apply_overhead_sync` 同步更新 DAG 参数）
- 精度评估（Period / Ω·T / Graham 三种上界）
- 随机 DAG 生成器（可复现、连通、利用率归一）
- **STG 解析器**：真实 `.stg` 读取 / 人造最小样本 / `load_stg_auto` 分组识别 / `assign_period` 周期分配
---

### 1.全量测试
bazelisk test //...

### 2.只重跑某个模块（调试某 cpp 时）
bazelisk test //:gedf_variants_test --test_output=all

### 3. Make 路径 —— 无 bazel 环境时的回退
for t in out/*_test; do "$t" || echo "FAIL: $t"; done

### 4. 跑完测试 + 跑完整实验 + 生成图表
make plot 


## 第三方依赖

- [nlohmann/json](https://github.com/nlohmann/json) v3.x
- [`include/json.hpp`](include/json.hpp)。

---
