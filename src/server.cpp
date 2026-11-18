#include "core.hpp"
#include <iostream>
#include <csignal>
int main(int argc,char** argv){
    try {
        Runtime runtime(load_config(argc>1?argv[1]:"config/server.json"));
        start_ingestion(runtime);start_http(runtime);
        asio::signal_set signals(runtime.io,SIGINT,SIGTERM);
        signals.async_wait([&](auto,int){std::cout<<"Stopping; draining accepted database work...\n";runtime.io.stop();});
        std::cout<<"Telemetry TCP "<<runtime.config.host<<":"<<runtime.config.tcp_port<<"\nDashboard http://"<<runtime.config.host<<":"<<runtime.config.http_port<<"\n"<<std::flush;
        runtime.io.run();runtime.database_worker.join();
        std::cout<<"Stopped.\n";
    }catch(const std::exception& e){std::cerr<<"Fatal: "<<e.what()<<'\n';return 1;}
}
