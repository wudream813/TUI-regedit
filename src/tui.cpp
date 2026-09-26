// tui.cpp - Console / Screen 实现
#include "tui.hpp"
#include "util.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>

std::string encodeUtf8(char32_t cp) {
    std::string s;
    if (cp < 0x80) s.push_back((char)cp);
    else if (cp < 0x800) {
        s.push_back((char)(0xC0 | (cp >> 6)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back((char)(0xE0 | (cp >> 12)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        s.push_back((char)(0xF0 | (cp >> 18)));
        s.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back((char)(0x80 | (cp & 0x3F)));
    }
    return s;
}

Console& Console::instance() {
    static Console c;
    return c;
}

static std::string ansiFg(Color c) {
    int v = (int)c;
    if (v < 0) return "\x1b[39m";
    if (v < 8) return "\x1b[" + std::to_string(30 + v) + "m";
    return "\x1b[" + std::to_string(90 + v - 8) + "m";
}
static std::string ansiBg(Color c) {
    int v = (int)c;
    if (v < 0) return "\x1b[49m";
    if (v < 8) return "\x1b[" + std::to_string(40 + v) + "m";
    return "\x1b[" + std::to_string(100 + v - 8) + "m";
}

void Console::moveCursor(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
    write(buf);
}
void Console::write(const std::string& s) { std::fwrite(s.c_str(), 1, s.size(), stdout); }
void Console::setFg(Color c) { write(ansiFg(c)); }
void Console::setBg(Color c) { write(ansiBg(c)); }
void Console::setBold(bool b) { write(b ? "\x1b[1m" : "\x1b[22m"); }
void Console::setReverse(bool b) { write(b ? "\x1b[7m" : "\x1b[27m"); }
void Console::resetAttr() { write("\x1b[0m"); }
void Console::hideCursor() { write("\x1b[?25l"); }
void Console::showCursor() { write("\x1b[?25h"); }
void Console::setTitle(const std::string& t) { write("\x1b]0;" + t + "\x07"); }
void Console::flushOut() { std::fflush(stdout); }
void Console::clear() { write("\x1b[2J\x1b[H"); }

// ================= Screen: 双缓冲脏矩形渲染 =================
void Screen::begin(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w != w_ || h != h_) {
        w_ = w; h_ = h;
        cur_.assign((size_t)w * (size_t)h, Cell());
        needClear_ = true;
    }
    next_ = cur_;  // 本帧从旧屏复制起步, 调用方覆盖所有可见格
}

void Screen::setCell(int x, int y, const std::string& s, const Attr& a, int wd, bool cont) {
    Cell& c = next_[(size_t)y * (size_t)w_ + (size_t)x];
    c.s = s; c.fg = a.fg; c.bg = a.bg; c.bold = a.bold; c.cont = cont; c.wd = wd;
}

void Screen::putStr(int x, int y, const std::string& utf8, Attr a) {
    if (y < 0 || y >= h_) return;
    int cx = x;
    for (size_t i = 0; i < utf8.size();) {
        size_t len = 1;
        uint32_t cp = utf8DecodeOne(utf8.c_str() + i, utf8.size() - i, len);
        std::string g = utf8.substr(i, len);
        int wd = 1;
        if (cp < 0x20 || cp == 0x7F) { g = "?"; wd = 1; }  // 控制字符消毒
        else if (isWideCodepoint(cp)) wd = 2;
        if (cx >= w_) break;
        if (cx >= 0) {
            if (cx + wd > w_) {
                if (wd == 2) setCell(cx, y, " ", a, 1, false);  // 行尾放不下宽字符则填空格
                break;
            }
            setCell(cx, y, g, a, wd, false);
            if (wd == 2 && cx + 1 < w_) setCell(cx + 1, y, "", a, 0, true);
        }
        cx += wd;
        i += len;
    }
}

void Screen::fillRect(int x, int y, int w, int h, const std::string& ch, Attr a) {
    for (int r = 0; r < h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= h_) continue;
        for (int c = 0; c < w; c++) {
            int xx = x + c;
            if (xx < 0 || xx >= w_) continue;
            setCell(xx, yy, ch, a, 1, false);
        }
    }
}

static void appendSgr(std::string& out, const Attr& a) {
    out += "\x1b[0";
    if (a.bold) out += ";1";
    int f = (int)a.fg, b = (int)a.bg;
    if (f >= 0) { out += ";"; out += (f < 8 ? std::to_string(30 + f) : std::to_string(90 + f - 8)); }
    if (b >= 0) { out += ";"; out += (b < 8 ? std::to_string(40 + b) : std::to_string(100 + b - 8)); }
    out += "m";
}

void Screen::present() {
    if (w_ <= 0 || h_ <= 0) return;
    std::string out;
    if (needClear_) { out += "\x1b[2J\x1b[H"; needClear_ = false; }
    int cx = -1, cy = -1;  // 跟踪终端真实光标, 避免多余定位
    Attr la;
    bool haveAttr = false;
    char mv[32];
    for (int y = 0; y < h_; y++) {
        int x = 0;
        while (x < w_) {
            Cell& n = next_[(size_t)y * (size_t)w_ + (size_t)x];
            Cell& c = cur_[(size_t)y * (size_t)w_ + (size_t)x];
            if (n == c) { x++; continue; }
            if (cx != x || cy != y) {
                snprintf(mv, sizeof(mv), "\x1b[%d;%dH", y + 1, x + 1);
                out += mv;
                cx = x; cy = y;
            }
            Attr ra; ra.fg = n.fg; ra.bg = n.bg; ra.bold = n.bold;
            while (x < w_) {
                Cell& n2 = next_[(size_t)y * (size_t)w_ + (size_t)x];
                Cell& c2 = cur_[(size_t)y * (size_t)w_ + (size_t)x];
                if (n2 == c2) break;
                Attr a2; a2.fg = n2.fg; a2.bg = n2.bg; a2.bold = n2.bold;
                if (a2 != ra) break;  // 属性变化则断开 run
                if (!haveAttr || la != ra) { appendSgr(out, ra); la = ra; haveAttr = true; }
                if (!n2.cont) { out += n2.s; cx += n2.wd; }  // 宽字符后半格不独立输出
                c2 = n2;
                x++;
            }
        }
    }
    out += "\x1b[0m";
    std::fwrite(out.c_str(), 1, out.size(), stdout);
    std::fflush(stdout);
}

#ifdef _WIN32
// ================= Windows 实现 =================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void Console::init() {
    if (inited_) return;
    hIn_ = GetStdHandle(STD_INPUT_HANDLE);
    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
    oldCp_ = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    if (hIn_ && hIn_ != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hIn_, &oldInMode_);
        // 原始输入事件模式: 方向键/功能键/鼠标事件直达, 不经过行缓冲
        SetConsoleMode(hIn_, ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    }
    if (hOut_ && hOut_ != INVALID_HANDLE_VALUE) {
        GetConsoleMode(hOut_, &oldOutMode_);
        SetConsoleMode(hOut_, oldOutMode_ | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    write("\x1b[?1049h");  // 备用屏幕
    hideCursor();
    clear();
    flushOut();
    inited_ = true;
    setTitle("TUI Regedit");
}

void Console::shutdown() {
    if (!inited_) return;
    resetAttr();
    showCursor();
    write("\x1b[?1049l");
    flushOut();
    if (hIn_ && hIn_ != INVALID_HANDLE_VALUE) SetConsoleMode(hIn_, oldInMode_);
    if (hOut_ && hOut_ != INVALID_HANDLE_VALUE) SetConsoleMode(hOut_, oldOutMode_);
    SetConsoleOutputCP(oldCp_);
    inited_ = false;
}

Size Console::getSize() {
    Size s;
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(hOut_, &info)) {
        s.w = info.srWindow.Right - info.srWindow.Left + 1;
        s.h = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
    if (s.w < 20) s.w = 80;
    if (s.h < 10) s.h = 24;
    return s;
}

Key Console::readKey() {
    HANDLE hIn = (HANDLE)hIn_;
    for (;;) {
        INPUT_RECORD rec;
        DWORD n = 0;
        if (!ReadConsoleInputW(hIn, &rec, 1, &n) || n == 0) continue;
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            Key k;  // Unknown, 让上层重绘
            return k;
        }
        if (rec.EventType == MOUSE_EVENT) {
            auto& me = rec.Event.MouseEvent;
            Key k;
            k.mouse = true;
            k.mx = (int)me.dwMousePosition.X;
            k.my = (int)me.dwMousePosition.Y;
            // 鼠标坐标是相对缓冲区的, 减去窗口原点得到窗口相对坐标
            CONSOLE_SCREEN_BUFFER_INFO info;
            if (GetConsoleScreenBufferInfo((HANDLE)hOut_, &info)) {
                k.mx -= info.srWindow.Left;
                k.my -= info.srWindow.Top;
            }
            DWORD btn = me.dwButtonState;
            if (me.dwEventFlags & MOUSE_WHEELED) {
                k.mwheel = ((short)HIWORD(btn) > 0) ? 1 : -1;
                return k;
            }
            if (me.dwEventFlags & MOUSE_HWHEELED) { Key u; return u; }  // 横向滚轮忽略
            if (me.dwEventFlags & MOUSE_MOVED) {
                if (btn == 0) { Key u; return u; }  // 无按键悬停: 忽略
                k.mdrag = true;
                k.mbutton = (btn & 0x1) ? 1 : ((btn & 0x2) ? 3 : ((btn & 0x4) ? 2 : 1));
                lastButtons_ = btn;
                return k;
            }
            // 按下/释放: 与上次按钮状态比较 (DOUBLE_CLICK 事件也走这里, 由应用层统一做双击检测)
            DWORD changed = lastButtons_ ^ btn;
            lastButtons_ = btn;
            if (changed & 0x1) {
                k.mbutton = 1; k.mpress = (btn & 0x1) != 0; k.mrelease = (btn & 0x1) == 0;
            } else if (changed & 0x2) {
                k.mbutton = 3; k.mpress = (btn & 0x2) != 0; k.mrelease = (btn & 0x2) == 0;
            } else if (changed & 0x4) {
                k.mbutton = 2; k.mpress = (btn & 0x4) != 0; k.mrelease = (btn & 0x4) == 0;
            } else { Key u; return u; }
            return k;
        }
        if (rec.EventType != KEY_EVENT) continue;
        auto& ke = rec.Event.KeyEvent;
        if (!ke.bKeyDown) continue;
        DWORD ctrl = ke.dwControlKeyState;
        bool isCtrl = (ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        bool isAlt = (ctrl & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
        WORD vk = ke.wVirtualKeyCode;
        wchar_t wc = ke.uChar.UnicodeChar;
        Key k;

        switch (vk) {
            case VK_UP: k.type = Key::Up; return k;
            case VK_DOWN: k.type = Key::Down; return k;
            case VK_LEFT: k.type = Key::Left; return k;
            case VK_RIGHT: k.type = Key::Right; return k;
            case VK_HOME: k.type = Key::Home; return k;
            case VK_END: k.type = Key::End; return k;
            case VK_PRIOR: k.type = Key::PgUp; return k;
            case VK_NEXT: k.type = Key::PgDn; return k;
            case VK_DELETE: k.type = Key::Delete; return k;
            case VK_F1: k.type = Key::F1; return k;
            case VK_F2: k.type = Key::F2; return k;
            case VK_F3: k.type = Key::F3; return k;
            case VK_F4: k.type = Key::F4; return k;
            case VK_F5: k.type = Key::F5; return k;
            case VK_F6: k.type = Key::F6; return k;
            case VK_F7: k.type = Key::F7; return k;
            case VK_F8: k.type = Key::F8; return k;
            case VK_F9: k.type = Key::F9; return k;
            case VK_F10: k.type = Key::F10; return k;
            case VK_F11: k.type = Key::F11; return k;
            case VK_F12: k.type = Key::F12; return k;
            case VK_RETURN: k.type = Key::Enter; return k;
            case VK_ESCAPE: k.type = Key::Esc; return k;
            case VK_BACK: k.type = Key::Backspace; return k;
            case VK_TAB:
                k.type = (ctrl & SHIFT_PRESSED) ? Key::ShiftTab : Key::Tab;
                return k;
            default: break;
        }
        if (vk == VK_INSERT) { Key u; return u; }  // 预留
        if (wc == 0) continue;  // 纯修饰键
        k.type = Key::Char;
        k.ch = (char32_t)wc;
        k.ctrl = isCtrl;
        k.alt = isAlt;
        if (wc >= 0xD800 && wc <= 0xDBFF) {  // 高代理项: 合并低代理
            INPUT_RECORD rec2; DWORD n2 = 0;
            if (ReadConsoleInputW(hIn, &rec2, 1, &n2) && n2 &&
                rec2.EventType == KEY_EVENT && rec2.Event.KeyEvent.bKeyDown) {
                wchar_t lo = rec2.Event.KeyEvent.uChar.UnicodeChar;
                if (lo >= 0xDC00 && lo <= 0xDFFF)
                    k.ch = 0x10000 + (((char32_t)wc - 0xD800) << 10) + (lo - 0xDC00);
            }
        }
        return k;
    }
}

#else
// ================= POSIX 实现 =================
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct TermState { struct termios oldt; };

void Console::init() {
    if (inited_) return;
    ttyFd_ = STDIN_FILENO;
    termState_ = new TermState();
    tcgetattr(ttyFd_, &termState_->oldt);
    struct termios raw = termState_->oldt;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(ttyFd_, TCSAFLUSH, &raw);
    write("\x1b[?1049h");
    // 鼠标: 1000 点击 + 1002 按住拖动 + 1006 SGR 扩展坐标
    write("\x1b[?1000h\x1b[?1002h\x1b[?1006h");
    hideCursor();
    clear();
    flushOut();
    inited_ = true;
    setTitle("TUI Regedit");
}

void Console::shutdown() {
    if (!inited_) return;
    resetAttr();
    showCursor();
    write("\x1b[?1006l\x1b[?1002l\x1b[?1000l");  // 关闭鼠标上报
    write("\x1b[?1049l");
    flushOut();
    if (termState_) {
        tcsetattr(ttyFd_, TCSAFLUSH, &termState_->oldt);
        delete termState_;
        termState_ = nullptr;
    }
    inited_ = false;
}

Size Console::getSize() {
    Size s;
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        s.w = ws.ws_col; s.h = ws.ws_row;
    } else {
        s.w = 100; s.h = 30;
    }
    if (s.w < 20) s.w = 80;
    if (s.h < 10) s.h = 24;
    return s;
}

static bool readByte(int fd, unsigned char& out, int timeoutMs) {
    fd_set rf; FD_ZERO(&rf); FD_SET(fd, &rf);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = select(fd + 1, &rf, nullptr, nullptr, timeoutMs < 0 ? nullptr : &tv);
    if (r <= 0) return false;
    unsigned char b;
    ssize_t n = ::read(fd, &b, 1);
    if (n != 1) return false;
    out = b;
    return true;
}

Key Console::readKey() {
    Key k;
    unsigned char b = 0;
    if (!readByte(ttyFd_, b, -1)) { k.type = Key::Unknown; return k; }

    if (b != 0x1B) {
        if (b == '\r' || b == '\n') { k.type = Key::Enter; return k; }
        if (b == '\t') { k.type = Key::Tab; return k; }
        if (b == 127 || b == 8) { k.type = Key::Backspace; return k; }
        if (b < 0x20) { k.type = Key::Char; k.ch = b; k.ctrl = false; return k; }
        if (b < 0x80) { k.type = Key::Char; k.ch = b; return k; }
        char32_t cp = 0; int need = 0;
        if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; need = 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; need = 2; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; need = 3; }
        else { k.type = Key::Char; k.ch = b; return k; }
        for (int i = 0; i < need; i++) {
            unsigned char nb = 0;
            if (!readByte(ttyFd_, nb, 1000)) break;
            cp = (cp << 6) | (nb & 0x3F);
        }
        k.type = Key::Char; k.ch = cp;
        return k;
    }
    unsigned char b2 = 0;
    if (!readByte(ttyFd_, b2, 60)) { k.type = Key::Esc; return k; }
    if (b2 == 0x1B) { k.type = Key::Esc; return k; }
    if (b2 == '[' || b2 == 'O') {
        std::string seq;
        unsigned char c = 0;
        for (int i = 0; i < 24; i++) {
            if (!readByte(ttyFd_, c, 200)) break;
            seq.push_back((char)c);
            // 大写字母 / 小写 m(SGR 鼠标释放) / ~ 结束
            if ((c >= 'A' && c <= 'Z') || c == 'm' || c == '~') break;
        }
        // SGR 鼠标: <Cb;Cx;CyM(按下) / m(释放)
        if (b2 == '[' && !seq.empty() && seq[0] == '<') {
            int cb = 0, cx = 1, cy = 1;
            char term = seq.back();
            sscanf(seq.c_str() + 1, "%d;%d;%d", &cb, &cx, &cy);
            k.mouse = true;
            k.mx = cx - 1; k.my = cy - 1;
            if (k.mx < 0) k.mx = 0;
            if (k.my < 0) k.my = 0;
            if (cb & 64) {
                k.mwheel = ((cb & 1) == 0) ? 1 : -1;  // 64 上滚, 65 下滚
            } else if (term == 'm') {
                k.mrelease = true;
                k.mbutton = lastMouseBtn_;
            } else {
                int btn = (cb & 3) + 1;
                if (btn > 3) btn = 3;
                if (cb & 32) { k.mdrag = true; k.mbutton = btn; }
                else { k.mpress = true; k.mbutton = btn; lastMouseBtn_ = btn; }
            }
            return k;
        }
        if (b2 == '[') {
            if (seq == "A") k.type = Key::Up;
            else if (seq == "B") k.type = Key::Down;
            else if (seq == "C") k.type = Key::Right;
            else if (seq == "D") k.type = Key::Left;
            else if (seq == "H" || seq == "1~") k.type = Key::Home;
            else if (seq == "F" || seq == "4~") k.type = Key::End;
            else if (seq == "5~") k.type = Key::PgUp;
            else if (seq == "6~") k.type = Key::PgDn;
            else if (seq == "3~") k.type = Key::Delete;
            else if (seq == "2~") k.type = Key::Unknown;
            else if (seq == "Z") k.type = Key::ShiftTab;
            else if (seq == "11~") k.type = Key::F1;
            else if (seq == "12~") k.type = Key::F2;
            else if (seq == "13~") k.type = Key::F3;
            else if (seq == "14~") k.type = Key::F4;
            else if (seq == "15~") k.type = Key::F5;
            else if (seq == "17~") k.type = Key::F6;
            else if (seq == "18~") k.type = Key::F7;
            else if (seq == "19~") k.type = Key::F8;
            else if (seq == "20~") k.type = Key::F9;
            else if (seq == "21~") k.type = Key::F10;
            else if (seq == "23~") k.type = Key::F11;
            else if (seq == "24~") k.type = Key::F12;
            else k.type = Key::Unknown;
        } else {
            if (seq == "P") k.type = Key::F1;
            else if (seq == "Q") k.type = Key::F2;
            else if (seq == "R") k.type = Key::F3;
            else if (seq == "S") k.type = Key::F4;
            else if (seq == "H") k.type = Key::Home;
            else if (seq == "F") k.type = Key::End;
            else k.type = Key::Unknown;
        }
        return k;
    }
    k.type = Key::Char; k.ch = b2; k.alt = true;
    return k;
}
#endif
