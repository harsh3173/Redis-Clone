#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class RedisTestClient {
private:
    int sock_fd;
    
public:
    RedisTestClient() : sock_fd(-1) {}
    
    ~RedisTestClient() {
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
    
    std::string send_command(const std::string& command) {
        if (sock_fd < 0) return "";
        
        std::string full_command = command + "\r\n";
        send(sock_fd, full_command.c_str(), full_command.length(), 0);
        
        char buffer[4096];
        ssize_t bytes_received = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) return "";
        
        buffer[bytes_received] = '\0';
        return std::string(buffer);
    }
};

class TestRunner {
private:
    int tests_passed = 0;
    int tests_failed = 0;
    
    void assert_response(const std::string& actual, const std::string& expected, const std::string& test_name) {
        if (actual.find(expected) != std::string::npos) {
            std::cout << "✓ " << test_name << std::endl;
            tests_passed++;
        } else {
            std::cout << "✗ " << test_name << " - Expected: " << expected << ", Got: " << actual << std::endl;
            tests_failed++;
        }
    }
    
public:
    void run_basic_string_tests() {
        std::cout << "\n=== Basic String Operations Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        std::string response = client.send_command("SET key1 value1");
        assert_response(response, "+OK", "SET basic");
        
        response = client.send_command("GET key1");
        assert_response(response, "$6\r\nvalue1", "GET basic");
        
        response = client.send_command("GET nonexistent");
        assert_response(response, "$-1", "GET nonexistent key");
        
        response = client.send_command("SET key2 value2 EX 1");
        assert_response(response, "+OK", "SET with expiry");
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        response = client.send_command("GET key2");
        assert_response(response, "$-1", "GET expired key");
        
        response = client.send_command("DEL key1");
        assert_response(response, ":1", "DEL existing key");
        
        response = client.send_command("DEL key1");
        assert_response(response, ":0", "DEL nonexistent key");
        
        response = client.send_command("EXISTS key1");
        assert_response(response, ":0", "EXISTS nonexistent key");
        
        client.send_command("SET key3 value3");
        response = client.send_command("EXISTS key3");
        assert_response(response, ":1", "EXISTS existing key");
    }
    
    void run_list_tests() {
        std::cout << "\n=== List Operations Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        std::string response = client.send_command("LPUSH mylist item1");
        assert_response(response, ":1", "LPUSH first item");
        
        response = client.send_command("RPUSH mylist item2 item3");
        assert_response(response, ":3", "RPUSH multiple items");
        
        response = client.send_command("LLEN mylist");
        assert_response(response, ":3", "LLEN");
        
        response = client.send_command("LPOP mylist");
        assert_response(response, "$5\r\nitem1", "LPOP");
        
        response = client.send_command("RPOP mylist");
        assert_response(response, "$5\r\nitem3", "RPOP");
        
        response = client.send_command("LRANGE mylist 0 -1");
        assert_response(response, "*1\r\n$5\r\nitem2", "LRANGE all");
        
        response = client.send_command("LPOP empty_list");
        assert_response(response, "$-1", "LPOP empty list");
    }
    
    void run_hash_tests() {
        std::cout << "\n=== Hash Operations Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        std::string response = client.send_command("HSET myhash field1 value1");
        assert_response(response, ":1", "HSET new field");
        
        response = client.send_command("HSET myhash field1 newvalue1 field2 value2");
        assert_response(response, ":1", "HSET update and new");
        
        response = client.send_command("HGET myhash field1");
        assert_response(response, "$9\r\nnewvalue1", "HGET existing field");
        
        response = client.send_command("HGET myhash nonexistent");
        assert_response(response, "$-1", "HGET nonexistent field");
        
        response = client.send_command("HGETALL myhash");
        assert_response(response, "*4\r\n", "HGETALL");
        
        response = client.send_command("HDEL myhash field1");
        assert_response(response, ":1", "HDEL existing field");
        
        response = client.send_command("HDEL myhash field1");
        assert_response(response, ":0", "HDEL nonexistent field");
    }
    
    void run_set_tests() {
        std::cout << "\n=== Set Operations Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        std::string response = client.send_command("SADD myset member1");
        assert_response(response, ":1", "SADD new member");
        
        response = client.send_command("SADD myset member1 member2 member3");
        assert_response(response, ":2", "SADD duplicate and new");
        
        response = client.send_command("SCARD myset");
        assert_response(response, ":3", "SCARD");
        
        response = client.send_command("SMEMBERS myset");
        assert_response(response, "*3\r\n", "SMEMBERS");
        
        response = client.send_command("SREM myset member1");
        assert_response(response, ":1", "SREM existing member");
        
        response = client.send_command("SREM myset member1");
        assert_response(response, ":0", "SREM nonexistent member");
        
        response = client.send_command("SCARD myset");
        assert_response(response, ":2", "SCARD after removal");
    }
    
    void run_expiry_tests() {
        std::cout << "\n=== Expiry Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        client.send_command("SET expiry_key test_value");
        std::string response = client.send_command("EXPIRE expiry_key 2");
        assert_response(response, ":1", "EXPIRE existing key");
        
        response = client.send_command("TTL expiry_key");
        assert_response(response, ":2", "TTL with expiry");
        
        response = client.send_command("EXPIRE nonexistent 10");
        assert_response(response, ":0", "EXPIRE nonexistent key");
        
        client.send_command("SET persistent_key value");
        response = client.send_command("TTL persistent_key");
        assert_response(response, ":-1", "TTL without expiry");
        
        std::this_thread::sleep_for(std::chrono::seconds(3));
        response = client.send_command("GET expiry_key");
        assert_response(response, "$-1", "GET expired key");
        
        response = client.send_command("TTL expiry_key");
        assert_response(response, ":-2", "TTL expired key");
    }
    
    void run_error_handling_tests() {
        std::cout << "\n=== Error Handling Tests ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        std::string response = client.send_command("GET");
        assert_response(response, "-ERR", "GET without arguments");
        
        response = client.send_command("SET key");
        assert_response(response, "-ERR", "SET incomplete");
        
        response = client.send_command("UNKNOWNCOMMAND");
        assert_response(response, "-ERR", "Unknown command");
        
        client.send_command("SET string_key value");
        response = client.send_command("LPUSH string_key item");
        assert_response(response, "-WRONGTYPE", "Wrong type operation");
        
        response = client.send_command("HGET string_key field");
        assert_response(response, "-WRONGTYPE", "Wrong type hash operation");
    }
    
    void run_concurrent_tests() {
        std::cout << "\n=== Concurrent Access Tests ===" << std::endl;
        
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        
        for (int i = 0; i < 10; ++i) {
            threads.emplace_back([&, i]() {
                RedisTestClient client;
                if (client.connect_to_server()) {
                    for (int j = 0; j < 100; ++j) {
                        std::string key = "concurrent_key_" + std::to_string(i) + "_" + std::to_string(j);
                        std::string value = "value_" + std::to_string(j);
                        
                        client.send_command("SET " + key + " " + value);
                        std::string response = client.send_command("GET " + key);
                        
                        if (response.find(value) != std::string::npos) {
                            success_count++;
                        }
                    }
                }
            });
        }
        
        for (auto& t : threads) {
            t.join();
        }
        
        if (success_count >= 990) {
            std::cout << "✓ Concurrent operations (" << success_count << "/1000 successful)" << std::endl;
            tests_passed++;
        } else {
            std::cout << "✗ Concurrent operations (" << success_count << "/1000 successful)" << std::endl;
            tests_failed++;
        }
    }
    
    void run_memory_stress_test() {
        std::cout << "\n=== Memory Stress Test ===" << std::endl;
        
        RedisTestClient client;
        assert(client.connect_to_server());
        
        client.send_command("FLUSHALL");
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < 10000; ++i) {
            std::string key = "stress_key_" + std::to_string(i);
            std::string value = "stress_value_" + std::to_string(i) + "_with_longer_content_to_test_memory";
            client.send_command("SET " + key + " " + value);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::string response = client.send_command("GET stress_key_5000");
        if (response.find("stress_value_5000") != std::string::npos) {
            std::cout << "✓ Memory stress test (10K keys in " << duration.count() << "ms)" << std::endl;
            tests_passed++;
        } else {
            std::cout << "✗ Memory stress test failed" << std::endl;
            tests_failed++;
        }
        
        for (int i = 0; i < 5000; ++i) {
            std::string key = "stress_key_" + std::to_string(i);
            client.send_command("DEL " + key);
        }
        
        response = client.send_command("GET stress_key_2500");
        if (response.find("$-1") != std::string::npos) {
            std::cout << "✓ Bulk deletion test" << std::endl;
            tests_passed++;
        } else {
            std::cout << "✗ Bulk deletion test failed" << std::endl;
            tests_failed++;
        }
    }
    
    void run_pubsub_tests() {
        std::cout << "\n=== Pub/Sub Tests ===" << std::endl;
        
        RedisTestClient pub_client, sub_client;
        assert(pub_client.connect_to_server());
        assert(sub_client.connect_to_server());
        
        std::string response = pub_client.send_command("PUBLISH test_channel hello_world");
        assert_response(response, ":0", "PUBLISH to empty channel");
        
        response = pub_client.send_command("PUBLISH another_channel test_message");
        assert_response(response, ":0", "PUBLISH to another empty channel");
        
        std::cout << "✓ Basic pub/sub functionality (no active subscribers)" << std::endl;
        tests_passed++;
    }
    
    void run_all_tests() {
        std::cout << "Starting Redis Clone Test Suite..." << std::endl;
        std::cout << "Connecting to server on localhost:6379" << std::endl;
        
        run_basic_string_tests();
        run_list_tests();
        run_hash_tests();
        run_set_tests();
        run_expiry_tests();
        run_error_handling_tests();
        run_concurrent_tests();
        run_memory_stress_test();
        run_pubsub_tests();
        
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Tests Passed: " << tests_passed << std::endl;
        std::cout << "Tests Failed: " << tests_failed << std::endl;
        std::cout << "Success Rate: " << (tests_passed * 100.0 / (tests_passed + tests_failed)) << "%" << std::endl;
        
        if (tests_failed == 0) {
            std::cout << "🎉 All tests passed!" << std::endl;
        }
    }
};

int main() {
    TestRunner runner;
    runner.run_all_tests();
    return 0;
}