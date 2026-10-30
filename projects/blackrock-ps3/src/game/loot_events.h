#pragma once
 
 
 
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace brps3 {
class Runtime;
}
namespace brps3::cmd {
class Handler;
}

namespace brps3::loot_events {

struct Entry {
    uint32_t hash = 0;
    int type = 0;                        
    uint32_t schema = 0;
    uint32_t size = 0;                   
    std::string label;                   
    std::string presets;                 
};

struct Field {
    uint32_t offset;
    int width;                           
    uint32_t value;
};

 
 
const std::vector<Entry>& catalog();
const Entry* find(uint32_t hash);
 
void setCatalogPath(const std::filesystem::path& tsv);
std::vector<Entry> loadCatalog(const std::filesystem::path& tsv);    
std::vector<Field> parseFields(const std::string& text);      
std::string formatFields(const std::vector<Field>& fields);

using Progress = std::function<void(const std::string&)>;
using Cancel = std::function<bool()>;

 
 
int call(Runtime& rt, const Entry& e, const std::vector<Field>& fields, int count, Progress progress, Cancel cancel);
 
 
 
 
int callInGame(Runtime& rt, cmd::Handler& h, const Entry& e, const std::vector<Field>& fields, int count, Progress progress, Cancel cancel);
 
void validateCall(const Entry& e, const std::vector<Field>& fields, int count);
 
int replay(Runtime& rt, const Entry& e, int count, double timeoutSec, Progress progress, Cancel cancel);

 
 
bool gather(Runtime& rt, uint32_t selector, std::string& message);

 
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> callProgram(const Entry& e, const std::vector<Field>& fields,
                                                                  int count, uint32_t cave, uint32_t ctrl, uint32_t tail = 0);
std::vector<uint8_t> gatherProgram(uint32_t cave, uint32_t ctrl, uint32_t tail = 0);
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> replayProgram(const Entry& e, int count, uint32_t cave, uint32_t ctrl);

}   
