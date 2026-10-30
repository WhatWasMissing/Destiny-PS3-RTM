#include "app/settings.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>

#include "core/error.h"

namespace brps3 {

namespace {

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::filesystem::path iniPath() { return state_dir() / "settings.ini"; }

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out.push_back(c);
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out.push_back(s[i]);
            continue;
        }
        char n = s[++i];
        out.push_back(n == 'n' ? '\n' : n == 'r' ? '\r' : n);
    }
    return out;
}

std::vector<std::string> splitList(const std::string& v) {
    std::vector<std::string> out;
    if (v.empty()) return out;
    size_t start = 0;
    for (;;) {
        size_t end = v.find('|', start);
        out.push_back(v.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

std::string joinList(const std::vector<std::string>& values) {
    std::string out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out.push_back('|');
        for (char c : values[i])
            if (c != '|') out.push_back(c);
    }
    return out;
}

}   

void Settings::load() {
    std::lock_guard<std::mutex> lock(mutex_);
    kv_.clear();
    std::ifstream f(iniPath(), std::ios::binary);
    std::string line;
    while (f && std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        kv_[unescape(line.substr(0, eq))] = unescape(line.substr(eq + 1));
    }
    dirty_ = false;
}

void Settings::save() {
    std::map<std::string, std::string> sorted;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sorted.insert(kv_.begin(), kv_.end());
        dirty_ = false;
    }
    std::string text = "# Blackrock PS3 settings (key=value)\r\n";
    for (const auto& [k, v] : sorted) text += escape(k) + "=" + escape(v) + "\r\n";
    auto path = iniPath();
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(text.data(), std::streamsize(text.size()));
        if (!f) {
            coreLog("cannot write " + tmp.string());
            return;
        }
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        coreLog("cannot replace " + path.string());
}

bool Settings::saveIfDirty(double quietSeconds) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_ || nowSec() - changedAt_ < quietSeconds) return false;
    }
    save();
    return true;
}

std::string Settings::str(const std::string& key, const std::string& def) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = kv_.find(key);
    return it == kv_.end() ? def : it->second;
}

int Settings::integer(const std::string& key, int def) const {
    std::string v = str(key);
    if (v.empty()) return def;
    try {
        size_t used = 0;
        int n = std::stoi(v, &used, 10);
        return used == v.size() ? n : def;
    } catch (const std::exception&) {
        return def;
    }
}

float Settings::number(const std::string& key, float def) const {
    std::string v = str(key);
    if (v.empty()) return def;
    try {
        size_t used = 0;
        float n = std::stof(v, &used);
        return used == v.size() ? n : def;
    } catch (const std::exception&) {
        return def;
    }
}

bool Settings::flag(const std::string& key, bool def) const {
    std::string v = str(key);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return def;
}

std::vector<std::string> Settings::list(const std::string& key) const { return splitList(str(key)); }

void Settings::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = kv_.find(key);
    if (it != kv_.end() && it->second == value) return;
    kv_[key] = value;
    dirty_ = true;
    changedAt_ = nowSec();
}

void Settings::set(const std::string& key, const char* value) { set(key, std::string(value ? value : "")); }

void Settings::set(const std::string& key, int value) { set(key, std::to_string(value)); }

void Settings::set(const std::string& key, float value) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", double(value));
    set(key, std::string(buf));
}

void Settings::set(const std::string& key, bool value) { set(key, std::string(value ? "1" : "0")); }

void Settings::setList(const std::string& key, const std::vector<std::string>& values) { set(key, joinList(values)); }

bool Settings::inList(const std::string& key, const std::string& item) const {
    auto l = list(key);
    return std::find(l.begin(), l.end(), item) != l.end();
}

void Settings::toggleInList(const std::string& key, const std::string& item) {
    std::lock_guard<std::mutex> lock(mutex_);    
    auto it = kv_.find(key);
    auto l = splitList(it == kv_.end() ? std::string() : it->second);
    auto pos = std::find(l.begin(), l.end(), item);
    if (pos == l.end()) l.push_back(item);
    else l.erase(pos);
    kv_[key] = joinList(l);
    dirty_ = true;
    changedAt_ = nowSec();
}

void Settings::pushRecent(const std::string& key, const std::string& item, size_t max) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = kv_.find(key);
    auto l = splitList(it == kv_.end() ? std::string() : it->second);
    l.erase(std::remove(l.begin(), l.end(), item), l.end());
    l.insert(l.begin(), item);
    if (l.size() > max) l.resize(max);
    std::string joined = joinList(l);
    if (it != kv_.end() && it->second == joined) return;
    kv_[key] = joined;
    dirty_ = true;
    changedAt_ = nowSec();
}

}   
