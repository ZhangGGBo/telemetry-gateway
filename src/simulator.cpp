#include "core.hpp"
#include <iostream>
#include <thread>
#include <random>
#include <atomic>
#include <csignal>
std::atomic<bool> stopped{false};
int main(int argc,char** argv){
    try{
        std::string host=argc>1?argv[1]:"127.0.0.1";std::string port=argc>2?argv[2]:"9000";
        std::string id=argc>3?argv[3]:"sensor-001";int count=argc>4?std::stoi(argv[4]):0;
        if(count<0)throw std::runtime_error("count must be >=0");
        parse_reading(json::serialize(json::object{{"device",id},{"temperature",20},{"pressure",1},{"sequence",0}}));
        std::signal(SIGINT,[](int){stopped=true;});
        asio::io_context io;tcp::resolver resolver(io);std::mt19937 random(std::random_device{}());
        std::uniform_real_distribution<double> temp(25,95),pressure(2,12);
        long long sequence=0;int sent=0;
        while(!stopped&&(count==0||sent<count)){
            try{
                tcp::socket socket(io);asio::connect(socket,resolver.resolve(host,port));
                std::cout<<"Connected as "<<id<<'\n';asio::streambuf buffer(4096);
                while(!stopped&&(count==0||sent<count)){
                    json::object reading{{"device",id},{"temperature",temp(random)},{"pressure",pressure(random)},{"sequence",sequence}};
                    auto line=json::serialize(reading)+"\n";parse_reading(line);
                    asio::write(socket,asio::buffer(line));asio::read_until(socket,buffer,'\n');
                    std::istream in(&buffer);std::string reply;std::getline(in,reply);
                    if(!json::parse(reply).as_object().at("ok").as_bool())throw std::runtime_error(reply);
                    std::cout<<"ACK #"<<sequence<<'\n';++sequence;++sent;
                    for(int tick=0;tick<10&&!stopped;++tick)std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }catch(const std::exception& e){std::cerr<<"Retry in 2s: "<<e.what()<<'\n';for(int tick=0;tick<20&&!stopped;++tick)std::this_thread::sleep_for(std::chrono::milliseconds(100));}
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

