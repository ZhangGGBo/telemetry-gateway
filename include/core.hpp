#pragma once
#include <boost/asio.hpp>
#include <boost/json.hpp>
#include <sqlite3.h>
#include <chrono>
#include <memory>
#include <string>
#include <stdexcept>
namespace asio = boost::asio;
namespace json = boost::json;
using tcp = asio::ip::tcp;
inline long long now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
struct Config {
    std::string host = "127.0.0.1", database = "telemetry.db";
    unsigned short tcp_port = 9000, http_port = 8080;
    int timeout_seconds = 10;
    double temperature_limit = 80, pressure_limit = 10;
};
Config load_config(const std::string& path);
struct Reading { std::string device; double temperature, pressure; long long sequence; };
Reading parse_reading(const std::string& line);
class Store {
    sqlite3* db_ = nullptr;
public:
    explicit Store(const std::string& path);
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    void insert(const Reading& reading, const Config& config);
    json::array devices(int timeout);
    json::array history(const std::string& id, int limit);
};
struct Runtime {
    asio::io_context io;
    asio::thread_pool database_worker{1};
    Store store;
    Config config;
    std::size_t pending = 0;
    explicit Runtime(Config c) : store(c.database), config(std::move(c)) {}
};
void start_ingestion(Runtime& runtime);
void start_http(Runtime& runtime);
