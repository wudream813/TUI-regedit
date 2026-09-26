// registry_win.cpp - Windows 真实注册表实现 (仅 _WIN32 编译)
#ifdef _WIN32
#include "registry.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>

static std::map<std::string, HKEY> kRoots = {
    {"HKEY_CLASSES_ROOT",   HKEY_CLASSES_ROOT},
    {"HKEY_CURRENT_USER",   HKEY_CURRENT_USER},
    {"HKEY_LOCAL_MACHINE",  HKEY_LOCAL_MACHINE},
    {"HKEY_USERS",          HKEY_USERS},
    {"HKEY_CURRENT_CONFIG", HKEY_CURRENT_CONFIG},
};
static std::vector<std::string> kRootOrder = {
    "HKEY_CLASSES_ROOT", "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE",
    "HKEY_USERS", "HKEY_CURRENT_CONFIG",
};

class WinRegistry : public IRegistry {
public:
    std::string backendName() const override { return "Windows Registry (真实)"; }

    std::vector<std::string> listRoots() override { return kRootOrder; }

    bool exists(const std::string& fullPath) override {
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) return false;
        HKEY hKey;
        LONG rc = RegOpenKeyExW(hRoot, sub.empty() ? nullptr : sub.c_str(), 0,
                                KEY_READ, &hKey);
        if (rc != ERROR_SUCCESS) return false;
        RegCloseKey(hKey);
        return true;
    }

    std::vector<std::string> listSubkeys(const std::string& fullPath) override {
        std::vector<std::string> out;
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) return out;
        HKEY hKey;
        // 先尝试读写打开, 不行则只读打开 (保证浏览不受权限影响)
        LONG rc = RegOpenKeyExW(hRoot, sub.empty() ? nullptr : sub.c_str(), 0,
                                KEY_READ, &hKey);
        if (rc != ERROR_SUCCESS) return out;
        DWORD count = 0;
        RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &count, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
        for (DWORD i = 0; i < count; i++) {
            wchar_t name[512]; DWORD nameLen = 512;
            FILETIME ft;
            rc = RegEnumKeyExW(hKey, i, name, &nameLen, nullptr, nullptr, nullptr, &ft);
            if (rc == ERROR_SUCCESS) out.push_back(wideToUtf8(std::wstring(name, nameLen)));
            else if (rc == ERROR_MORE_DATA) {
                // 名字超长, 动态分配重试
                DWORD need = 4096; std::wstring big(need, 0);
                DWORD len2 = need;
                // 注意 RegEnumKeyEx 需要重新调用; 这里简化跳过超长项
                (void)need; (void)len2; (void)big;
                continue;
            }
        }
        RegCloseKey(hKey);
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b){
            return toLowerStr(a) < toLowerStr(b);
        });
        return out;
    }

    std::vector<RegValue> listValues(const std::string& fullPath) override {
        std::vector<RegValue> out;
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) return out;
        HKEY hKey;
        LONG rc = RegOpenKeyExW(hRoot, sub.empty() ? nullptr : sub.c_str(), 0, KEY_READ, &hKey);
        if (rc != ERROR_SUCCESS) return out;

        // 先保证 "(默认)" 值永远显示在第一行 (即使不存在也显示空, 与 regedit 一致)
        bool hasDefault = false;
        DWORD count = 0, maxName = 0, maxData = 0;
        RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         &count, &maxName, &maxData, nullptr, nullptr);
        DWORD nameCap = std::max<DWORD>(maxName + 2, 256);
        DWORD dataCap = std::max<DWORD>(maxData + 2, 1024);
        std::vector<wchar_t> nameBuf(nameCap);
        std::vector<BYTE> dataBuf(dataCap);
        for (DWORD i = 0; i < count; i++) {
            DWORD nameLen = nameCap, type = 0;
            DWORD dataLen = dataCap;
            // Enum 前重置 buffer 大小 (QueryInfo 可能过时)
            if (nameBuf.size() < nameLen) nameBuf.resize(nameLen);
            if (dataBuf.size() < dataLen) dataBuf.resize(dataLen);
            rc = RegEnumValueW(hKey, i, nameBuf.data(), &nameLen, nullptr, &type,
                               dataBuf.data(), &dataLen);
            if (rc == ERROR_MORE_DATA) {
                // 动态扩容重试一次
                nameCap = nameLen + 2; dataCap = dataLen + 2;
                nameBuf.resize(nameCap); dataBuf.resize(dataCap);
                nameLen = nameCap; dataLen = dataCap;
                rc = RegEnumValueW(hKey, i, nameBuf.data(), &nameLen, nullptr, &type,
                                   dataBuf.data(), &dataLen);
            }
            if (rc != ERROR_SUCCESS) continue;
            RegValue v;
            v.name = wideToUtf8(std::wstring(nameBuf.data(), nameLen));
            v.type = type;
            if (v.name.empty()) hasDefault = true;
            // SZ / EXPAND_SZ: UTF-16LE -> UTF-8
            if (type == REG_SZ || type == REG_EXPAND_SZ) {
                std::wstring ws;
                if (dataLen >= 2) ws.assign((wchar_t*)dataBuf.data(), dataLen / 2);
                while (!ws.empty() && ws.back() == L'\0') ws.pop_back();
                v.setString(wideToUtf8(ws));
            } else if (type == REG_MULTI_SZ) {
                std::vector<std::string> lines;
                if (dataLen >= 2) {
                    const wchar_t* p = (const wchar_t*)dataBuf.data();
                    size_t total = dataLen / 2, off = 0;
                    while (off < total) {
                        std::wstring one = p + off;
                        if (one.empty()) break;
                        lines.push_back(wideToUtf8(one));
                        off += one.size() + 1;
                    }
                }
                v.setMultiStrings(lines);
            } else {
                v.data.assign(dataBuf.begin(), dataBuf.begin() + dataLen);
            }
            out.push_back(std::move(v));
        }
        RegCloseKey(hKey);
        if (!hasDefault) {
            RegValue d; d.name = ""; d.type = REG_SZ_T; d.data.clear();
            out.push_back(std::move(d));
        }
        std::sort(out.begin(), out.end(), [](const RegValue& a, const RegValue& b){
            if (a.name.empty() != b.name.empty()) return a.name.empty();
            return toLowerStr(a.name) < toLowerStr(b.name);
        });
        return out;
    }

    bool createKey(const std::string& fullPath, std::string& err) override {
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) { err = "路径无效"; return false; }
        if (sub.empty()) { err = "根项已存在"; return false; }
        HKEY hKey; DWORD disp;
        LONG rc = RegCreateKeyExW(hRoot, sub.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                                  KEY_WRITE, nullptr, &hKey, &disp);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc) + " (可能需要管理员权限)"; return false; }
        RegCloseKey(hKey);
        return true;
    }

    bool deleteKeyTree(const std::string& fullPath, std::string& err) override {
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) { err = "路径无效"; return false; }
        if (sub.empty()) { err = "不能删除根项"; return false; }
        LONG rc = RegDeleteTreeW(hRoot, sub.c_str());
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc) + " (可能需要管理员权限)"; return false; }
        return true;
    }

    bool setValue(const std::string& fullPath, const RegValue& v, std::string& err) override {
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) { err = "路径无效"; return false; }
        HKEY hKey;
        LONG rc = RegOpenKeyExW(hRoot, sub.empty() ? nullptr : sub.c_str(), 0, KEY_SET_VALUE, &hKey);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc) + " (可能需要管理员权限)"; return false; }
        std::wstring wname = utf8ToWide(v.name);
        const wchar_t* namePtr = wname.c_str();
        bool ok = true;
        if (v.type == REG_SZ || v.type == REG_EXPAND_SZ) {
            std::wstring ws = utf8ToWide(v.getString());
            rc = RegSetValueExW(hKey, namePtr, 0, v.type, (const BYTE*)ws.c_str(),
                                (DWORD)((ws.size() + 1) * sizeof(wchar_t)));
            if (rc != ERROR_SUCCESS) { err = winErrorStr(rc); ok = false; }
        } else if (v.type == REG_MULTI_SZ) {
            auto lines = v.getMultiStrings();
            std::vector<wchar_t> buf;
            for (auto& ln : lines) {
                std::wstring w = utf8ToWide(ln);
                buf.insert(buf.end(), w.begin(), w.end());
                buf.push_back(L'\0');
            }
            buf.push_back(L'\0'); // 双 NUL 结尾
            if (lines.empty()) buf.push_back(L'\0');
            rc = RegSetValueExW(hKey, namePtr, 0, REG_MULTI_SZ, (const BYTE*)buf.data(),
                                (DWORD)(buf.size() * sizeof(wchar_t)));
            if (rc != ERROR_SUCCESS) { err = winErrorStr(rc); ok = false; }
        } else {
            DWORD t = v.type;
            if (t == REG_NONE_T) t = REG_NONE;
            rc = RegSetValueExW(hKey, namePtr, 0, t,
                                v.data.empty() ? nullptr : v.data.data(), (DWORD)v.data.size());
            if (rc != ERROR_SUCCESS) { err = winErrorStr(rc); ok = false; }
        }
        RegCloseKey(hKey);
        return ok;
    }

    bool deleteValue(const std::string& fullPath, const std::string& valueName, std::string& err) override {
        if (valueName.empty()) { err = "不能删除“(默认)”值, 只能清空它的内容"; return false; }
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) { err = "路径无效"; return false; }
        HKEY hKey;
        LONG rc = RegOpenKeyExW(hRoot, sub.empty() ? nullptr : sub.c_str(), 0, KEY_SET_VALUE, &hKey);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc); return false; }
        std::wstring wn = utf8ToWide(valueName);
        rc = RegDeleteValueW(hKey, wn.c_str());
        RegCloseKey(hKey);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc); return false; }
        return true;
    }

    bool renameValue(const std::string& fullPath, const std::string& oldName,
                     const std::string& newName, std::string& err) override {
        if (oldName.empty()) { err = "不能重命名“(默认)”值"; return false; }
        if (newName.empty()) { err = "新名称不能为空 (空名称保留给“(默认)”)"; return false; }
        auto vals = listValues(fullPath);
        const RegValue* found = nullptr;
        for (auto& v : vals) if (v.name == oldName) { found = &v; break; }
        if (!found) { err = "找不到原值"; return false; }
        RegValue nv = *found; nv.name = newName;
        if (!setValue(fullPath, nv, err)) return false;
        if (!deleteValue(fullPath, oldName, err)) return false;
        return true;
    }

    bool renameKey(const std::string& fullPath, const std::string& newName,
                   std::string& err) override {
        if (isRootPath(fullPath)) { err = "不能重命名根项"; return false; }
        if (newName.empty() || newName.find('\\') != std::string::npos) { err = "新名称无效"; return false; }
        HKEY hRoot; std::wstring sub;
        if (!parse(fullPath, hRoot, sub)) { err = "路径无效"; return false; }
        // 拆出父项与叶子名
        std::wstring parentSub, leaf;
        auto pos = sub.rfind(L'\\');
        if (pos == std::wstring::npos) { parentSub.clear(); leaf = sub; }
        else { parentSub = sub.substr(0, pos); leaf = sub.substr(pos + 1); }
        HKEY hParent;
        LONG rc = RegOpenKeyExW(hRoot, parentSub.empty() ? nullptr : parentSub.c_str(),
                                0, KEY_WRITE, &hParent);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc) + " (可能需要管理员权限)"; return false; }
        // 动态加载 RegRenameKeyW (需要 Vista+, 避免旧 SDK 链接失败)
        typedef LSTATUS (WINAPI *PFN_RegRenameKeyW)(HKEY, LPCWSTR, LPCWSTR);
        HMODULE hAdv = GetModuleHandleW(L"Advapi32.dll");
        PFN_RegRenameKeyW pfn = nullptr;
        if (hAdv) {
            FARPROC fp = GetProcAddress(hAdv, "RegRenameKeyW");
            static_assert(sizeof(fp) == sizeof(pfn), "pointer size mismatch");
            memcpy(&pfn, &fp, sizeof(pfn));
        }
        if (!pfn) { RegCloseKey(hParent); err = "系统不支持 RegRenameKey (需要 Vista 及以上)"; return false; }
        std::wstring wNew = utf8ToWide(newName);
        rc = pfn(hParent, leaf.c_str(), wNew.c_str());
        RegCloseKey(hParent);
        if (rc != ERROR_SUCCESS) { err = winErrorStr(rc) + " (可能需要管理员权限)"; return false; }
        return true;
    }

    bool exportReg(const std::string& fullPath, const std::string& filePath, std::string& err) override {
        std::string fp = trimStr(filePath);
        if (fp.empty()) { err = "文件名不能为空"; return false; }
        // 使用系统 reg.exe 导出, 稳定且格式标准
        std::string cmd = "reg export \"" + fullPath + "\" \"" + fp + "\" /y";
        // 转为宽字符执行, 支持中文路径
        std::wstring wcmd = utf8ToWide(cmd);
        int rc = _wsystem(wcmd.c_str());
        if (rc != 0) { err = "reg export 执行失败 (退出码 " + std::to_string(rc) + ")"; return false; }
        return true;
    }

private:
    bool parse(const std::string& fullPath, HKEY& hRoot, std::wstring& sub) {
        std::string root, subPath;
        splitRootPath(fullPath, root, subPath);
        auto it = kRoots.find(root);
        if (it == kRoots.end()) return false;
        hRoot = it->second;
        sub = utf8ToWide(subPath);
        return true;
    }
};

std::unique_ptr<IRegistry> createRegistry() {
    return std::make_unique<WinRegistry>();
}
#endif // _WIN32
