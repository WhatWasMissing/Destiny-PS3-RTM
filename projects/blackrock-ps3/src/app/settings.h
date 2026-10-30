#pragma once
 
 
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace brps3 {

class Settings {
public:
    void load();
    void save();                         
    bool saveIfDirty(double quietSeconds = 1.0);    

    std::string str(const std::string& key, const std::string& def = "") const;
    int integer(const std::string& key, int def = 0) const;
    float number(const std::string& key, float def = 0) const;
    bool flag(const std::string& key, bool def = false) const;
    std::vector<std::string> list(const std::string& key) const;

    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, const char* value);    
    void set(const std::string& key, int value);
    void set(const std::string& key, float value);
    void set(const std::string& key, bool value);
    void setList(const std::string& key, const std::vector<std::string>& values);

     
    bool inList(const std::string& key, const std::string& item) const;
    void toggleInList(const std::string& key, const std::string& item);
    void pushRecent(const std::string& key, const std::string& item, size_t max = 20);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> kv_;
    bool dirty_ = false;
    double changedAt_ = 0;               
};

}   
