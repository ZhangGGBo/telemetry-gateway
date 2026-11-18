#include "core.hpp"
#include <boost/beast.hpp>
#include <iostream>
namespace beast = boost::beast;
namespace http = beast::http;
namespace {
class DeviceSession : public std::enable_shared_from_this<DeviceSession> {
    tcp::socket socket_;
    asio::steady_timer timer_;
    asio::streambuf buffer_{4096};
    Runtime& r_;
    std::string reply_;
    void close(){ boost::system::error_code ec; timer_.cancel(); socket_.close(ec); }
    void read(){
        timer_.expires_after(std::chrono::seconds(r_.config.timeout_seconds));
        timer_.async_wait([self=shared_from_this()](auto ec){ if(!ec) self->close(); });
        asio::async_read_until(socket_,buffer_,'\n',[self=shared_from_this()](auto ec,std::size_t){
            if(ec) { self->close(); return; }
            std::istream in(&self->buffer_); std::string line; std::getline(in,line);
            try {
                auto reading=parse_reading(line);
                if(self->r_.pending>=256) { self->reply(false,"Server busy; retry later"); return; }
                ++self->r_.pending;
                asio::post(self->r_.database_worker,[self,reading]{
                    std::string error;
                    try {self->r_.store.insert(reading,self->r_.config);} catch(const std::exception& e){error=e.what(); std::cerr<<"[database] "<<error<<'\n';}
                    asio::post(self->r_.io,[self,error]{ --self->r_.pending; self->reply(error.empty(),error.empty()?"stored":"Storage failed"); });
                });
            } catch(const std::exception&){self->reply(false,"Invalid telemetry; check schema and ranges");}
        });
    }
    void reply(bool ok,const std::string& message){
        reply_=json::serialize(json::object{{"ok",ok},{"message",message}})+"\n";
        asio::async_write(socket_,asio::buffer(reply_),[self=shared_from_this()](auto ec,std::size_t){if(ec) self->close(); else self->read();});
    }
public:
    DeviceSession(tcp::socket s,Runtime& r):socket_(std::move(s)),timer_(r.io),r_(r){}
    void run(){read();}
};
const char* dashboard=R"HTML(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>设备采集监控</title>
<style>body{background:#101827;color:#e6edf7;font:16px system-ui;margin:40px auto;max-width:1100px;padding:0 20px}h1{font-size:30px}p{color:#a7b8cd}table{width:100%;border-collapse:collapse;background:#182438}td,th{text-align:left;padding:14px;border-bottom:1px solid #30405a}button{background:#294263;color:white;border:0;padding:8px;cursor:pointer} .alarm{color:#ffac70}pre{white-space:pre-wrap}#error{color:#ffac70}</style>
<h1>Telemetry · 设备采集监控</h1><p>每两秒更新 · 点击设备查看最近 50 条记录 · 超时未收到数据即标记离线</p><p id="summary">等待数据</p><p id="error"></p><table><thead><tr><th>设备</th><th>状态</th><th>温度 °C</th><th>压力 bar</th><th>告警</th><th>最后上报</th></tr></thead><tbody id="rows"></tbody></table><h2 id="title">历史记录</h2><pre id="history">选择一台设备</pre>
<script>let selected='';async function get(url){const r=await fetch(url);if(!r.ok)throw Error('HTTP '+r.status);return r.json()}async function refresh(){try{const data=await get('/api/devices');document.getElementById('summary').textContent=`设备 ${data.length} · 在线 ${data.filter(d=>d.online).length} · 最近读数告警 ${data.filter(d=>d.alarm).length}`;const rows=document.getElementById('rows');rows.replaceChildren();for(const d of data){const tr=document.createElement('tr');const td=document.createElement('td');const b=document.createElement('button');b.textContent=d.device;b.onclick=()=>{selected=d.device;refresh()};td.append(b);tr.append(td);for(const value of [d.online?'在线':'离线',d.temperature.toFixed(1),d.pressure.toFixed(2),d.alarm?'超阈值':'正常',new Date(d.received_at*1000).toLocaleTimeString()]){const c=document.createElement('td');c.textContent=value;if(d.alarm)c.className='alarm';tr.append(c)}rows.append(tr)}if(selected){document.getElementById('title').textContent=selected+' · 最近 50 条';const h=await get('/api/history?device='+encodeURIComponent(selected)+'&limit=50');document.getElementById('history').textContent=h.map(d=>`${new Date(d.received_at*1000).toLocaleString()}  #${d.sequence}  ${d.temperature} °C  ${d.pressure} bar${d.alarm?'  ALARM':''}`).join('\n')}document.getElementById('error').textContent=''}catch(e){document.getElementById('error').textContent='连接失败：'+e.message}finally{setTimeout(refresh,2000)}}refresh();</script></html>)HTML";
class HttpSession : public std::enable_shared_from_this<HttpSession> {
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request_parser<http::string_body> parser_;
    http::response<http::string_body> response_;
    Runtime& r_;
    void send(http::status status,std::string body,bool html=false){
        response_={status,11};response_.set(http::field::content_type,html?"text/html; charset=utf-8":"application/json");
        response_.set(http::field::cache_control,"no-store"); response_.keep_alive(false);response_.body()=std::move(body);response_.prepare_payload();
        stream_.expires_after(std::chrono::seconds(5));
        http::async_write(stream_,response_,[self=shared_from_this()](auto,std::size_t){boost::system::error_code ec;self->stream_.socket().shutdown(tcp::socket::shutdown_both,ec);});
    }
    void route(){
        auto& request=parser_.get();
        if(request.method()!=http::verb::get){send(http::status::method_not_allowed,"{\"error\":\"GET only\"}");return;}
        std::string target(request.target());
        if(target=="/"){send(http::status::ok,dashboard,true);return;}
        if(target=="/health"){send(http::status::ok,"{\"status\":\"ok\"}");return;}
        std::string device;int limit=50;
        if(target!="/api/devices"){
            const std::string prefix="/api/history?device=";
            if(target.rfind(prefix,0)!=0){send(http::status::not_found,"{\"error\":\"Not found\"}");return;}
            auto query=target.substr(prefix.size());auto amp=query.find('&');device=query.substr(0,amp);
            try{
                if(device.empty()||device.size()>64||device.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos)throw std::runtime_error("id");
                if(amp!=std::string::npos){auto tail=query.substr(amp);if(tail.rfind("&limit=",0)!=0)throw std::runtime_error("query");auto digits=tail.substr(7);if(digits.empty()||digits.size()>3||digits.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("limit");limit=std::stoi(digits);if(limit<1||limit>500)throw std::runtime_error("limit");}
            }catch(...){send(http::status::bad_request,"{\"error\":\"Invalid device or limit (1..500)\"}");return;}
        }
        if(r_.pending>=256){send(http::status::service_unavailable,"{\"error\":\"Busy\"}");return;}
        ++r_.pending;
        asio::post(r_.database_worker,[self=shared_from_this(),device,limit]{
            std::string body;bool ok=true;
            try{body=json::serialize(device.empty()?self->r_.store.devices(self->r_.config.timeout_seconds):self->r_.store.history(device,limit));}
            catch(const std::exception& e){std::cerr<<"[database] "<<e.what()<<'\n';body="{\"error\":\"Storage failed\"}";ok=false;}
            asio::post(self->r_.io,[self,body=std::move(body),ok]()mutable{--self->r_.pending;self->send(ok?http::status::ok:http::status::internal_server_error,std::move(body));});
        });
    }
public:
    HttpSession(tcp::socket s,Runtime& r):stream_(std::move(s)),r_(r){parser_.body_limit(0);parser_.header_limit(8192);}
    void run(){stream_.expires_after(std::chrono::seconds(5));http::async_read(stream_,buffer_,parser_,[self=shared_from_this()](auto ec,std::size_t){if(!ec)self->route();});}
};
class Listener : public std::enable_shared_from_this<Listener>{
    tcp::acceptor acceptor_;Runtime& r_;bool http_;std::size_t active_=0;
public:
    Listener(Runtime& r,unsigned short port,bool web):acceptor_(r.io),r_(r),http_(web){
        tcp::endpoint endpoint(asio::ip::make_address(r.config.host),port);
        acceptor_.open(endpoint.protocol());acceptor_.set_option(tcp::acceptor::reuse_address(true));acceptor_.bind(endpoint);acceptor_.listen();
    }
    void run(){acceptor_.async_accept([self=shared_from_this()](auto ec,tcp::socket socket){
        if(!ec){if(self->http_)std::make_shared<HttpSession>(std::move(socket),self->r_)->run();else std::make_shared<DeviceSession>(std::move(socket),self->r_)->run();}
        else if(ec!=asio::error::operation_aborted)std::cerr<<"[accept] "<<ec.message()<<'\n';
        if(self->acceptor_.is_open()) self->run();
    });}
};
}
void start_ingestion(Runtime& r){std::make_shared<Listener>(r,r.config.tcp_port,false)->run();}
void start_http(Runtime& r){std::make_shared<Listener>(r,r.config.http_port,true)->run();}
