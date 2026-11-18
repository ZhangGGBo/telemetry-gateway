#include "core.hpp"
#include <iostream>
#include <filesystem>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){
 try{
    const std::string valid=R"({"device":"pump-1","temperature":85.5,"pressure":3.2,"sequence":1})";
    auto r=parse_reading(valid);check(r.device=="pump-1","parse ID");
    for(const std::string bad : {"{}", R"({"device":"<script>","temperature":1,"pressure":1,"sequence":0})", R"({"device":"x","temperature":1,"pressure":-1,"sequence":0})", R"({"device":"x","temperature":1,"pressure":1,"sequence":-1})"}) {
        bool rejected=false;try{parse_reading(bad);}catch(...){rejected=true;}check(rejected,"invalid reading accepted");
    }
    {Store store(":memory:");Config c;store.insert(r,c);auto d=store.devices(10);check(d.size()==1,"device count");check(d[0].as_object().at("alarm").as_bool(),"alarm missing");check(d[0].as_object().at("online").as_bool(),"online missing");r.temperature=20;store.insert(r,c);check(store.devices(10).size()==1,"latest device dedup");check(store.history("pump-1",1).size()==1,"history limit");check(store.history("' OR 1=1 --",50).empty(),"SQL parameter binding");}
    const auto path=(std::filesystem::temp_directory_path()/("telemetry-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".db")).string();
    {Store store(path);store.insert(r,Config{});} {Store store(path);check(store.history("pump-1",50).size()==1,"restart persistence");}
    std::filesystem::remove(path);std::cout<<"All core tests passed\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
