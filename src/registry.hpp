// registry.hpp - 注册表抽象接口
// 约定:
//  - 所有字符串均为 UTF-8
//  - fullPath 形如 "HKEY_CURRENT_USER\Software\Microsoft"
//  - RegValue.name == "" 表示 "(默认)" 值
//  - SZ / EXPAND_SZ: data 为 UTF-8 字节 (不含结尾 NUL)
//  - MULTI_SZ: data 为 UTF-8, 行与行之间用 '\n' 分隔
//  - DWORD: 4 字节小端; QWORD: 8 字节小端; BINARY: 原始字节
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "util.hpp"

// 与 Win32 REG_* 对齐的类型常量 (避免非 Windows 也要包含 windows.h)
enum RegType : uint32_t {
    REG_NONE_T       = 0,
    REG_SZ_T         = 1,
    REG_EXPAND_SZ_T  = 2,
    REG_BINARY_T     = 3,
    REG_DWORD_T      = 4,
    REG_MULTI_SZ_T   = 7,
    REG_QWORD_T      = 11,
};

struct RegValue {
    std::string name;           // UTF-8, "" = (默认)
    uint32_t type = REG_SZ_T;
    std::vector<uint8_t> data;

    std::string displayName() const { return name.empty() ? "(默认)" : name; }

    std::string typeName() const {
        switch (type) {
            case REG_SZ_T: return "REG_SZ";
            case REG_EXPAND_SZ_T: return "REG_EXPAND_SZ";
            case REG_BINARY_T: return "REG_BINARY";
            case REG_DWORD_T: return "REG_DWORD";
            case REG_MULTI_SZ_T: return "REG_MULTI_SZ";
            case REG_QWORD_T: return "REG_QWORD";
            case REG_NONE_T: return "REG_NONE";
            default: return "REG_BINARY";
        }
    }

    std::string getString() const { return std::string(data.begin(), data.end()); }
    void setString(const std::string& s) { data.assign(s.begin(), s.end()); }

    uint32_t getDword() const {
        uint32_t v = 0;
        for (size_t i = 0; i < 4 && i < data.size(); i++) v |= ((uint32_t)data[i] << (i * 8));
        return v;
    }
    void setDword(uint32_t v) {
        data.resize(4);
        for (int i = 0; i < 4; i++) data[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
    uint64_t getQword() const {
        uint64_t v = 0;
        for (size_t i = 0; i < 8 && i < data.size(); i++) v |= ((uint64_t)data[i] << (i * 8));
        return v;
    }
    void setQword(uint64_t v) {
        data.resize(8);
        for (int i = 0; i < 8; i++) data[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
    std::vector<std::string> getMultiStrings() const {
        std::string s(data.begin(), data.end());
        if (s.empty()) return {};
        return splitBy(s, '\n');
    }
    void setMultiStrings(const std::vector<std::string>& lines) {
        std::string s;
        for (size_t i = 0; i < lines.size(); i++) {
            if (i) s.push_back('\n');
            s += lines[i];
        }
        data.assign(s.begin(), s.end());
    }

    // 列表中显示的数据摘要
    std::string prettyData(size_t maxLen = 48) const {
        std::string s;
        switch (type) {
            case REG_SZ_T:
            case REG_EXPAND_SZ_T: {
                s = getString();
                // 去掉换行以免破坏表格
                for (auto& c : s) if (c == '\n' || c == '\r') c = ' ';
                if (s.empty()) s = "(空)";
                else s = "\"" + s + "\"";
                break;
            }
            case REG_DWORD_T: s = u32ToHex(getDword()); break;
            case REG_QWORD_T: s = u64ToHex(getQword()); break;
            case REG_MULTI_SZ_T: {
                auto lines = getMultiStrings();
                for (auto& c : s) (void)c;
                s.clear();
                for (size_t i = 0; i < lines.size(); i++) {
                    if (i) s += " | ";
                    s += lines[i];
                }
                if (s.empty()) s = "(空)";
                break;
            }
            default: s = bytesToHex(data, true, 16); if (s.empty()) s = "(空二进制)"; break;
        }
        // 按字节粗略截断 (显示时再按显示宽度截断)
        if (s.size() > maxLen * 3) s = s.substr(0, maxLen * 3) + "...";
        return s;
    }
};

class IRegistry {
public:
    virtual ~IRegistry() = default;
    virtual std::string backendName() const = 0;
    virtual std::vector<std::string> listRoots() = 0;
    virtual bool exists(const std::string& fullPath) = 0;
    virtual std::vector<std::string> listSubkeys(const std::string& fullPath) = 0;
    virtual std::vector<RegValue> listValues(const std::string& fullPath) = 0;
    virtual bool createKey(const std::string& fullPath, std::string& err) = 0;
    virtual bool deleteKeyTree(const std::string& fullPath, std::string& err) = 0;
    virtual bool setValue(const std::string& fullPath, const RegValue& v, std::string& err) = 0;
    virtual bool deleteValue(const std::string& fullPath, const std::string& valueName, std::string& err) = 0;
    virtual bool renameValue(const std::string& fullPath, const std::string& oldName,
                             const std::string& newName, std::string& err) = 0;
    virtual bool renameKey(const std::string& fullPath, const std::string& newName,
                           std::string& err) = 0;
    virtual bool exportReg(const std::string& fullPath, const std::string& filePath, std::string& err) = 0;
};

// 真实注册表 (Windows) / 模拟注册表 (Linux 演示与 --mock)
std::unique_ptr<IRegistry> createRegistry();
std::unique_ptr<IRegistry> createMockRegistry();

// 路径工具
inline void splitRootPath(const std::string& fullPath, std::string& root, std::string& sub) {
    auto pos = fullPath.find('\\');
    if (pos == std::string::npos) { root = fullPath; sub.clear(); }
    else { root = fullPath.substr(0, pos); sub = fullPath.substr(pos + 1); }
}
inline std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + "\\" + b;
}
inline std::string parentPath(const std::string& fullPath) {
    auto pos = fullPath.rfind('\\');
    if (pos == std::string::npos) return "";
    return fullPath.substr(0, pos);
}
inline std::string baseName(const std::string& fullPath) {
    auto pos = fullPath.rfind('\\');
    if (pos == std::string::npos) return fullPath;
    return fullPath.substr(pos + 1);
}
inline bool isRootPath(const std::string& fullPath) {
    return fullPath.find('\\') == std::string::npos;
}
