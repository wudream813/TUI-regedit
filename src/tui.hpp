// tui.hpp - 极简跨平台 TUI 底层
// 渲染: 双缓冲 + 脏矩形 diff, 每帧只输出变化部分
// 输入: 原始键盘 + 鼠标 (单击/双击/右键/滚轮/拖动)
#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct Size { int w = 80, h = 24; };

struct Key {
    enum Type {
        Char, Up, Down, Left, Right, Home, End, PgUp, PgDn,
        Enter, Esc, Tab, ShiftTab, Backspace, Delete,
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
        Unknown
    } type = Unknown;
    char32_t ch = 0;   // type==Char 时的 Unicode 码点
    bool ctrl = false;
    bool alt = false;
    // ---- 鼠标 (mouse==true 时有效) ----
    bool mouse = false;
    int mx = 0, my = 0;      // 0-based 列/行 (相对当前窗口)
    int mbutton = 0;         // 1=左 2=中 3=右
    bool mpress = false;     // 按下
    bool mrelease = false;   // 释放
    bool mdrag = false;      // 按住拖动
    int mwheel = 0;          // +1 上滚, -1 下滚

    bool isChar(char c) const { return !mouse && type == Char && ch == (char32_t)c; }
    // Ctrl+字母判断 (Windows 下 ReadConsoleInput 直接给 ctrl 标志; Linux 下是 1-26 控制码)
    bool isCtrl(char c) const {
        if (mouse || type != Char) return false;
        char lower = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        char upper = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        // 控制码 1-26 恒为 Ctrl+字母 (Linux 原始字节; Windows 下 ctrl 标志同时置位)
        if (ch >= 1 && ch <= 26) return (char)('a' + ch - 1) == lower;
        return ctrl && (ch == (char32_t)lower || ch == (char32_t)upper);
    }
};

enum class Color : int {
    Default = -1,
    Black = 0, Red = 1, Green = 2, Yellow = 3,
    Blue = 4, Magenta = 5, Cyan = 6, White = 7,
    BrightBlack = 8, BrightRed = 9, BrightGreen = 10, BrightYellow = 11,
    BrightBlue = 12, BrightMagenta = 13, BrightCyan = 14, BrightWhite = 15,
};

struct Attr {
    Color fg = Color::Default;
    Color bg = Color::Default;
    bool bold = false;
    bool operator==(const Attr& o) const { return fg == o.fg && bg == o.bg && bold == o.bold; }
    bool operator!=(const Attr& o) const { return !(*this == o); }
};

// 双缓冲屏幕: 每帧 begin(w,h) -> putStr/fillRect -> present()
// present() 对比前后两帧, 只把变化的格子拼成一个字符串一次性输出。
// 约定: 调用方每帧重绘所有可见格 (否则残留旧内容)。
class Screen {
public:
    void begin(int w, int h);
    int width() const { return w_; }
    int height() const { return h_; }
    void putStr(int x, int y, const std::string& utf8, Attr a = Attr{});
    void putStr(int x, int y, const std::string& utf8, Color fg,
                Color bg = Color::Default, bool bold = false) {
        Attr a; a.fg = fg; a.bg = bg; a.bold = bold;
        putStr(x, y, utf8, a);
    }
    void fillRect(int x, int y, int w, int h, const std::string& ch, Attr a = Attr{});
    void present();

private:
    struct Cell {
        std::string s = " ";
        Color fg = Color::Default;
        Color bg = Color::Default;
        bool bold = false;
        bool cont = false;  // 宽字符后半格 (不独立输出)
        int wd = 1;         // 1 窄 / 2 宽首格 / 0 后半格
        bool operator==(const Cell& o) const {
            return s == o.s && fg == o.fg && bg == o.bg && bold == o.bold && cont == o.cont;
        }
        bool operator!=(const Cell& o) const { return !(*this == o); }
    };
    int w_ = 0, h_ = 0;
    bool needClear_ = false;
    std::vector<Cell> cur_, next_;
    void setCell(int x, int y, const std::string& s, const Attr& a, int wd, bool cont);
};

class Console {
public:
    static Console& instance();
    void init();
    void shutdown();
    bool initialized() const { return inited_; }

    Size getSize();
    Screen& screen() { return screen_; }
    void present() { screen_.present(); }

    // 即时输出 (仅用于 init/shutdown/光标等零星控制, 常规绘制请走 Screen)
    void clear();
    void moveCursor(int x, int y);   // 0-based
    void write(const std::string& s);
    void setFg(Color c);
    void setBg(Color c);
    void setBold(bool b);
    void setReverse(bool b);
    void resetAttr();
    void hideCursor();
    void showCursor();
    void setTitle(const std::string& t);
    void flushOut();

    Key readKey();  // 阻塞读取一个按键/鼠标事件

private:
    Console() = default;
    bool inited_ = false;
    Screen screen_;
#ifdef _WIN32
    void* hIn_ = nullptr;
    void* hOut_ = nullptr;
    unsigned long oldInMode_ = 0, oldOutMode_ = 0, oldCp_ = 0;
    unsigned long lastButtons_ = 0;  // 上次鼠标按钮状态 (区分按下/释放)
#else
    int ttyFd_ = -1;
    struct TermState* termState_ = nullptr;
    int lastMouseBtn_ = 1;  // 上次按下的按钮 (SGR 释放事件不带按钮号)
#endif
};

// 编码小工具: char32 -> UTF-8
std::string encodeUtf8(char32_t cp);
