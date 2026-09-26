// registry_mock.cpp - 内存模拟注册表 (Linux/macOS 演示 + Windows --mock)
#include "registry.hpp"
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>

struct MockKey {
    std::map<std::string, MockKey> subs;       // 子项 (key: 项名, UTF-8)
    std::map<std::string, RegValue> values;    // 值 (key: 值名, "" = 默认)
};

class MockRegistry : public IRegistry {
public:
    MockRegistry() { seedDemo(); }

    std::string backendName() const override { return "Mock Registry (内存演示数据)"; }

    std::vector<std::string> listRoots() override {
        std::vector<std::string> r;
        for (auto& kv : roots_) r.push_back(kv.first);
        return r;
    }
    bool exists(const std::string& fullPath) override { return find(fullPath) != nullptr; }
    std::vector<std::string> listSubkeys(const std::string& fullPath) override {
        MockKey* k = find(fullPath);
        if (!k) return {};
        std::vector<std::string> out;
        for (auto& kv : k->subs) out.push_back(kv.first);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b){ return toLowerStr(a) < toLowerStr(b); });
        return out;
    }
    std::vector<RegValue> listValues(const std::string& fullPath) override {
        MockKey* k = find(fullPath);
        std::vector<RegValue> out;
        if (!k) return out;
        for (auto& kv : k->values) out.push_back(kv.second);
        bool hasDefault = false;
        for (auto& v : out) if (v.name.empty()) hasDefault = true;
        if (!hasDefault) { RegValue d; d.name=""; d.type=REG_SZ_T; out.push_back(d); }
        std::sort(out.begin(), out.end(), [](const RegValue& a, const RegValue& b){
            if (a.name.empty() != b.name.empty()) return a.name.empty();
            return toLowerStr(a.name) < toLowerStr(b.name);
        });
        return out;
    }
    bool createKey(const std::string& fullPath, std::string& err) override {
        if (isRootPath(fullPath)) { err = "根项已存在"; return false; }
        std::string p = parentPath(fullPath), b = baseName(fullPath);
        if (b.empty() || b.find('\\') != std::string::npos) { err = "项名无效"; return false; }
        MockKey* pk = find(p);
        if (!pk) { err = "父项不存在"; return false; }
        if (pk->subs.count(b) == 0) pk->subs[b] = MockKey();
        return true;
    }
    bool deleteKeyTree(const std::string& fullPath, std::string& err) override {
        if (isRootPath(fullPath)) { err = "不能删除根项"; return false; }
        std::string p = parentPath(fullPath), b = baseName(fullPath);
        MockKey* pk = find(p);
        if (!pk || pk->subs.count(b) == 0) { err = "项不存在"; return false; }
        pk->subs.erase(b);
        return true;
    }
    bool setValue(const std::string& fullPath, const RegValue& v, std::string& err) override {
        MockKey* k = find(fullPath);
        if (!k) { err = "项不存在"; return false; }
        k->values[v.name] = v;
        return true;
    }
    bool deleteValue(const std::string& fullPath, const std::string& valueName, std::string& err) override {
        if (valueName.empty()) { err = "不能删除“(默认)”值, 只能清空它的内容"; return false; }
        MockKey* k = find(fullPath);
        if (!k) { err = "项不存在"; return false; }
        if (k->values.count(valueName) == 0) { err = "值不存在"; return false; }
        k->values.erase(valueName);
        return true;
    }
    bool renameValue(const std::string& fullPath, const std::string& oldName,
                     const std::string& newName, std::string& err) override {
        if (oldName.empty()) { err = "不能重命名“(默认)”值"; return false; }
        if (newName.empty()) { err = "新名称不能为空"; return false; }
        MockKey* k = find(fullPath);
        if (!k) { err = "项不存在"; return false; }
        if (k->values.count(oldName) == 0) { err = "找不到原值"; return false; }
        if (k->values.count(newName)) { err = "同名值已存在"; return false; }
        RegValue v = k->values[oldName];
        v.name = newName;
        k->values.erase(oldName);
        k->values[newName] = v;
        return true;
    }
    bool renameKey(const std::string& fullPath, const std::string& newName,
                   std::string& err) override {
        if (isRootPath(fullPath)) { err = "不能重命名根项"; return false; }
        if (newName.empty() || newName.find('\\') != std::string::npos) { err = "新名称无效"; return false; }
        std::string p = parentPath(fullPath), b = baseName(fullPath);
        MockKey* pk = find(p);
        if (!pk) { err = "父项不存在"; return false; }
        if (pk->subs.count(b) == 0) { err = "项不存在"; return false; }
        if (pk->subs.count(newName)) { err = "同名项已存在"; return false; }
        pk->subs[newName] = std::move(pk->subs[b]);
        pk->subs.erase(b);
        return true;
    }
    bool exportReg(const std::string& fullPath, const std::string& filePath, std::string& err) override {
        MockKey* k = find(fullPath);
        if (!k) { err = "项不存在"; return false; }
        std::ofstream f(trimStr(filePath), std::ios::binary);
        if (!f) { err = "无法写入文件"; return false; }
        f << "Windows Registry Editor Version 5.00\n\n";
        exportKey(f, fullPath, *k);
        return true;
    }

private:
    std::map<std::string, MockKey> roots_;

    MockKey* find(const std::string& fullPath) {
        std::string root, sub;
        splitRootPath(fullPath, root, sub);
        auto it = roots_.find(root);
        if (it == roots_.end()) return nullptr;
        MockKey* cur = &it->second;
        if (sub.empty()) return cur;
        for (auto& seg : splitBy(sub, '\\')) {
            auto jt = cur->subs.find(seg);
            if (jt == cur->subs.end()) return nullptr;
            cur = &jt->second;
        }
        return cur;
    }
    static std::string escReg(const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '\\') o += "\\\\";
            else if (c == '"') o += "\\\"";
            else o += c;
        }
        return o;
    }
    // UTF-8 -> UTF-16LE 字节 (用于 .reg 的 hex(2)/hex(7))
    static std::vector<uint8_t> utf8ToUtf16LE(const std::string& s) {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < s.size();) {
            size_t len = 1;
            uint32_t cp = utf8DecodeOne(s.c_str() + i, s.size() - i, len);
            i += len;
            if (cp < 0x10000) {
                out.push_back((uint8_t)(cp & 0xFF));
                out.push_back((uint8_t)((cp >> 8) & 0xFF));
            } else {
                uint32_t v = cp - 0x10000;
                uint32_t hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
                out.push_back((uint8_t)(hi & 0xFF)); out.push_back((uint8_t)((hi >> 8) & 0xFF));
                out.push_back((uint8_t)(lo & 0xFF)); out.push_back((uint8_t)((lo >> 8) & 0xFF));
            }
        }
        return out;
    }
    static void writeHexBytes(std::ofstream& f, const std::vector<uint8_t>& b) {
        for (size_t i = 0; i < b.size(); i++) {
            char buf[8]; snprintf(buf, sizeof(buf), "%02x", b[i]);
            f << buf;
            if (i + 1 < b.size()) f << ",";
            // 标准 .reg 每行用反斜杠续行; 这里简化: 每 25 字节换行续行
            if ((i + 1) % 25 == 0 && i + 1 < b.size()) f << "\\\n  ";
        }
        f << "\n";
    }
    void exportKey(std::ofstream& f, const std::string& path, MockKey& k) {
        f << "[" << path << "]\n";
        for (auto& kv : k.values) {
            const RegValue& v = kv.second;
            std::string vn = v.name.empty() ? "@" : ("\"" + escReg(v.name) + "\"");
            if (v.type == REG_SZ_T) f << vn << "=\"" << escReg(v.getString()) << "\"\n";
            else if (v.type == REG_DWORD_T) {
                char buf[32]; snprintf(buf, sizeof(buf), "dword:%08x", v.getDword());
                f << vn << "=" << buf << "\n";
            } else if (v.type == REG_QWORD_T) {
                char buf[64]; snprintf(buf, sizeof(buf), "hex(b):%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x",
                    v.data.size()>0?v.data[0]:0, v.data.size()>1?v.data[1]:0, v.data.size()>2?v.data[2]:0, v.data.size()>3?v.data[3]:0,
                    v.data.size()>4?v.data[4]:0, v.data.size()>5?v.data[5]:0, v.data.size()>6?v.data[6]:0, v.data.size()>7?v.data[7]:0);
                f << vn << "=" << buf << "\n";
            } else if (v.type == REG_EXPAND_SZ_T) {
                auto b = utf8ToUtf16LE(v.getString());
                b.push_back(0); b.push_back(0); // NUL 结尾
                f << vn << "=hex(2):";
                writeHexBytes(f, b);
            } else if (v.type == REG_MULTI_SZ_T) {
                std::vector<uint8_t> b;
                for (auto& ln : v.getMultiStrings()) {
                    auto u = utf8ToUtf16LE(ln);
                    b.insert(b.end(), u.begin(), u.end());
                    b.push_back(0); b.push_back(0);
                }
                b.push_back(0); b.push_back(0); // 双 NUL 结尾
                f << vn << "=hex(7):";
                writeHexBytes(f, b);
            } else {
                f << vn << "=hex:";
                writeHexBytes(f, v.data);
            }
        }
        f << "\n";
        for (auto& kv : k.subs) exportKey(f, path + "\\" + kv.first, kv.second);
    }

    void seedDemo() {
        roots_["HKEY_CLASSES_ROOT"] = MockKey();
        roots_["HKEY_CURRENT_USER"] = MockKey();
        roots_["HKEY_LOCAL_MACHINE"] = MockKey();
        roots_["HKEY_USERS"] = MockKey();
        roots_["HKEY_CURRENT_CONFIG"] = MockKey();
        std::string err;
        // 构造一些仿真数据, 结构类似真实注册表
        createKey("HKEY_CURRENT_USER\\Software", err);
        createKey("HKEY_CURRENT_USER\\Software\\Microsoft", err);
        createKey("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows", err);
        createKey("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion", err);
        createKey("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", err);
        createKey("HKEY_CURRENT_USER\\Software\\TuiRegedit", err);
        createKey("HKEY_CURRENT_USER\\Software\\TuiRegedit\\Demo", err);
        createKey("HKEY_LOCAL_MACHINE\\SOFTWARE", err);
        createKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft", err);
        createKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows", err);
        createKey("HKEY_LOCAL_MACHINE\\SOFTWARE\\TuiRegedit", err);
        createKey("HKEY_CLASSES_ROOT\\.txt", err);
        createKey("HKEY_USERS\\.DEFAULT", err);
        createKey("HKEY_CURRENT_CONFIG\\System", err);

        RegValue v;
        v.name = ""; v.type = REG_SZ_T; v.setString("默认示例值");
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "UserName"; v.type = REG_SZ_T; v.setString("张三");
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "InstallPath"; v.type = REG_EXPAND_SZ_T; v.setString("%ProgramFiles%\\TuiRegedit");
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "Enabled"; v.type = REG_DWORD_T; v.setDword(1);
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "RetryCount"; v.type = REG_DWORD_T; v.setDword(3);
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "BigNumber"; v.type = REG_QWORD_T; v.setQword(123456789012345ULL);
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "Blob"; v.type = REG_BINARY_T; v.data = {0x01,0x02,0xAB,0xCD,0xEF,0x00,0x42};
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);
        v.name = "Servers"; v.type = REG_MULTI_SZ_T; v.setMultiStrings({"server1.local","server2.local","192.168.1.10"});
        setValue("HKEY_CURRENT_USER\\Software\\TuiRegedit", v, err);

        v.name = "OneDrive"; v.type = REG_SZ_T; v.setString("\"C:\\Program Files\\OneDrive.exe\" /background");
        setValue("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", v, err);

        v.name = ""; v.type = REG_SZ_T; v.setString("txtfile");
        setValue("HKEY_CLASSES_ROOT\\.txt", v, err);

        v.name = "Version"; v.type = REG_SZ_T; v.setString("1.0.0-demo");
        setValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\TuiRegedit", v, err);
        v.name = "Port"; v.type = REG_DWORD_T; v.setDword(8080);
        setValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\TuiRegedit", v, err);
    }
};

std::unique_ptr<IRegistry> createMockRegistry() {
    return std::make_unique<MockRegistry>();
}

#ifndef _WIN32
std::unique_ptr<IRegistry> createRegistry() {
    return std::make_unique<MockRegistry>();
}
#endif
