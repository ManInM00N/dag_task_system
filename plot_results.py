#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_results.py — 读取 output/results.json 并生成所有实验对应的图。

使用：
    python3 plot_results.py [results.json [out_dir]]

默认参数：
    results.json = output/results.json
    out_dir      = 同目录下的 plots/

输出：
    out_dir/
        acceptance_vs_util.png           Part 3
        acceptance_vs_processors.png     Part 4
        variants_comparison.png          Part 5
        high_elasticity.png              Part 6a
        overhead_comparison.png          Part 6b
        precision_summary.png            Part 7
        stg_<label>.png                  Part 8（每个 STG 分组一张）
        stg_precision_bars.png           Part 8 精度汇总
        wfinstances.png                  Part 9（若存在）
"""

from __future__ import annotations
import json
import os
import sys
from pathlib import Path

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "figure.figsize": (8.5, 5.5),
    "figure.dpi": 110,
    "axes.grid": True,
    "grid.alpha": 0.3,
    "axes.axisbelow": True,
    "legend.frameon": False,
    "font.family": "DejaVu Sans",
})


# ----------------------------------------------------------------------
# 帮助函数
# ----------------------------------------------------------------------

def _save(fig: plt.Figure, out_dir: Path, name: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / name
    fig.tight_layout()
    fig.savefig(path)
    plt.close(fig)
    print(f"  ✓ {path}")


def _ratio(entry: dict, key: str, fallback_num: str | None = None,
           fallback_den_key: str = "total_valid") -> float:
    """优先直接用 *_ratio 字段，否则按 num/den 计算。"""
    if key in entry:
        return float(entry[key])
    if fallback_num and fallback_num in entry and entry.get(fallback_den_key, 0) > 0:
        return entry[fallback_num] / entry[fallback_den_key]
    return 0.0


# ----------------------------------------------------------------------
# Part 3 / 4
# ----------------------------------------------------------------------

def plot_acceptance_vs_util(data: list[dict], out_dir: Path) -> None:
    if not data: return
    xs = [d["util_norm"] for d in data]
    ana = [_ratio(d, "analytical_ratio", "analytical") for d in data]
    sim = [_ratio(d, "simulation_ratio", "simulation") for d in data]

    fig, ax = plt.subplots()
    ax.plot(xs, ana, "o-", color="#2E86AB", linewidth=2, label="Analytical")
    ax.plot(xs, sim, "s--", color="#E63946", linewidth=2, label="Simulation")
    ax.set_xlabel("Normalized Utilization U/m")
    ax.set_ylabel("Acceptance Ratio")
    ax.set_title("Part 3 — Acceptance Ratio vs. Normalized Utilization")
    ax.set_ylim(-0.02, 1.05)
    ax.legend(loc="lower left")
    _save(fig, out_dir, "acceptance_vs_util.png")


def plot_acceptance_vs_processors(data: list[dict], out_dir: Path) -> None:
    if not data: return
    xs  = [d["m"] for d in data]
    ana = [_ratio(d, "analytical_ratio", "analytical") for d in data]
    sim = [_ratio(d, "simulation_ratio", "simulation") for d in data]

    fig, ax = plt.subplots()
    ax.plot(xs, ana, "o-", color="#2E86AB", linewidth=2, label="Analytical")
    ax.plot(xs, sim, "s--", color="#E63946", linewidth=2, label="Simulation")
    ax.set_xlabel("Number of Processors m")
    ax.set_ylabel("Acceptance Ratio")
    ax.set_title("Part 4 — Acceptance Ratio vs. Number of Processors")
    ax.set_ylim(-0.02, 1.05)
    ax.set_xticks(xs)
    ax.legend(loc="lower right")
    _save(fig, out_dir, "acceptance_vs_processors.png")


# ----------------------------------------------------------------------
# Part 5 — 5 变体对比（支持新字段 r_sim_ratio / rds_sim_ratio）
# ----------------------------------------------------------------------

def plot_variants_comparison(data: list[dict], out_dir: Path) -> None:
    if not data: return
    xs = [d["util_norm"] for d in data]

    ana_series = [
        ("GEDF ana", [d.get("gedf_ratio", 0) for d in data], "#1f77b4", "-"),
        ("NP   ana", [d.get("np_ratio",   0) for d in data], "#d62728", "-"),
        ("DS   ana", [d.get("ds_ratio",   0) for d in data], "#2ca02c", "-"),
        ("R    ana", [d.get("r_ratio",    0) for d in data], "#9467bd", "-"),
    ]
    sim_series = [
        ("GEDF sim", [d.get("gedf_sim_ratio", 0) for d in data], "#1f77b4", "--"),
        ("NP   sim", [d.get("np_sim_ratio",   0) for d in data], "#d62728", "--"),
        ("DS   sim", [d.get("ds_sim_ratio",   0) for d in data], "#2ca02c", "--"),
        ("R    sim", [d.get("r_sim_ratio",    0) for d in data], "#9467bd", "--"),
        ("RDS  sim", [d.get("rds_sim_ratio",  0) for d in data], "#e377c2", "--"),
    ]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5), sharey=True)
    for name, ys, c, ls in ana_series:
        ax1.plot(xs, ys, ls, marker="o", color=c, linewidth=1.8, label=name)
    ax1.set_title("Analytical Acceptance")
    ax1.set_xlabel("U/m"); ax1.set_ylabel("Acceptance Ratio")
    ax1.set_ylim(-0.02, 1.05)
    ax1.legend(loc="lower left")

    for name, ys, c, ls in sim_series:
        ax2.plot(xs, ys, ls, marker="s", color=c, linewidth=1.8, label=name)
    ax2.set_title("Simulation Acceptance")
    ax2.set_xlabel("U/m")
    ax2.legend(loc="lower left")

    fig.suptitle("Part 5 — GEDF / NP / DS / R / RDS Comparison", y=1.02)
    _save(fig, out_dir, "variants_comparison.png")


# ----------------------------------------------------------------------
# Part 6a — 高弹性
# ----------------------------------------------------------------------

def plot_high_elasticity(data: list[dict], out_dir: Path) -> None:
    if not data: return
    xs = [d["util_norm"] for d in data]
    gedf = [_ratio(d, "gedf_ratio", "gedf") for d in data]
    ds   = [_ratio(d, "ds_ratio",   "ds")   for d in data]

    fig, ax = plt.subplots()
    ax.plot(xs, gedf, "o-", color="#2E86AB", linewidth=2, label="D-XU")
    ax.plot(xs, ds,   "s--", color="#E63946", linewidth=2, label="D-XU-DS")
    ax.set_xlabel("Normalized Utilization U/m")
    ax.set_ylabel("Acceptance Ratio")
    ax.set_title("Part 6a — High Elasticity (Fig. 8)")
    ax.set_ylim(-0.02, 1.05)
    ax.legend()
    _save(fig, out_dir, "high_elasticity.png")


# ----------------------------------------------------------------------
# Part 6b — 开销对比
# ----------------------------------------------------------------------

def plot_overhead_comparison(data: list[dict], out_dir: Path) -> None:
    if not data: return
    xs = [d["wcet_scale"] for d in data]
    series = [
        ("GEDF", "gedf_ratio", "gedf", "#1f77b4"),
        ("DS",   "ds_ratio",   "ds",   "#2ca02c"),
        ("R",    "r_ratio",    "r",    "#9467bd"),
        ("RDS",  "rds_ratio",  "rds",  "#e377c2"),
        ("NP",   "np_ratio",   "np",   "#d62728"),
    ]
    fig, ax = plt.subplots()
    for name, rk, ck, color in series:
        ys = [_ratio(d, rk, ck) for d in data]
        ax.plot(xs, ys, "o-", color=color, linewidth=2, label=name)
    ax.set_xscale("log")
    ax.set_xlabel("WCET Scale (log)")
    ax.set_ylabel("Acceptance Ratio")
    ax.set_title("Part 6b — Overhead-Aware Acceptance")
    ax.set_ylim(-0.02, 1.05)
    ax.legend()
    _save(fig, out_dir, "overhead_comparison.png")


# ----------------------------------------------------------------------
# Part 7 — 精度汇总
# ----------------------------------------------------------------------

def plot_precision_summary(prec: dict, out_dir: Path) -> None:
    if not prec: return
    summary = prec.get("summary", {})
    if not summary:
        return

    # 优先读三路上界
    keys = [k for k in ("period", "omega", "graham") if isinstance(summary.get(k), dict)]
    if not keys:
        # 旧结构：只 avg/min/max_precision
        avg = summary.get("avg_precision", 0)
        mn  = summary.get("min_precision", 0)
        mx  = summary.get("max_precision", 0)
        fig, ax = plt.subplots()
        ax.bar(["avg", "min", "max"],
               [avg*100, mn*100, mx*100],
               color=["#2E86AB", "#F4A261", "#E63946"])
        ax.set_ylabel("Precision (%)")
        ax.set_title("Part 7 — Precision Summary")
        ax.axhline(80, color="gray", linestyle="--", linewidth=1, label="Target 80%")
        ax.legend()
        _save(fig, out_dir, "precision_summary.png")
        return

    labels = ["avg", "min", "max"]
    width  = 0.25
    x      = np.arange(len(labels))

    fig, ax = plt.subplots()
    colors = {"period": "#2E86AB", "omega": "#2ca02c", "graham": "#E63946"}
    for i, k in enumerate(keys):
        s = summary[k]
        vals = [s.get("avg", 0)*100, s.get("min", 0)*100, s.get("max", 0)*100]
        pretty = {"period": "WCRT / T", "omega": "WCRT / (Ω·T)", "graham": "WCRT / Graham"}[k]
        ax.bar(x + (i - (len(keys)-1)/2) * width, vals,
               width, label=pretty, color=colors[k])
    ax.axhline(80, color="gray", linestyle="--", linewidth=1, label="Target 80%")
    ax.set_xticks(x); ax.set_xticklabels(labels)
    ax.set_ylabel("Precision (%)")
    ax.set_title("Part 7 — Precision Summary (three bounds)")
    ax.legend()
    _save(fig, out_dir, "precision_summary.png")


# ----------------------------------------------------------------------
# Part 8 — STG 实验（分组绘图）
# ----------------------------------------------------------------------

def plot_stg_experiment(stg: dict, out_dir: Path) -> None:
    if not stg or stg.get("status") == "no_data":
        return
    groups = stg.get("groups", [])
    if not groups:
        # 旧结构 by_util 直接在 stg 上
        groups = [{"label": "root",
                   "num_dags": stg.get("num_dags", 0),
                   "by_util": stg.get("by_util", [])}]

    # 每组一张"仿真接受率曲线 + 精度标注"图
    for g in groups:
        label = g.get("label", "root")
        rows = g.get("by_util", [])
        if not rows: continue
        xs = [r["util_norm"] for r in rows]

        def getv(r, key):
            # 新结构: r["simulation"][key]
            sim = r.get("simulation", {})
            if isinstance(sim, dict) and key in sim:
                return int(bool(sim[key]))
            # 旧结构只 bool "simulation"
            if key == "gedf" and isinstance(sim, bool):
                return int(sim)
            return 0

        series = [
            ("GEDF", "gedf", "#1f77b4", "o"),
            ("NP",   "np",   "#d62728", "s"),
            ("DS",   "ds",   "#2ca02c", "^"),
            ("R",    "r",    "#9467bd", "D"),
            ("RDS",  "rds",  "#e377c2", "v"),
        ]

        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
        for name, k, c, m in series:
            ys = [getv(r, k) for r in rows]
            ax1.plot(xs, ys, "-", marker=m, color=c, linewidth=1.8, label=name)
        ax1.set_title(f"STG [{label}] — Simulation Schedulable (1=Y, 0=N)")
        ax1.set_xlabel("U/m"); ax1.set_ylabel("Schedulable")
        ax1.set_ylim(-0.1, 1.1)
        ax1.set_yticks([0, 1]); ax1.set_yticklabels(["N", "Y"])
        ax1.legend(loc="lower left")

        prec_omega  = [r.get("avg_precision_omega",  r.get("avg_precision", 0))*100 for r in rows]
        prec_period = [r.get("avg_precision_period", 0)*100 for r in rows]
        ax2.plot(xs, prec_omega,  "o-", color="#2ca02c", linewidth=2, label="WCRT / (Ω·T)")
        if any(v > 0 for v in prec_period):
            ax2.plot(xs, prec_period, "s--", color="#2E86AB", linewidth=2, label="WCRT / T")
        ax2.axhline(80, color="gray", linestyle="--", linewidth=1, label="Target 80%")
        ax2.set_title(f"STG [{label}] — Average Precision")
        ax2.set_xlabel("U/m"); ax2.set_ylabel("Precision (%)")
        ax2.set_ylim(0, 110)
        ax2.legend(loc="lower left")

        n_dags = g.get("num_dags", "?")
        fig.suptitle(f"Part 8 — STG group '{label}' ({n_dags} DAGs)", y=1.02)
        _save(fig, out_dir, f"stg_{label}.png")

    # 汇总 bar：各组精度
    labels, avgs_o, avgs_p = [], [], []
    for g in groups:
        rows = g.get("by_util", [])
        if not rows: continue
        ovals = [r.get("avg_precision_omega",  r.get("avg_precision", 0)) for r in rows]
        pvals = [r.get("avg_precision_period", 0) for r in rows]
        ovals = [v for v in ovals if v > 0]
        pvals = [v for v in pvals if v > 0]
        if not ovals: continue
        labels.append(g.get("label", "?"))
        avgs_o.append(sum(ovals)/len(ovals)*100)
        avgs_p.append(sum(pvals)/len(pvals)*100 if pvals else 0)

    if labels:
        x = np.arange(len(labels)); w = 0.35
        fig, ax = plt.subplots()
        ax.bar(x - w/2, avgs_o, w, label="WCRT / (Ω·T)", color="#2ca02c")
        if any(v > 0 for v in avgs_p):
            ax.bar(x + w/2, avgs_p, w, label="WCRT / T", color="#2E86AB")
        ax.axhline(80, color="gray", linestyle="--", linewidth=1, label="Target 80%")
        ax.set_xticks(x); ax.set_xticklabels(labels)
        ax.set_ylabel("Avg Precision (%)")
        ax.set_title("Part 8 — STG Precision by Group (avg over all U/m)")
        ax.set_ylim(0, 110)
        ax.legend()
        _save(fig, out_dir, "stg_precision_bars.png")


# ----------------------------------------------------------------------
# Part 9 — WfInstances（结构与 STG by_util 类似）
# ----------------------------------------------------------------------

def plot_wfinstances(wf: dict, out_dir: Path) -> None:
    if not wf or wf.get("status") == "no_data":
        return
    rows = wf.get("by_util", [])
    if not rows: return

    xs = [r["util_norm"] for r in rows]
    ana = [int(bool(r.get("analytical", False))) for r in rows]
    sim = [int(bool(r.get("simulation", False))) for r in rows]
    prec= [r.get("avg_precision", 0)*100 for r in rows]

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
    ax1.plot(xs, ana, "o-",  color="#2E86AB", linewidth=2, label="Analytical")
    ax1.plot(xs, sim, "s--", color="#E63946", linewidth=2, label="Simulation")
    ax1.set_title("WfInstances — Schedulable (1=Y, 0=N)")
    ax1.set_xlabel("U/m"); ax1.set_ylabel("Schedulable")
    ax1.set_ylim(-0.1, 1.1)
    ax1.set_yticks([0, 1]); ax1.set_yticklabels(["N", "Y"])
    ax1.legend()

    ax2.plot(xs, prec, "o-", color="#2ca02c", linewidth=2, label="Precision")
    ax2.axhline(80, color="gray", linestyle="--", linewidth=1, label="Target 80%")
    ax2.set_title("WfInstances — Average Precision")
    ax2.set_xlabel("U/m"); ax2.set_ylabel("Precision (%)")
    ax2.set_ylim(0, 110); ax2.legend()

    fig.suptitle(f"Part 9 — WfInstances ({wf.get('num_dags', '?')} DAGs)", y=1.02)
    _save(fig, out_dir, "wfinstances.png")


# ----------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------

def main() -> int:
    results_path = Path(sys.argv[1]) if len(sys.argv) >= 2 else Path("output/results.json")
    out_dir      = Path(sys.argv[2]) if len(sys.argv) >= 3 else results_path.parent / "plots"

    if not results_path.exists():
        print(f"[ERROR] {results_path} not found", file=sys.stderr)
        return 1

    with open(results_path, "r", encoding="utf-8") as f:
        root = json.load(f)

    print(f"== Plot results: {results_path} -> {out_dir}")

    plot_acceptance_vs_util      (root.get("acceptance_ratio_vs_util", []),       out_dir)
    plot_acceptance_vs_processors(root.get("acceptance_ratio_vs_processors", []), out_dir)
    plot_variants_comparison     (root.get("variants_comparison", []),            out_dir)
    plot_high_elasticity         (root.get("high_elasticity", []),                out_dir)
    plot_overhead_comparison     (root.get("overhead_comparison", []),            out_dir)
    plot_precision_summary       (root.get("precision_evaluation", {}),           out_dir)
    plot_stg_experiment          (root.get("stg_experiment", {}),                 out_dir)
    plot_wfinstances             (root.get("wfinstances_experiment", {}),         out_dir)

    print("== Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
