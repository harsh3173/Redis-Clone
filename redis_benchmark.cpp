#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class BenchmarkClient {
private:
    int sock_fd;
    
public:
    BenchmarkClient() : sock_fd(-1) {}
    
    ~BenchmarkClient() {
        if (sock_fd >= 0) {
            close(sock_fd);
        }
    }
    
    bool connect_to_server(const std::string& host = "127.0.0.1", int port = 6379) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return false;
        
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
        
        return connect(sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) >= 0;
    }
    
    bool send_command_fast(const std::string& command) {
        if (sock_fd < 0) return false;
        
        std::string full_command = command + "\r\n";
        ssize_t sent = send(sock_fd, full_command.c_str(), full_command.length(), 0);
        if (sent <= 0) return false;
        
        char buffer[1024];
        ssize_t received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        return received > 0;
    }
};

class PerformanceBenchmark {
private:
    std::atomic<long> total_operations{0};
    std::atomic<long> successful_operations{0};
    std::atomic<long> failed_operations{0};
    
    std::string generate_random_string(int length) {
        const std::string chars = "abcdefghijklmnopqrstuvwxyz0123456789";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, chars.size() - 1);
        
        std::string result;
        result.reserve(length);
        for (int i = 0; i < length; ++i) {
            result += chars[dis(gen)];
        }
        return result;
    }
    
public:
    void run_set_benchmark(int num_threads, int operations_per_thread, int key_size, int value_size) {
        std::cout << "\n=== SET Benchmark ===" << std::endl;
        std::cout << "Threads: " << num_threads << ", Operations per thread: " << operations_per_thread << std::endl;
        std::cout << "Key size: " << key_size << " bytes, Value size: " << value_size << " bytes" << std::endl;
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([this, t, operations_per_thread, key_size, value_size]() {
                BenchmarkClient client;
                if (!client.connect_to_server()) {
                    failed_operations += operations_per_thread;
                    return;
                }
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    std::string key = "bench_key_" + std::to_string(t) + "_" + std::to_string(i);
                    if (key.length() < key_size) {
                        key += generate_random_string(key_size - key.length());
                    }
                    
                    std::string value = generate_random_string(value_size);
                    std::string command = "SET " + key + " " + value;
                    
                    if (client.send_command_fast(command)) {
                        successful_operations++;
                    } else {
                        failed_operations++;
                    }
                    total_operations++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        double ops_per_second = (total_operations.load() * 1000.0) / duration.count();
        double success_rate = (successful_operations.load() * 100.0) / total_operations.load();
        
        std::cout << "Total operations: " << total_operations.load() << std::endl;
        std::cout << "Successful: " << successful_operations.load() << std::endl;
        std::cout << "Failed: " << failed_operations.load() << std::endl;
        std::cout << "Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "Throughput: " << static_cast<int>(ops_per_second) << " ops/sec" << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
        
        reset_counters();
    }
    
    void run_get_benchmark(int num_threads, int operations_per_thread) {
        std::cout << "\n=== GET Benchmark ===" << std::endl;
        std::cout << "Threads: " << num_threads << ", Operations per thread: " << operations_per_thread << std::endl;
        
        BenchmarkClient setup_client;
        setup_client.connect_to_server();
        for (int i = 0; i < 1000; ++i) {
            std::string command = "SET get_bench_key_" + std::to_string(i) + " value_" + std::to_string(i);
            setup_client.send_command_fast(command);
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([this, t, operations_per_thread]() {
                BenchmarkClient client;
                if (!client.connect_to_server()) {
                    failed_operations += operations_per_thread;
                    return;
                }
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, 999);
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    int key_id = dis(gen);
                    std::string command = "GET get_bench_key_" + std::to_string(key_id);
                    
                    if (client.send_command_fast(command)) {
                        successful_operations++;
                    } else {
                        failed_operations++;
                    }
                    total_operations++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        double ops_per_second = (total_operations.load() * 1000.0) / duration.count();
        double success_rate = (successful_operations.load() * 100.0) / total_operations.load();
        
        std::cout << "Total operations: " << total_operations.load() << std::endl;
        std::cout << "Successful: " << successful_operations.load() << std::endl;
        std::cout << "Failed: " << failed_operations.load() << std::endl;
        std::cout << "Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "Throughput: " << static_cast<int>(ops_per_second) << " ops/sec" << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
        
        reset_counters();
    }
    
    void run_mixed_benchmark(int num_threads, int operations_per_thread) {
        std::cout << "\n=== Mixed Operations Benchmark ===" << std::endl;
        std::cout << "Threads: " << num_threads << ", Operations per thread: " << operations_per_thread << std::endl;
        std::cout << "Mix: 60% GET, 30% SET, 10% other operations" << std::endl;
        
        BenchmarkClient setup_client;
        setup_client.connect_to_server();
        for (int i = 0; i < 1000; ++i) {
            std::string command = "SET mixed_key_" + std::to_string(i) + " value_" + std::to_string(i);
            setup_client.send_command_fast(command);
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([this, t, operations_per_thread]() {
                BenchmarkClient client;
                if (!client.connect_to_server()) {
                    failed_operations += operations_per_thread;
                    return;
                }
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> op_dis(1, 100);
                std::uniform_int_distribution<> key_dis(0, 999);
                
                for (int i = 0; i < operations_per_thread; ++i) {
                    int op_type = op_dis(gen);
                    int key_id = key_dis(gen);
                    std::string command;
                    
                    if (op_type <= 60) {
                        command = "GET mixed_key_" + std::to_string(key_id);
                    } else if (op_type <= 90) {
                        command = "SET mixed_key_" + std::to_string(key_id) + " new_value_" + std::to_string(i);
                    } else if (op_type <= 95) {
                        command = "DEL mixed_key_" + std::to_string(key_id);
                    } else {
                        command = "EXISTS mixed_key_" + std::to_string(key_id);
                    }
                    
                    if (client.send_command_fast(command)) {
                        successful_operations++;
                    } else {
                        failed_operations++;
                    }
                    total_operations++;
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        double ops_per_second = (total_operations.load() * 1000.0) / duration.count();
        double success_rate = (successful_operations.load() * 100.0) / total_operations.load();
        
        std::cout << "Total operations: " << total_operations.load() << std::endl;
        std::cout << "Successful: " << successful_operations.load() << std::endl;
        std::cout << "Failed: " << failed_operations.load() << std::endl;
        std::cout << "Duration: " << duration.count() << " ms" << std::endl;
        std::cout << "Throughput: " << static_cast<int>(ops_per_second) << " ops/sec" << std::endl;
        std::cout << "Success rate: " << std::fixed << std::setprecision(2) << success_rate << "%" << std::endl;
        
        reset_counters();
    }
    
    void run_latency_test() {
        std::cout << "\n=== Latency Test ===" << std::endl;
        
        BenchmarkClient client;
        if (!client.connect_to_server()) {
            std::cout << "Failed to connect to server" << std::endl;
            return;
        }
        
        std::vector<double> latencies;
        const int num_operations = 1000;
        
        for (int i = 0; i < num_operations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            
            std::string command = "SET latency_key_" + std::to_string(i) + " latency_value";
            client.send_command_fast(command);
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            latencies.push_back(duration.count() / 1000.0);
        }
        
        std::sort(latencies.begin(), latencies.end());
        
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double p50 = latencies[latencies.size() * 0.5];
        double p95 = latencies[latencies.size() * 0.95];
        double p99 = latencies[latencies.size() * 0.99];
        
        std::cout << "Samples: " << num_operations << std::endl;
        std::cout << "Average latency: " << std::fixed << std::setprecision(3) << avg_latency << " ms" << std::endl;
        std::cout << "P50 latency: " << std::fixed << std::setprecision(3) << p50 << " ms" << std::endl;
        std::cout << "P95 latency: " << std::fixed << std::setprecision(3) << p95 << " ms" << std::endl;
        std::cout << "P99 latency: " << std::fixed << std::setprecision(3) << p99 << " ms" << std::endl;
        std::cout << "Min latency: " << std::fixed << std::setprecision(3) << latencies.front() << " ms" << std::endl;
        std::cout << "Max latency: " << std::fixed << std::setprecision(3) << latencies.back() << " ms" << std::endl;
    }
    
    void run_connection_stress_test() {
        std::cout << "\n=== Connection Stress Test ===" << std::endl;
        
        const int max_connections = 100;
        std::vector<std::thread> threads;
        std::atomic<int> successful_connections{0};
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < max_connections; ++i) {
            threads.emplace_back([&, i]() {
                BenchmarkClient client;
                if (client.connect_to_server()) {
                    successful_connections++;
                    
                    for (int j = 0; j < 100; ++j) {
                        std::string command = "SET conn_test_" + std::to_string(i) + "_" + std::to_string(j) + " value";
                        client.send_command_fast(command);
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
        
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "Attempted connections: " << max_connections << std::endl;
        std::cout << "Successful connections: " << successful_connections.load() << std::endl;
        std::cout << "Connection success rate: " << (successful_connections.load() * 100.0 / max_connections) << "%" << std::endl;
        std::cout << "Total duration: " << duration.count() << " ms" << std::endl;
    }
    
    void reset_counters() {
        total_operations = 0;
        successful_operations = 0;
        failed_operations = 0;
    }
    
    void run_all_benchmarks() {
        std::cout << "Redis Clone Performance Benchmark Suite" << std::endl;
        std::cout << "========================================" << std::endl;
        
        BenchmarkClient test_client;
        if (!test_client.connect_to_server()) {
            std::cout << "Error: Cannot connect to Redis clone server on localhost:6379" << std::endl;
            std::cout << "Please start the server first: ./redis_clone" << std::endl;
            return;
        }
        
        test_client.send_command_fast("FLUSHALL");
        
        run_set_benchmark(1, 10000, 16, 64);
        run_set_benchmark(4, 5000, 16, 64);
        run_set_benchmark(8, 2500, 16, 64);
        
        run_get_benchmark(1, 10000);
        run_get_benchmark(4, 5000);
        run_get_benchmark(8, 2500);
        
        run_mixed_benchmark(4, 5000);
        run_latency_test();
        run_connection_stress_test();
        
        std::cout << "\n=== Benchmark Complete ===" << std::endl;
        std::cout << "For comparison with Redis, install redis-tools and run:" << std::endl;
        std::cout << "redis-benchmark -h 127.0.0.1 -p 6379 -t get,set -n 10000 -c 50" << std::endl;
    }
};

int main() {
    PerformanceBenchmark benchmark;
    benchmark.run_all_benchmarks();
    return 0;
}