CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Iinclude
TARGET   = build/dag_simulator

LIB_OBJS = build/decomposition.o build/gedf_simulator.o \
	    build/gedf_variants.o build/dag_generators.o \
	    build/stg_parser.o build/wfcommons_parser.o
OBJS = build/main.o $(LIB_OBJS)
HDRS = include/dag_model.h include/decomposition.h include/gedf_simulator.h \
	include/gedf_variants.h include/vertex_reassemble.h \
	include/dag_generators.h include/json.hpp include/stg_parser.h \
	include/wfcommons_parser.h
 

TESTS = dag_model_test decomposition_test gedf_simulator_test \
	 gedf_variants_test overhead_test precision_test dag_generators_test \
	 sample_graphs gen_tool
TEST_BINS = $(addprefix build/,$(TESTS))
TEST_OBJS = $(addprefix build/,$(addsuffix .o,$(TESTS)))

.PHONY: all clean run plot tests

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

tests: $(TEST_BINS)

$(TEST_BINS): build/%: build/%.o $(LIB_OBJS) | build
	$(CXX) $(CXXFLAGS) -o $@ $< $(LIB_OBJS)

build/main.o: src/main.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/main.cpp -o build/main.o

build/decomposition.o: src/decomposition.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/decomposition.cpp -o build/decomposition.o

build/gedf_simulator.o: src/gedf_simulator.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/gedf_simulator.cpp -o build/gedf_simulator.o

build/gedf_variants.o: src/gedf_variants.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/gedf_variants.cpp -o build/gedf_variants.o

build/dag_generators.o: src/dag_generators.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/dag_generators.cpp -o build/dag_generators.o

build/%.o: tests/%.cpp $(HDRS) tests/test_utils.h | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/gen_tool.o: utils/gen_tool.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c utils/gen_tool.cpp -o build/gen_tool.o

build/stg_parser.o: src/stg_parser.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/stg_parser.cpp -o build/stg_parser.o

build/wfcommons_parser.o: src/wfcommons_parser.cpp $(HDRS) | build
	$(CXX) $(CXXFLAGS) -c src/wfcommons_parser.cpp -o build/wfcommons_parser.o
	
build:
	mkdir -p build

output:
	mkdir -p output

run: $(TARGET) | output
	./$(TARGET) output/results.json

plot: run
	python3 plot_results.py output/results.json output

clean:
	rm -rf build output
