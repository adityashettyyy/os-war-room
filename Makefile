# Plain-make build (no CMake required). Targets: all, test, run, clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -I.
BUILD    := build
CORE     := kernel/kernel.cpp kernel/kernel_json.cpp simulation/failure_lab.cpp simulation/experiment_lab.cpp simulation/detective.cpp
CORE_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(CORE))
TESTS    := tests/test_main.cpp tests/test_scheduler.cpp tests/test_memory.cpp tests/test_sync_ipc_io.cpp tests/test_kernel.cpp
TEST_OBJ := $(patsubst %.cpp,$(BUILD)/%.o,$(TESTS))

all: $(BUILD)/owr_server $(BUILD)/owr_cli $(BUILD)/owr_tests

$(BUILD)/%.o: %.cpp $(wildcard kernel/*/*.hpp kernel/*.hpp simulation/*.hpp tests/*.hpp)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -DOWR_ROOT='"$(CURDIR)"' -c $< -o $@

$(BUILD)/owr_server: $(BUILD)/dashboard/backend/api_server.o $(CORE_OBJ)
	$(CXX) $^ -o $@ -pthread
$(BUILD)/owr_cli: $(BUILD)/tools/owr_cli.o $(CORE_OBJ)
	$(CXX) $^ -o $@
$(BUILD)/owr_tests: $(TEST_OBJ) $(CORE_OBJ)
	$(CXX) $^ -o $@

test: $(BUILD)/owr_tests
	./$(BUILD)/owr_tests

run: $(BUILD)/owr_server
	./$(BUILD)/owr_server --static dashboard/frontend/dist --scenarios scenarios

clean:
	rm -rf $(BUILD)

.PHONY: all test run clean
