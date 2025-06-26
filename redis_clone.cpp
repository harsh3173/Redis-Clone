#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <list>
#include <set>
#include <memory>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <queue>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

class RedisValue {
public:
    enum Type { STRING, LIST, HASH, SET };
    
    Type type;
    std::string str_val;
    std::list<std::string> list_val;
    std::unordered_map<std::string, std::string> hash_val;
    std::set<std::string> set_val;
    std::chrono::steady_clock::time_point expiry;
    bool has_expiry = false;
    
    RedisValue(Type t) : type(t) {}
    
    bool is_expired() const {
        return has_expiry && std::chrono::steady_clock::now() > expiry;
    }
    
    void set_expiry(int seconds) {
        has_expiry = true;
        expiry = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    }
};

class ConnectionPool {
private:
    std::queue<int> available_connections;
    std::mutex pool_mutex;
    std::condition_variable pool_cv;
    std::atomic<int> active_connections{0};
    const int max_connections = 1000;
    
public:
    int acquire_connection() {
        std::unique_lock<std::mutex> lock(pool_mutex);
        if (active_connections < max_connections) {
            active_connections++;
            return active_connections.load();
        }
        return -1;
    }
    
    void release_connection(int conn_id) {
        std::unique_lock<std::mutex> lock(pool_mutex);
        active_connections--;
        pool_cv.notify_one();
    }
    
    int get_active_count() const {
        return active_connections.load();
    }
};

class PubSubManager {
private:
    std::unordered_map<std::string, std::vector<int>> channel_subscribers;
    std::shared_mutex pubsub_mutex;
    
public:
    void subscribe(const std::string& channel, int client_fd) {
        std::unique_lock<std::shared_mutex> lock(pubsub_mutex);
        channel_subscribers[channel].push_back(client_fd);
    }
    
    void unsubscribe(const std::string& channel, int client_fd) {
        std::unique_lock<std::shared_mutex> lock(pubsub_mutex);
        auto& subscribers = channel_subscribers[channel];
        subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), client_fd), subscribers.end());
    }
    
    int publish(const std::string& channel, const std::string& message) {
        std::shared_lock<std::shared_mutex> lock(pubsub_mutex);
        auto it = channel_subscribers.find(channel);
        if (it == channel_subscribers.end()) return 0;
        
        int count = 0;
        std::string response = "*3\r\n$7\r\nmessage\r\n$" + std::to_string(channel.length()) + 
                              "\r\n" + channel + "\r\n$" + std::to_string(message.length()) + 
                              "\r\n" + message + "\r\n";
        
        for (int fd : it->second) {
            if (send(fd, response.c_str(), response.length(), MSG_NOSIGNAL) > 0) {
                count++;
            }
        }
        return count;
    }
};

class RedisClone {
private:
    std::unordered_map<std::string, std::shared_ptr<RedisValue>> data;
    mutable std::shared_mutex data_mutex;
    ConnectionPool connection_pool;
    PubSubManager pubsub_manager;
    std::atomic<bool> running{true};
    std::thread cleanup_thread;
    
    void cleanup_expired_keys() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::unique_lock<std::shared_mutex> lock(data_mutex);
            
            auto it = data.begin();
            while (it != data.end()) {
                if (it->second->is_expired()) {
                    it = data.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    
    std::string encode_bulk_string(const std::string& str) {
        return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
    }
    
    std::string encode_array(const std::vector<std::string>& arr) {
        std::string result = "*" + std::to_string(arr.size()) + "\r\n";
        for (const auto& item : arr) {
            result += encode_bulk_string(item);
        }
        return result;
    }
    
    std::string encode_integer(int value) {
        return ":" + std::to_string(value) + "\r\n";
    }
    
    std::string encode_simple_string(const std::string& str) {
        return "+" + str + "\r\n";
    }
    
    std::string encode_error(const std::string& error) {
        return "-" + error + "\r\n";
    }
    
    std::vector<std::string> parse_command(const std::string& input) {
        std::vector<std::string> tokens;
        std::istringstream iss(input);
        std::string token;
        
        while (iss >> token) {
            tokens.push_back(token);
        }
        return tokens;
    }
    
    std::string process_command(const std::vector<std::string>& tokens) {
        if (tokens.empty()) return encode_error("ERR unknown command");
        
        std::string cmd = tokens[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::toupper);
        
        if (cmd == "SET") {
            return handle_set(tokens);
        } else if (cmd == "GET") {
            return handle_get(tokens);
        } else if (cmd == "DEL") {
            return handle_del(tokens);
        } else if (cmd == "EXISTS") {
            return handle_exists(tokens);
        } else if (cmd == "EXPIRE") {
            return handle_expire(tokens);
        } else if (cmd == "TTL") {
            return handle_ttl(tokens);
        } else if (cmd == "LPUSH") {
            return handle_lpush(tokens);
        } else if (cmd == "RPUSH") {
            return handle_rpush(tokens);
        } else if (cmd == "LPOP") {
            return handle_lpop(tokens);
        } else if (cmd == "RPOP") {
            return handle_rpop(tokens);
        } else if (cmd == "LLEN") {
            return handle_llen(tokens);
        } else if (cmd == "LRANGE") {
            return handle_lrange(tokens);
        } else if (cmd == "HSET") {
            return handle_hset(tokens);
        } else if (cmd == "HGET") {
            return handle_hget(tokens);
        } else if (cmd == "HDEL") {
            return handle_hdel(tokens);
        } else if (cmd == "HGETALL") {
            return handle_hgetall(tokens);
        } else if (cmd == "SADD") {
            return handle_sadd(tokens);
        } else if (cmd == "SREM") {
            return handle_srem(tokens);
        } else if (cmd == "SMEMBERS") {
            return handle_smembers(tokens);
        } else if (cmd == "SCARD") {
            return handle_scard(tokens);
        } else if (cmd == "PUBLISH") {
            return handle_publish(tokens);
        } else if (cmd == "PING") {
            return encode_simple_string("PONG");
        } else if (cmd == "INFO") {
            return handle_info();
        } else if (cmd == "FLUSHALL") {
            return handle_flushall();
        }
        
        return encode_error("ERR unknown command '" + cmd + "'");
    }
    
    std::string handle_set(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'set' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto value = std::make_shared<RedisValue>(RedisValue::STRING);
        value->str_val = tokens[2];
        
        if (tokens.size() >= 5 && tokens[3] == "EX") {
            try {
                int seconds = std::stoi(tokens[4]);
                value->set_expiry(seconds);
            } catch (...) {
                return encode_error("ERR invalid expire time");
            }
        }
        
        data[tokens[1]] = value;
        return encode_simple_string("OK");
    }
    
    std::string handle_get(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'get' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired()) {
            return "$-1\r\n";
        }
        
        if (it->second->type != RedisValue::STRING) {
            return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        
        return encode_bulk_string(it->second->str_val);
    }
    
    std::string handle_del(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'del' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        int deleted = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (data.erase(tokens[i]) > 0) {
                deleted++;
            }
        }
        return encode_integer(deleted);
    }
    
    std::string handle_exists(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'exists' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        int exists = 0;
        for (size_t i = 1; i < tokens.size(); ++i) {
            auto it = data.find(tokens[i]);
            if (it != data.end() && !it->second->is_expired()) {
                exists++;
            }
        }
        return encode_integer(exists);
    }
    
    std::string handle_expire(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'expire' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired()) {
            return encode_integer(0);
        }
        
        try {
            int seconds = std::stoi(tokens[2]);
            it->second->set_expiry(seconds);
            return encode_integer(1);
        } catch (...) {
            return encode_error("ERR invalid expire time");
        }
    }
    
    std::string handle_ttl(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'ttl' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end()) {
            return encode_integer(-2);
        }
        
        if (it->second->is_expired()) {
            return encode_integer(-2);
        }
        
        if (!it->second->has_expiry) {
            return encode_integer(-1);
        }
        
        auto now = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::seconds>(it->second->expiry - now);
        return encode_integer(remaining.count());
    }
    
    std::string handle_lpush(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'lpush' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        std::shared_ptr<RedisValue> value;
        
        if (it == data.end() || it->second->is_expired()) {
            value = std::make_shared<RedisValue>(RedisValue::LIST);
            data[tokens[1]] = value;
        } else {
            value = it->second;
            if (value->type != RedisValue::LIST) {
                return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }
        }
        
        for (size_t i = 2; i < tokens.size(); ++i) {
            value->list_val.push_front(tokens[i]);
        }
        
        return encode_integer(value->list_val.size());
    }
    
    std::string handle_rpush(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'rpush' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        std::shared_ptr<RedisValue> value;
        
        if (it == data.end() || it->second->is_expired()) {
            value = std::make_shared<RedisValue>(RedisValue::LIST);
            data[tokens[1]] = value;
        } else {
            value = it->second;
            if (value->type != RedisValue::LIST) {
                return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }
        }
        
        for (size_t i = 2; i < tokens.size(); ++i) {
            value->list_val.push_back(tokens[i]);
        }
        
        return encode_integer(value->list_val.size());
    }
    
    std::string handle_lpop(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'lpop' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::LIST) {
            return "$-1\r\n";
        }
        
        if (it->second->list_val.empty()) {
            return "$-1\r\n";
        }
        
        std::string result = it->second->list_val.front();
        it->second->list_val.pop_front();
        return encode_bulk_string(result);
    }
    
    std::string handle_rpop(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'rpop' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::LIST) {
            return "$-1\r\n";
        }
        
        if (it->second->list_val.empty()) {
            return "$-1\r\n";
        }
        
        std::string result = it->second->list_val.back();
        it->second->list_val.pop_back();
        return encode_bulk_string(result);
    }
    
    std::string handle_llen(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'llen' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired()) {
            return encode_integer(0);
        }
        
        if (it->second->type != RedisValue::LIST) {
            return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
        }
        
        return encode_integer(it->second->list_val.size());
    }
    
    std::string handle_lrange(const std::vector<std::string>& tokens) {
        if (tokens.size() < 4) return encode_error("ERR wrong number of arguments for 'lrange' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::LIST) {
            return "*0\r\n";
        }
        
        try {
            int start = std::stoi(tokens[2]);
            int stop = std::stoi(tokens[3]);
            const auto& list = it->second->list_val;
            int size = list.size();
            
            if (start < 0) start += size;
            if (stop < 0) stop += size;
            
            if (start < 0) start = 0;
            if (stop >= size) stop = size - 1;
            
            std::vector<std::string> result;
            if (start <= stop) {
                auto it_start = list.begin();
                std::advance(it_start, start);
                auto it_stop = it_start;
                std::advance(it_stop, stop - start + 1);
                
                for (auto it_cur = it_start; it_cur != it_stop; ++it_cur) {
                    result.push_back(*it_cur);
                }
            }
            
            return encode_array(result);
        } catch (...) {
            return encode_error("ERR invalid range");
        }
    }
    
    std::string handle_hset(const std::vector<std::string>& tokens) {
        if (tokens.size() < 4 || tokens.size() % 2 != 0) {
            return encode_error("ERR wrong number of arguments for 'hset' command");
        }
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        std::shared_ptr<RedisValue> value;
        
        if (it == data.end() || it->second->is_expired()) {
            value = std::make_shared<RedisValue>(RedisValue::HASH);
            data[tokens[1]] = value;
        } else {
            value = it->second;
            if (value->type != RedisValue::HASH) {
                return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }
        }
        
        int added = 0;
        for (size_t i = 2; i < tokens.size(); i += 2) {
            if (value->hash_val.find(tokens[i]) == value->hash_val.end()) {
                added++;
            }
            value->hash_val[tokens[i]] = tokens[i + 1];
        }
        
        return encode_integer(added);
    }
    
    std::string handle_hget(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'hget' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::HASH) {
            return "$-1\r\n";
        }
        
        auto hash_it = it->second->hash_val.find(tokens[2]);
        if (hash_it == it->second->hash_val.end()) {
            return "$-1\r\n";
        }
        
        return encode_bulk_string(hash_it->second);
    }
    
    std::string handle_hdel(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'hdel' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::HASH) {
            return encode_integer(0);
        }
        
        int deleted = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (it->second->hash_val.erase(tokens[i]) > 0) {
                deleted++;
            }
        }
        
        return encode_integer(deleted);
    }
    
    std::string handle_hgetall(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'hgetall' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::HASH) {
            return "*0\r\n";
        }
        
        std::vector<std::string> result;
        for (const auto& pair : it->second->hash_val) {
            result.push_back(pair.first);
            result.push_back(pair.second);
        }
        
        return encode_array(result);
    }
    
    std::string handle_sadd(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'sadd' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        std::shared_ptr<RedisValue> value;
        
        if (it == data.end() || it->second->is_expired()) {
            value = std::make_shared<RedisValue>(RedisValue::SET);
            data[tokens[1]] = value;
        } else {
            value = it->second;
            if (value->type != RedisValue::SET) {
                return encode_error("WRONGTYPE Operation against a key holding the wrong kind of value");
            }
        }
        
        int added = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (value->set_val.insert(tokens[i]).second) {
                added++;
            }
        }
        
        return encode_integer(added);
    }
    
    std::string handle_srem(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'srem' command");
        
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::SET) {
            return encode_integer(0);
        }
        
        int removed = 0;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (it->second->set_val.erase(tokens[i]) > 0) {
                removed++;
            }
        }
        
        return encode_integer(removed);
    }
    
    std::string handle_smembers(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'smembers' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::SET) {
            return "*0\r\n";
        }
        
        std::vector<std::string> result(it->second->set_val.begin(), it->second->set_val.end());
        return encode_array(result);
    }
    
    std::string handle_scard(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) return encode_error("ERR wrong number of arguments for 'scard' command");
        
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        auto it = data.find(tokens[1]);
        if (it == data.end() || it->second->is_expired() || it->second->type != RedisValue::SET) {
            return encode_integer(0);
        }
        
        return encode_integer(it->second->set_val.size());
    }
    
    std::string handle_publish(const std::vector<std::string>& tokens) {
        if (tokens.size() < 3) return encode_error("ERR wrong number of arguments for 'publish' command");
        
        int count = pubsub_manager.publish(tokens[1], tokens[2]);
        return encode_integer(count);
    }
    
    std::string handle_info() {
        std::shared_lock<std::shared_mutex> lock(data_mutex);
        std::string info = "# Server\r\nredis_version:7.0.0-compatible\r\n";
        info += "# Clients\r\nconnected_clients:" + std::to_string(connection_pool.get_active_count()) + "\r\n";
        info += "# Memory\r\nused_memory:" + std::to_string(data.size() * sizeof(RedisValue)) + "\r\n";
        info += "# Keyspace\r\ndb0:keys=" + std::to_string(data.size()) + "\r\n";
        return encode_bulk_string(info);
    }
    
    std::string handle_flushall() {
        std::unique_lock<std::shared_mutex> lock(data_mutex);
        data.clear();
        return encode_simple_string("OK");
    }
    
public:
    RedisClone() : cleanup_thread(&RedisClone::cleanup_expired_keys, this) {}
    
    ~RedisClone() {
        running = false;
        if (cleanup_thread.joinable()) {
            cleanup_thread.join();
        }
    }
    
    void handle_client(int client_fd) {
        int conn_id = connection_pool.acquire_connection();
        if (conn_id == -1) {
            close(client_fd);
            return;
        }
        
        char buffer[4096];
        std::string command_buffer;
        
        while (true) {
            ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_read <= 0) break;
            
            buffer[bytes_read] = '\0';
            command_buffer += buffer;
            
            size_t pos;
            while ((pos = command_buffer.find("\r\n")) != std::string::npos) {
                std::string command = command_buffer.substr(0, pos);
                command_buffer.erase(0, pos + 2);
                
                if (!command.empty()) {
                    auto tokens = parse_command(command);
                    if (!tokens.empty()) {
                        std::string response = process_command(tokens);
                        send(client_fd, response.c_str(), response.length(), MSG_NOSIGNAL);
                    }
                }
            }
        }
        
        connection_pool.release_connection(conn_id);
        close(client_fd);
    }
    
    void start_server(int port) {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == -1) {
            perror("Socket creation failed");
            return;
        }
        
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            perror("Bind failed");
            close(server_fd);
            return;
        }
        
        if (listen(server_fd, 10) < 0) {
            perror("Listen failed");
            close(server_fd);
            return;
        }
        
        std::cout << "Redis clone server started on port " << port << std::endl;
        
        while (running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                std::thread client_thread(&RedisClone::handle_client, this, client_fd);
                client_thread.detach();
            }
        }
        
        close(server_fd);
    }
    
    void subscribe_client(int client_fd, const std::string& channel) {
        pubsub_manager.subscribe(channel, client_fd);
    }
    
    void unsubscribe_client(int client_fd, const std::string& channel) {
        pubsub_manager.unsubscribe(channel, client_fd);
    }
};

int main(int argc, char* argv[]) {
    int port = 6379;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    RedisClone server;
    server.start_server(port);
    
    return 0;
}