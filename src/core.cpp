#include "core.hpp"
#include <boost/json/src.hpp>
#include <fstream>
#include <cmath>
#include <cctype>
#include <sstream>
Config load_config(const std::string& path) {
    std::ifstream in(path); if (!in) throw std::runtime_error("Cannot open config: " + path);
    std::ostringstream text; text << in.rdbuf();
    auto value = json::parse(text.str()); auto& o = value.as_object();
    Config c;
    auto port = [&](const char* key) { auto n = o.at(key).as_int64(); if(n<1 || n>65535) throw std::runtime_error("Invalid port"); return static_cast<unsigned short>(n); };
    c.host = std::string(o.at("host").as_string()); c.database = std::string(o.at("database").as_string());
    c.tcp_port = port("tcp_port"); c.http_port = port("http_port");
    auto timeout = o.at("timeout_seconds").as_int64();
    if(timeout < 1 || timeout > 3600) throw std::runtime_error("Invalid timeout");
    c.timeout_seconds = static_cast<int>(timeout);
    c.temperature_limit = json::value_to<double>(o.at("temperature_limit"));
    c.pressure_limit = json::value_to<double>(o.at("pressure_limit"));
    if (!std::isfinite(c.temperature_limit) || !std::isfinite(c.pressure_limit) || c.tcp_port == c.http_port) throw std::runtime_error("Invalid configuration");
    asio::ip::make_address(c.host);
    return c;
}
Reading parse_reading(const std::string& line) {
    auto v = json::parse(line); const auto& o = v.as_object();
    Reading r{std::string(o.at("device").as_string()), json::value_to<double>(o.at("temperature")), json::value_to<double>(o.at("pressure")), o.at("sequence").as_int64()};
    if(r.device.empty() || r.device.size()>64) throw std::runtime_error("Device ID length must be 1..64");
    for(unsigned char ch : r.device) if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9')||ch=='_'||ch=='-')) throw std::runtime_error("Invalid device ID");
    if(!std::isfinite(r.temperature) || !std::isfinite(r.pressure) || r.temperature < -100 || r.temperature > 1000 || r.pressure < 0 || r.pressure > 1000 || r.sequence < 0) throw std::runtime_error("Reading outside accepted range");
    return r;
}
namespace {
struct Statement {
    sqlite3_stmt* p = nullptr;
    explicit Statement(sqlite3* db, const char* sql) {
        if(sqlite3_prepare_v2(db, sql, -1, &p, nullptr)!=SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement(){ sqlite3_finalize(p); }
    void text(int index, const std::string& value) { if(sqlite3_bind_text(p,index,value.c_str(),-1,SQLITE_TRANSIENT)!=SQLITE_OK) throw std::runtime_error("SQL bind failed"); }
};
void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if(sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK) {
        std::string message = error ? error : "SQLite error"; sqlite3_free(error); throw std::runtime_error(message);
    }
}
json::object row(sqlite3_stmt* p) {
    return {{"device", reinterpret_cast<const char*>(sqlite3_column_text(p,0))}, {"temperature",sqlite3_column_double(p,1)}, {"pressure",sqlite3_column_double(p,2)}, {"sequence",sqlite3_column_int64(p,3)}, {"received_at",sqlite3_column_int64(p,4)}, {"alarm", sqlite3_column_int(p,5)!=0}};
}
}
Store::Store(const std::string& path) {
    if(sqlite3_open(path.c_str(), &db_)!=SQLITE_OK) { std::string error=sqlite3_errmsg(db_); sqlite3_close(db_); db_=nullptr; throw std::runtime_error(error); }
    try {
        sqlite3_busy_timeout(db_,3000);
        exec(db_,"PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL; CREATE TABLE IF NOT EXISTS readings(id INTEGER PRIMARY KEY,device TEXT NOT NULL,temperature REAL NOT NULL,pressure REAL NOT NULL,sequence INTEGER NOT NULL,received_at INTEGER NOT NULL,alarm INTEGER NOT NULL); CREATE INDEX IF NOT EXISTS device_history ON readings(device,id DESC);");
    } catch (...) { sqlite3_close(db_); db_=nullptr; throw; }
}
Store::~Store(){ sqlite3_close(db_); }
void Store::insert(const Reading& r, const Config& c) {
    Statement s(db_,"INSERT INTO readings(device,temperature,pressure,sequence,received_at,alarm) VALUES(?,?,?,?,?,?)");
    s.text(1,r.device); sqlite3_bind_double(s.p,2,r.temperature); sqlite3_bind_double(s.p,3,r.pressure);
    sqlite3_bind_int64(s.p,4,r.sequence); sqlite3_bind_int64(s.p,5,now_seconds());
    sqlite3_bind_int(s.p,6,r.temperature>=c.temperature_limit || r.pressure>=c.pressure_limit);
    if(sqlite3_step(s.p)!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
}
json::array Store::devices(int timeout) {
    Statement s(db_,"SELECT device,temperature,pressure,sequence,received_at,alarm FROM readings WHERE id IN(SELECT MAX(id) FROM readings GROUP BY device) ORDER BY device LIMIT 1000");
    json::array result; int rc;
    while((rc=sqlite3_step(s.p))==SQLITE_ROW) { auto o=row(s.p); o["online"] = now_seconds()-sqlite3_column_int64(s.p,4)<timeout; result.push_back(std::move(o)); }
    if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return result;
}
json::array Store::history(const std::string& id,int limit) {
    Statement s(db_,"SELECT device,temperature,pressure,sequence,received_at,alarm FROM readings WHERE device=? ORDER BY id DESC LIMIT ?"); s.text(1,id); sqlite3_bind_int(s.p,2,limit);
    json::array result; int rc;
    while((rc=sqlite3_step(s.p))==SQLITE_ROW) result.push_back(row(s.p));
    if(rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return result;
}

