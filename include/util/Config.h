#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

/**
 * @brief 简单 INI 风格配置文件读取器
 *
 * 格式：
 *   # 注释
 *   key = value
 *   key=value
 *
 * 不区分 section，只解析 key-value 对。
 */
class Config {
public:
    Config() = default;

    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            // 去除注释
            auto sharp = line.find('#');
            if (sharp != std::string::npos) line.erase(sharp);

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (!key.empty()) map_[key] = val;
        }
        return true;
    }

    std::string getString(const std::string& key,
                          const std::string& def = "") const {
        auto it = map_.find(key);
        return (it != map_.end()) ? it->second : def;
    }

    int getInt(const std::string& key, int def = 0) const {
        auto it = map_.find(key);
        if (it == map_.end()) return def;
        try { return std::stoi(it->second); }
        catch (...) { return def; }
    }

    bool getBool(const std::string& key, bool def = false) const {
        auto it = map_.find(key);
        if (it == map_.end()) return def;
        const auto& v = it->second;
        return (v == "true" || v == "1" || v == "yes" || v == "on");
    }

private:
    static std::string trim(std::string s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(),
            [](unsigned char c) { return !std::isspace(c); }));
        s.erase(std::find_if(s.rbegin(), s.rend(),
            [](unsigned char c) { return !std::isspace(c); }).base(), s.end());
        return s;
    }

    std::unordered_map<std::string, std::string> map_;
};
