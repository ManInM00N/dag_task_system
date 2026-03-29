#pragma once
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "dag_model.h"

struct TestCtx {
    int total = 0;
    int failed = 0;
    void expect(bool ok, const std::string &name) {
        ++total;
        if (!ok) {
            ++failed;
            std::cerr << "[FAIL] " << name << "\n";
        }
    }
    void expect_near(double a, double b, double eps, const std::string &name) {
        expect(std::fabs(a - b) <= eps, name + " (" + std::to_string(a) + " vs " + std::to_string(b) + ")");
    }
    int exit_code() const { return failed == 0 ? 0 : 1; }
};

inline DAGTask make_linear_task() {
    DAGTask t; t.task_id = 0; t.period = 10.0;
    t.add_vertex(1, 1.0);
    t.add_vertex(2, 2.0);
    t.add_vertex(3, 1.0);
    t.add_edge(1, 2);
    t.add_edge(2, 3);
    t.compute_parameters();
    return t;
}

inline void print_summary(const std::string &name, const TestCtx &ctx) {
    std::cout << "[" << name << "] total=" << ctx.total << " failed=" << ctx.failed << "\n";
}
