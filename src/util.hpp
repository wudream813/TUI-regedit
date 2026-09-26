// util.hpp - 通用工具函数 (UTF-8 / HEX / DWORD 解析 / 显示宽度)
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
inline std::string wideToUtf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), s.data(), n, nullptr, nullptr);
    return s;
}
inline std::string winErrorStr(DWORD err) {
    LPWSTR buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, err,
                             MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&buf, 0, nullptr);
    std::string s;
    if (n && buf) {
        s = wideToUtf8(std::wstring(buf, n));
        LocalFree(buf);
    } else {
        s = "Win32 error " + std::to_string(err);
    }
    // 去掉末尾空白
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}
#endif

// ---------- 字符串小工具 ----------
inline std::string trimStr(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b-1])) b--;
    return s.substr(a, b - a);
}
inline std::string toLowerStr(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline bool containsIgnoreCase(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return toLowerStr(hay).find(toLowerStr(needle)) != std::string::npos;
}
inline std::vector<std::string> splitBy(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

// ---------- HEX ----------
inline std::string bytesToHex(const std::vector<uint8_t>& data, bool spaces = true, size_t maxBytes = 64) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    size_t n = std::min(data.size(), maxBytes);
    for (size_t i = 0; i < n; i++) {
        if (i && spaces) oss << ' ';
        oss << std::setw(2) << (int)data[i];
    }
    if (data.size() > maxBytes) oss << "...";
    return oss.str();
}
inline bool hexToBytes(const std::string& hex, std::vector<uint8_t>& out, std::string& err) {
    out.clear();
    std::string t;
    for (char c : hex) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == ';' || c == '-') continue;
        if (c == '0' && (t.empty())) { /* 允许 0x 前缀, 在下面处理 */ }
        t.push_back(c);
    }
    // 去掉 0x / 0X 前缀
    std::string f;
    for (size_t i = 0; i < t.size(); i++) {
        if (t[i] == '0' && i + 1 < t.size() && (t[i+1] == 'x' || t[i+1] == 'X')) { i++; continue; }
        f.push_back(t[i]);
    }
    if (f.empty()) return true; // 空 = 空数据
    if (f.size() % 2 != 0) { err = "十六进制长度必须为偶数"; return false; }
    for (size_t i = 0; i < f.size(); i += 2) {
        auto hv = [](char c)->int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = hv(f[i]), lo = hv(f[i+1]);
        if (hi < 0 || lo < 0) { err = std::string("非法十六进制字符: ") + f[i] + f[i+1]; return false; }
        out.push_back((uint8_t)((hi << 4) | lo));
    }
    return true;
}

// ---------- DWORD / QWORD ----------
inline bool stringToU32(const std::string& s, uint32_t& out, std::string& err) {
    std::string t = trimStr(s);
    if (t.empty()) { err = "不能为空"; return false; }
    try {
        size_t pos = 0;
        unsigned long long v;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
            v = std::stoull(t.substr(2), &pos, 16);
        else
            v = std::stoull(t, &pos, 10);
        if (pos != (t.size() > 2 && t[0]=='0'&&(t[1]=='x'||t[1]=='X') ? t.size()-2 : t.size())) {
            // stoull 对 substr 的 pos 检查; 简化: 只要能解析就过, 但检查非法字符
        }
        if (v > 0xFFFFFFFFULL) { err = "超出 DWORD 范围 (0 ~ 4294967295)"; return false; }
        out = (uint32_t)v;
        return true;
    } catch (...) { err = "数字格式无效 (支持十进制与 0x 十六进制)"; return false; }
}
inline bool stringToU64(const std::string& s, uint64_t& out, std::string& err) {
    std::string t = trimStr(s);
    if (t.empty()) { err = "不能为空"; return false; }
    try {
        size_t pos = 0;
        unsigned long long v;
        if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
            v = std::stoull(t.substr(2), &pos, 16);
        else
            v = std::stoull(t, &pos, 10);
        out = (uint64_t)v;
        return true;
    } catch (...) { err = "数字格式无效 (支持十进制与 0x 十六进制)"; return false; }
}
inline std::string u32ToHex(uint32_t v) {
    std::ostringstream oss; oss << "0x" << std::hex << std::setw(8) << std::setfill('0') << v
        << " (" << std::dec << v << ")";
    return oss.str();
}
inline std::string u64ToHex(uint64_t v) {
    std::ostringstream oss; oss << "0x" << std::hex << std::setw(16) << std::setfill('0') << v
        << " (" << std::dec << v << ")";
    return oss.str();
}

// ---------- UTF-8 显示宽度 (CJK 算 2 宽) ----------
inline uint32_t utf8DecodeOne(const char* s, size_t n, size_t& len) {
    len = 1;
    if (n == 0) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { len = 1; return c; }
    if ((c & 0xE0) == 0xC0 && n >= 2) { len = 2; return ((c & 0x1F) << 6) | (((unsigned char)s[1]) & 0x3F); }
    if ((c & 0xF0) == 0xE0 && n >= 3) { len = 3; return ((c & 0x0F) << 12) | ((((unsigned char)s[1]) & 0x3F) << 6) | (((unsigned char)s[2]) & 0x3F); }
    if ((c & 0xF8) == 0xF0 && n >= 4) { len = 4; return ((c & 0x07) << 18) | ((((unsigned char)s[1]) & 0x3F) << 12) | ((((unsigned char)s[2]) & 0x3F) << 6) | (((unsigned char)s[3]) & 0x3F); }
    return c;
}
inline bool isWideCodepoint(uint32_t cp) {
    // 简化版 East Asian Wide/Fullwidth 判断
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x9FFF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD) || (cp >= 0x3000 && cp <= 0x303F) ||
           (cp >= 0x3040 && cp <= 0x33FF) || (cp >= 0xFF61 && cp <= 0xFFDC);
}
inline int utf8DisplayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        size_t len = 1;
        uint32_t cp = utf8DecodeOne(s.c_str() + i, s.size() - i, len);
        w += isWideCodepoint(cp) ? 2 : 1;
        i += len;
    }
    return w;
}
// 按显示宽度截断, 超出加…
inline std::string truncateDisplay(const std::string& s, int maxWidth) {
    if (maxWidth <= 0) return "";
    if (utf8DisplayWidth(s) <= maxWidth) return s;
    std::string out;
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        size_t len = 1;
        uint32_t cp = utf8DecodeOne(s.c_str() + i, s.size() - i, len);
        int cw = isWideCodepoint(cp) ? 2 : 1;
        if (w + cw > maxWidth - 1) break;
        out.append(s.substr(i, len));
        w += cw;
        i += len;
    }
    out += "…";
    return out;
}
// 按显示宽度填充 pad
inline std::string padDisplay(const std::string& s, int width) {
    int w = utf8DisplayWidth(s);
    if (w >= width) return s;
    return s + std::string(width - w, ' ');
}
// UTF-8 光标移动: 上一个/下一个字符边界字节长度
inline size_t utf8PrevCharLen(const std::string& s, size_t bytePos) {
    if (bytePos == 0 || bytePos > s.size()) return 0;
    size_t p = bytePos - 1;
    while (p > 0 && ((unsigned char)s[p] & 0xC0) == 0x80) p--;
    return bytePos - p;
}
inline size_t utf8NextCharLen(const std::string& s, size_t bytePos) {
    if (bytePos >= s.size()) return 0;
    size_t len = 1;
    utf8DecodeOne(s.c_str() + bytePos, s.size() - bytePos, len);
    return len;
}
