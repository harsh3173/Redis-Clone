CXX = clang++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra -pthread
LDFLAGS = -pthread

TARGET = redis_clone
TEST_TARGET = redis_test
BENCHMARK_TARGET = redis_benchmark
SOURCES = redis_clone.cpp
TEST_SOURCES = redis_test.cpp
BENCHMARK_SOURCES = redis_benchmark.cpp

.PHONY: all clean test run benchmark_custom benchmark

all: $(TARGET) $(TEST_TARGET) $(BENCHMARK_TARGET)

all: $(TARGET) $(TEST_TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

$(TEST_TARGET): $(TEST_SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TEST_TARGET) $(TEST_SOURCES) $(LDFLAGS)

$(BENCHMARK_TARGET): $(BENCHMARK_SOURCES)
	$(CXX) $(CXXFLAGS) -o $(BENCHMARK_TARGET) $(BENCHMARK_SOURCES) $(LDFLAGS)

test: $(TEST_TARGET)
	@echo "Make sure Redis clone server is running on port 6379"
	@echo "Run './redis_clone' in another terminal first"
	@sleep 1
	./$(TEST_TARGET)

benchmark_custom: $(BENCHMARK_TARGET)
	@echo "Make sure Redis clone server is running on port 6379"
	@echo "Run './redis_clone' in another terminal first"
	@sleep 1
	./$(BENCHMARK_TARGET)

run: $(TARGET)
	./$(TARGET)

benchmark: $(TARGET)
	@echo "Starting benchmark with redis-benchmark (install redis-tools if needed)"
	@echo "Server will start in background..."
	./$(TARGET) &
	@sleep 2
	redis-benchmark -h 127.0.0.1 -p 6379 -t get,set -n 10000 -c 50 || echo "Install redis-tools for benchmarking"
	@pkill redis_clone || true

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(BENCHMARK_TARGET)

install_deps_macos:
	@echo "Installing dependencies for macOS..."
	@command -v brew >/dev/null 2>&1 || { echo "Please install Homebrew first"; exit 1; }
	brew install redis

debug: CXXFLAGS += -g -DDEBUG
debug: $(TARGET)

release: CXXFLAGS += -DNDEBUG -march=native
release: $(TARGET)

help:
	@echo "Available targets:"
	@echo "  all              - Build server, test client, and benchmark"
	@echo "  run              - Start the Redis clone server"
	@echo "  test             - Run the test suite (server must be running)"
	@echo "  benchmark        - Run performance benchmark with redis-benchmark"
	@echo "  benchmark_custom - Run custom performance benchmark suite"
	@echo "  debug            - Build with debug symbols"
	@echo "  release          - Build optimized release version"
	@echo "  clean            - Remove compiled binaries"
	@echo "  install_deps_macos - Install Redis tools on macOS"