package(default_visibility = ["//visibility:public"])

# ======================== 头文件库 ========================

# nlohmann/json 单头文件库
cc_library(
    name = "json",
    hdrs = ["include/json.hpp"],
    includes = ["include"],
)

# DAG 数据模型（纯头文件）
cc_library(
    name = "dag_model",
    hdrs = ["include/dag_model.h"],
    includes = ["include"],
)

# 顶点重组（纯头文件）
cc_library(
    name = "vertex_reassemble",
    hdrs = ["include/vertex_reassemble.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

# ======================== 编译库 ========================

# 分解算法
cc_library(
    name = "decomposition",
    srcs = ["src/decomposition.cpp"],
    hdrs = ["include/decomposition.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

# DAG 生成器
cc_library(
    name = "dag_generators",
    srcs = ["src/dag_generators.cpp"],
    hdrs = ["include/dag_generators.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

# STG 解析器
cc_library(
    name = "stg_parser",
    srcs = ["src/stg_parser.cpp"],
    hdrs = ["include/stg_parser.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

# WfCommons/WfInstances 解析器
cc_library(
    name = "wfcommons_parser",
    srcs = ["src/wfcommons_parser.cpp"],
    hdrs = ["include/wfcommons_parser.h"],
    includes = ["include"],
    deps = [
        ":dag_model",
        ":json",
        ":stg_parser",
    ],
)

# GEDF 仿真器
cc_library(
    name = "gedf_simulator",
    srcs = ["src/gedf_simulator.cpp"],
    hdrs = ["include/gedf_simulator.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

# GEDF 变体（NP、DS、R、RDS + 开销 + 精度评估）
cc_library(
    name = "gedf_variants",
    srcs = ["src/gedf_variants.cpp"],
    hdrs = ["include/gedf_variants.h"],
    includes = ["include"],
    deps = [
        ":dag_model",
        ":gedf_simulator",
        ":vertex_reassemble",
    ],
)

# ======================== 主程序 ========================

cc_binary(
    name = "dag_simulator",
    srcs = ["src/main.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_simulator",
        ":gedf_variants",
        ":vertex_reassemble",
        ":dag_generators",
        ":json",
        ":stg_parser",
        ":wfcommons_parser",
    ],
)

# ======================== 工具 ========================

cc_binary(
    name = "gen_tool",
    srcs = ["utils/gen_tool.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_generators",
        ":json",
    ],
)

# ======================== 测试 ========================

# 测试工具头文件
cc_library(
    name = "test_utils",
    hdrs = ["tests/test_utils.h"],
    includes = ["include"],
    deps = [":dag_model"],
)

cc_test(
    name = "dag_model_test",
    srcs = ["tests/dag_model_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":test_utils",
    ],
)

cc_test(
    name = "decomposition_test",
    srcs = ["tests/decomposition_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":dag_generators",
        ":test_utils",
    ],
)

cc_test(
    name = "gedf_simulator_test",
    srcs = ["tests/gedf_simulator_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_simulator",
        ":dag_generators",
        ":test_utils",
    ],
)

cc_test(
    name = "gedf_variants_test",
    srcs = ["tests/gedf_variants_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_simulator",
        ":gedf_variants",
        ":dag_generators",
        ":vertex_reassemble",
        ":test_utils",
    ],
)

cc_test(
    name = "overhead_test",
    srcs = ["tests/overhead_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_variants",
        ":dag_generators",
        ":test_utils",
    ],
)

cc_test(
    name = "precision_test",
    srcs = ["tests/precision_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_simulator",
        ":gedf_variants",
        ":dag_generators",
        ":test_utils",
    ],
)

cc_test(
    name = "dag_generators_test",
    srcs = ["tests/dag_generators_test.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_generators",
        ":test_utils",
    ],
)

cc_test(
    name = "sample_graphs_test",
    srcs = ["tests/sample_graphs.cpp"],
    includes = ["include"],
    copts = ["-std=c++17"],
    deps = [
        ":dag_model",
        ":decomposition",
        ":gedf_simulator",
        ":dag_generators",
        ":json",
        ":test_utils",
    ],
)
