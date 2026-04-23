CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
BUILDDIR = out
TARGET   = $(BUILDDIR)/dag_simulator

LIB_OBJS = $(BUILDDIR)/decomposition.o $(BUILDDIR)/gedf_simulator.o \
	    $(BUILDDIR)/gedf_variants.o $(BUILDDIR)/dag_generators.o \
	    $(BUILDDIR)/stg_parser.o $(BUILDDIR)/wfcommons_parser.o
OBJS = $(BUILDDIR)/main.o $(LIB_OBJS)
HDRS = include/dag_model.h include/decomposition.h include/gedf_simulator.h \
	include/gedf_variants.h include/vertex_reassemble.h \
	include/dag_generators.h include/json.hpp include/stg_parser.h \
	include/wfcommons_parser.h
 

TESTS = dag_model_test decomposition_test gedf_simulator_test \
	 gedf_variants_test overhead_test precision_test dag_generators_test \
	 stg_parser_test \
	 sample_graphs gen_tool
TEST_BINS = $(addprefix $(BUILDDIR)/,$(TESTS))
TEST_OBJS = $(addprefix $(BUILDDIR)/,$(addsuffix .o,$(TESTS)))

.PHONY: all clean run plot tests

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

tests: $(TEST_BINS)

$(TEST_BINS): $(BUILDDIR)/%: $(BUILDDIR)/%.o $(LIB_OBJS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_OBJS)

$(BUILDDIR)/main.o: src/main.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/main.cpp -o $(BUILDDIR)/main.o

$(BUILDDIR)/decomposition.o: src/decomposition.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/decomposition.cpp -o $(BUILDDIR)/decomposition.o

$(BUILDDIR)/gedf_simulator.o: src/gedf_simulator.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/gedf_simulator.cpp -o $(BUILDDIR)/gedf_simulator.o

$(BUILDDIR)/gedf_variants.o: src/gedf_variants.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/gedf_variants.cpp -o $(BUILDDIR)/gedf_variants.o

$(BUILDDIR)/dag_generators.o: src/dag_generators.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/dag_generators.cpp -o $(BUILDDIR)/dag_generators.o

$(BUILDDIR)/%.o: tests/%.cpp $(HDRS) tests/test_utils.h | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR)/gen_tool.o: utils/gen_tool.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c utils/gen_tool.cpp -o $(BUILDDIR)/gen_tool.o

$(BUILDDIR)/stg_parser.o: src/stg_parser.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/stg_parser.cpp -o $(BUILDDIR)/stg_parser.o

$(BUILDDIR)/wfcommons_parser.o: src/wfcommons_parser.cpp $(HDRS) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c src/wfcommons_parser.cpp -o $(BUILDDIR)/wfcommons_parser.o
	
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

output:
	mkdir -p output

# 最小实验（不含 STG/WfInstances）
run: $(TARGET) | output
	$(TARGET) -o output/results.json

# 带 STG 数据的完整实验
run-stg: $(TARGET) | output
	$(TARGET) --stg data/stg -o output/results.json

# 仅绘图（假定 output/results.json 已存在）
plot-only:
	python3 plot_results.py output/results.json output/plots

# 一站式：编译 → 实验（含 STG）→ 绘图
plot: run-stg
	python3 plot_results.py output/results.json output/plots

clean:
	rm -rf $(BUILDDIR) output
