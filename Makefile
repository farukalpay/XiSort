# Makefile — XiSort build targets
# Usage:
#   make              (build cli + tests)
#   make run-tests    (run validation suite)
#   make clean        (remove binaries)
#   make release      (O3 + strip)

CXX       ?= g++
CXXFLAGS  ?= -std=c++17 -O3 -fopenmp -Wall -Wextra -march=native
LDFLAGS   ?=
SRC_DIR   := src
BIN_DIR   := bin
OBJ_DIR   := obj

CLI_SRC   := $(SRC_DIR)/xisort_cli.cpp
TEST_SRC  := $(SRC_DIR)/xisort_test.cpp
CORE_SRC  := $(SRC_DIR)/xisort.cpp

CLI_BIN   := $(BIN_DIR)/xisort
TEST_BIN  := $(BIN_DIR)/xisort_tests

.PHONY: all dirs clean run-tests release

all: dirs $(CLI_BIN) $(TEST_BIN)

dirs:
	@mkdir -p $(BIN_DIR) $(OBJ_DIR)

$(CLI_BIN): $(CLI_SRC) $(CORE_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(TEST_BIN): $(TEST_SRC) $(CORE_SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

run-tests: $(TEST_BIN)
	cd $(BIN_DIR) && ./xisort_tests

release: CXXFLAGS := -std=c++17 -O3 -fopenmp -s
release: clean all

clean:
	@rm -rf $(BIN_DIR) $(OBJ_DIR)
