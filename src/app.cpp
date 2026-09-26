// app.cpp - TUI Regedit 主应用实现
#include "app.hpp"
#include "util.hpp"
#include "version.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

static Attr A(Color fg, Color bg = Color::Default, bool bold = false) {
    Attr a; a.fg = fg; a.bg = bg; a.bold = bold; return a;
}
static std::string rep(const std::string& s, int n) {
    std::string o;
    for (int i = 0; i < n; i++) o += s;
    return o;
}
static bool inRect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static int clampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
// 滚动条几何: 计算滑块起始行 thY 与高度 thH (相对列表区); 无需滚动返回 false
static bool scrollbarGeom(int total, int top, int rows, int& thY, int& thH) {
    if (total <= rows || rows <= 1) return false;
    thH = rows * rows / total;
    if (thH < 1) thH = 1;
    int maxTop = total - rows;
    thY = maxTop <= 0 ? 0 : (rows - thH) * top / maxTop;
    if (thY < 0) thY = 0;
    if (thY + thH > rows) thY = rows - thH;
    return true;
}
// 路径归一化: 修剪 / 转斜杠 / 补全根项缩写, 非法返回 ""
static std::string normalizeRegPath(const std::string& in) {
    std::string s = trimStr(in);
    if (s.empty()) return "";
    for (char& c : s) if (c == '/') c = '\\';
    std::string t;
    for (char c : s) {
        if (c == '\\' && !t.empty() && t.back() == '\\') continue;
        t += c;
    }
    s = t;
    while (!s.empty() && s.back() == '\\') s.pop_back();
    if (s.empty()) return "";
    std::string root, sub;
    splitRootPath(s, root, sub);
    std::string up;
    for (char c : root) up += (char)std::toupper((unsigned char)c);
    std::string canon;
    if (up == "HKCR" || up == "HKEY_CLASSES_ROOT") canon = "HKEY_CLASSES_ROOT";
    else if (up == "HKCU" || up == "HKEY_CURRENT_USER") canon = "HKEY_CURRENT_USER";
    else if (up == "HKLM" || up == "HKEY_LOCAL_MACHINE") canon = "HKEY_LOCAL_MACHINE";
    else if (up == "HKU" || up == "HKEY_USERS") canon = "HKEY_USERS";
    else if (up == "HKCC" || up == "HKEY_CURRENT_CONFIG") canon = "HKEY_CURRENT_CONFIG";
    else return "";
    return sub.empty() ? canon : canon + "\\" + sub;
}
static std::string asciiLowerStr(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
// 命令面板过滤: 子序列匹配 (ASCII 大小写不敏感)
static bool fuzzyMatchCmd(const std::string& q, const std::string& t) {
    if (q.empty()) return true;
    std::string qs = asciiLowerStr(q), ts = asciiLowerStr(t);
    size_t j = 0;
    for (size_t i = 0; i < qs.size(); i++) {
        j = ts.find(qs[i], j);
        if (j == std::string::npos) return false;
        j++;
    }
    return true;
}
// 由输入框内的列偏移反推字节光标
static size_t cursorFromColumn(const std::string& buf, size_t left, int col) {
    if (col <= 0) return left;
    size_t pos = left;
    int w = 0;
    while (pos < buf.size()) {
        size_t len = utf8NextCharLen(buf, pos);
        size_t dl = 1;
        uint32_t cp = utf8DecodeOne(buf.c_str() + pos, buf.size() - pos, dl);
        int cw = isWideCodepoint(cp) ? 2 : 1;
        if (w + cw > col) break;
        w += cw; pos += len;
    }
    return pos;
}
static std::string centerDisplay(const std::string& s, int width) {
    int w = utf8DisplayWidth(s);
    if (w >= width) return s;
    int left = (width - w) / 2;
    return std::string((size_t)left, ' ') + s + std::string((size_t)(width - w - left), ' ');
}
static std::string buildInfo() {
    std::string c;
#if defined(_MSC_VER)
    c = "MSVC ";
    c += std::to_string(_MSC_VER);
#elif defined(__MINGW64__) || defined(__MINGW32__)
    c = "MinGW GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__) + "." + std::to_string(__GNUC_PATCHLEVEL__);
#elif defined(__clang__)
    c = "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    c = "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    c = "unknown compiler";
#endif
#ifdef _WIN32
    c += " / Windows x64";
#else
    c += " / POSIX";
#endif
    c += " / ";
    c += __DATE__;
    return c;
}

// ---------------- 主题 ----------------
// 字段顺序: id, name, header, headerPath, status,
//           borderOn, borderOff, titleOn, titleOff,
//           selOn, selOff, root, thumbOn, thumbOff, track,
//           dlgText, dlgHint, dlgSel
const std::vector<Theme>& TuiRegedit::allThemes() {
    static const std::vector<Theme> v = {
        {"dark", "深色 (默认)",
         A(Color::BrightWhite, Color::Blue, true), A(Color::Yellow, Color::Blue, true), A(Color::Black, Color::White),
         A(Color::BrightCyan), A(Color::BrightBlack), A(Color::BrightCyan, Color::Default, true), A(Color::White),
         A(Color::BrightWhite, Color::Blue, true), A(Color::BrightWhite, Color::BrightBlack), A(Color::Yellow, Color::Default, true),
         A(Color::BrightCyan), A(Color::White), A(Color::BrightBlack),
         A(Color::White, Color::Black), A(Color::Yellow, Color::Black), A(Color::BrightWhite, Color::Blue, true)},
        {"light", "浅色",
         A(Color::Black, Color::White, true), A(Color::Blue, Color::White, true), A(Color::Black, Color::Cyan),
         A(Color::Blue), A(Color::BrightBlack), A(Color::Blue, Color::Default, true), A(Color::Black),
         A(Color::White, Color::Blue, true), A(Color::White, Color::BrightBlack), A(Color::Blue, Color::Default, true),
         A(Color::Blue), A(Color::Black), A(Color::BrightBlack),
         A(Color::Black, Color::White), A(Color::Blue, Color::White), A(Color::White, Color::Blue, true)},
        {"green", "复古绿",
         A(Color::Black, Color::Green, true), A(Color::Yellow, Color::Green, true), A(Color::Black, Color::Green),
         A(Color::Green), A(Color::BrightBlack), A(Color::Green, Color::Default, true), A(Color::White),
         A(Color::Black, Color::Green, true), A(Color::BrightWhite, Color::BrightBlack), A(Color::Green, Color::Default, true),
         A(Color::Green), A(Color::White), A(Color::BrightBlack),
         A(Color::Green, Color::Black), A(Color::Yellow, Color::Black), A(Color::Black, Color::Green, true)},
        {"ocean", "海洋",
         A(Color::BrightCyan, Color::Blue, true), A(Color::BrightWhite, Color::Blue, true), A(Color::Black, Color::Cyan),
         A(Color::Cyan), A(Color::Blue), A(Color::BrightCyan, Color::Default, true), A(Color::Cyan),
         A(Color::Black, Color::Cyan, true), A(Color::White, Color::Blue), A(Color::Cyan, Color::Default, true),
         A(Color::Cyan), A(Color::White), A(Color::Blue),
         A(Color::White, Color::Black), A(Color::Cyan, Color::Black), A(Color::Black, Color::Cyan, true)},
        {"mono", "单色",
         A(Color::Black, Color::White, true), A(Color::White, Color::Black, true), A(Color::White, Color::Black),
         A(Color::White), A(Color::BrightBlack), A(Color::White, Color::Default, true), A(Color::BrightBlack),
         A(Color::Black, Color::White, true), A(Color::White, Color::BrightBlack), A(Color::White, Color::Default, true),
         A(Color::White), A(Color::BrightBlack), A(Color::BrightBlack),
         A(Color::White, Color::Black), A(Color::White, Color::Black, true), A(Color::Black, Color::White, true)},
    };
    return v;
}

static std::string themeConfigPath() {
#ifdef _WIN32
    std::string dir = ".";
    const char* ad = std::getenv("APPDATA");
    if (ad && *ad) dir = ad;
    dir += "\\TuiRegedit";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\config.ini";
#else
    const char* home = std::getenv("HOME");
    std::string dir = (home && *home) ? (std::string(home) + "/.config/tui-regedit") : std::string(".");
    if (home && *home) {  // mkdir 非递归, 逐级创建
        ::mkdir((std::string(home) + "/.config").c_str(), 0755);
        ::mkdir(dir.c_str(), 0755);
    }
    return dir + "/config.ini";
#endif
}

void TuiRegedit::loadTheme() {
    themeIndex_ = 0;
    std::ifstream f(themeConfigPath());
    if (!f) return;
    std::string line;
    const std::string key = "theme=";
    while (std::getline(f, line)) {
        line = trimStr(line);
        if (line.compare(0, key.size(), key) != 0) continue;
        std::string id = trimStr(line.substr(key.size()));
        for (size_t i = 0; i < allThemes().size(); i++) {
            if (allThemes()[i].id == id) { themeIndex_ = (int)i; return; }
        }
    }
}

void TuiRegedit::saveTheme() {
    std::ofstream f(themeConfigPath(), std::ios::trunc);
    if (f) f << "# TUI Regedit config\ntheme=" << allThemes()[(size_t)themeIndex_].id << "\n";
}

TuiRegedit::TuiRegedit(std::unique_ptr<IRegistry> reg)
    : reg_(std::move(reg)), con_(Console::instance()) {}

void TuiRegedit::run() {
    con_.init();
    loadTheme();
    initRoots();
    setStatus("就绪。Ctrl+P 命令面板 │ ? 帮助。鼠标: 单击/双击/右键/滚轮，顶部路径可点击跳转。");
    while (running_) {
        draw();
        con_.present();
        Key k = con_.readKey();
        handleKey(k);
    }
    con_.shutdown();
}

// ---------------- 树 ----------------
void TuiRegedit::initRoots() {
    roots_.clear();
    for (auto& r : reg_->listRoots()) {
        auto n = std::make_unique<TreeNode>();
        n->name = r; n->fullPath = r; n->depth = 0;
        roots_.push_back(std::move(n));
    }
    rebuildVisible();
    for (size_t i = 0; i < visible_.size(); i++) {
        if (visible_[i]->fullPath == "HKEY_CURRENT_USER") { treeSel_ = (int)i; break; }
    }
    if (selectedNode()) {
        selectedNode()->expanded = true;
        rebuildVisible();
        for (size_t i = 0; i < visible_.size(); i++)
            if (visible_[i]->fullPath == "HKEY_CURRENT_USER") { treeSel_ = (int)i; break; }
    }
    refreshValues();
}

void TuiRegedit::rebuildVisible() {
    visible_.clear();
    for (auto& r : roots_) addVisible(r.get());
    if (visible_.empty()) treeSel_ = 0;
    else treeSel_ = clampInt(treeSel_, 0, (int)visible_.size() - 1);
}

void TuiRegedit::addVisible(TreeNode* n) {
    visible_.push_back(n);
    if (n->expanded) {
        if (!n->loaded) loadChildren(n);
        for (auto& c : n->children) addVisible(c.get());
    }
}

void TuiRegedit::loadChildren(TreeNode* n, bool force) {
    if (!n) return;
    if (n->loaded && !force) return;
    auto subs = reg_->listSubkeys(n->fullPath);
    n->children.clear();
    for (auto& s : subs) {
        auto c = std::make_unique<TreeNode>();
        c->name = s;
        c->fullPath = joinPath(n->fullPath, s);
        c->depth = n->depth + 1;
        c->parent = n;
        n->children.push_back(std::move(c));
    }
    n->loaded = true;
    n->hasChildren = !subs.empty();
}

void TuiRegedit::refreshValues() {
    TreeNode* n = selectedNode();
    currentPath_ = n ? n->fullPath : "";
    if (!currentPath_.empty()) values_ = reg_->listValues(currentPath_);
    else values_.clear();
    if (values_.empty()) valSel_ = 0;
    else valSel_ = clampInt(valSel_, 0, (int)values_.size() - 1);
    valTop_ = 0;
}

TuiRegedit::TreeNode* TuiRegedit::selectedNode() {
    if (treeSel_ < 0 || treeSel_ >= (int)visible_.size()) return nullptr;
    return visible_[treeSel_];
}

void TuiRegedit::ensureVisible(int sel, int& top, int page) {
    if (page <= 0) return;
    if (sel < top) top = sel;
    if (sel >= top + page) top = sel - page + 1;
    if (top < 0) top = 0;
}

bool TuiRegedit::ensurePathVisible(const std::string& fullPath) {
    if (fullPath.empty()) return false;
    std::string root, sub;
    splitRootPath(fullPath, root, sub);
    TreeNode* cur = nullptr;
    for (auto& r : roots_) if (toLowerStr(r->fullPath) == toLowerStr(root)) { cur = r.get(); break; }
    if (!cur) return false;
    if (!sub.empty()) {
        for (auto& seg : splitBy(sub, '\\')) {
            loadChildren(cur);
            cur->expanded = true;
            TreeNode* next = nullptr;
            for (auto& c : cur->children)
                if (toLowerStr(c->name) == toLowerStr(seg)) { next = c.get(); break; }
            if (!next) { rebuildVisible(); return false; }
            cur = next;
        }
    }
    rebuildVisible();
    for (size_t i = 0; i < visible_.size(); i++) {
        if (toLowerStr(visible_[i]->fullPath) == toLowerStr(fullPath)) {
            treeSel_ = (int)i;
            refreshValues();
            valSel_ = 0; valTop_ = 0;
            return true;
        }
    }
    return false;
}

void TuiRegedit::toggleExpand(TreeNode* n) {
    if (!n) return;
    if (!n->expanded) {
        loadChildren(n);
        if (!n->hasChildren) { setStatus("该项没有子项。"); return; }
        n->expanded = true;
        setStatus("已展开: " + n->fullPath);
    } else {
        n->expanded = false;
        setStatus("已收起: " + n->fullPath);
    }
    rebuildVisible();
    for (size_t i = 0; i < visible_.size(); i++)
        if (visible_[i] == n) { treeSel_ = (int)i; break; }
}

// ---------------- 布局与鼠标命中 ----------------
TuiRegedit::Layout TuiRegedit::calcLayout(const Size& s) {
    Layout L;
    L.W = s.w; L.H = s.h;
    int boxY = 1, boxH = s.h - 2;  // 顶栏 1 行 + 底状态栏 1 行, 中间全给面板
    int treeW = s.w * 40 / 100;
    if (treeW < 22) treeW = 22;
    if (treeW > s.w - 30) treeW = s.w - 30;
    L.treeX = 0; L.treeY = boxY; L.treeW = treeW; L.treeH = boxH;
    L.valX = treeW; L.valY = boxY; L.valW = s.w - treeW; L.valH = boxH;
    L.treeListY = boxY + 1; L.treeRows = boxH - 2;
    L.valListY = boxY + 3; L.valRows = boxH - 2 - 2;
    return L;
}

int TuiRegedit::treeIndexAt(const Layout& L, int my) {
    int r = my - L.treeListY;
    if (r < 0 || r >= L.treeRows) return -1;
    int idx = treeTop_ + r;
    if (idx < 0 || idx >= (int)visible_.size()) return -1;
    return idx;
}

int TuiRegedit::valIndexAt(const Layout& L, int my) {
    int r = my - L.valListY;
    if (r < 0 || r >= L.valRows) return -1;
    int idx = valTop_ + r;
    if (idx < 0 || idx >= (int)values_.size()) return -1;
    return idx;
}

bool TuiRegedit::isDoubleClick(const Key& k) {
    using namespace std::chrono;
    if (!k.mpress) return false;
    auto now = steady_clock::now();
    bool dbl = (k.mbutton == 1 && lastMouseBtn_ == 1 && lastMouseX_ == k.mx &&
                lastMouseY_ == k.my &&
                duration_cast<milliseconds>(now - lastMouseTime_).count() < 500);
    lastMouseTime_ = now; lastMouseX_ = k.mx; lastMouseY_ = k.my; lastMouseBtn_ = k.mbutton;
    if (dbl) lastMouseBtn_ = 0;  // 避免三击被连判为双击
    return dbl;
}

void TuiRegedit::scrollbarJump(int pane, int my) {
    Layout L = calcLayout(con_.getSize());
    int total = pane == 0 ? (int)visible_.size() : (int)values_.size();
    int rows = pane == 0 ? L.treeRows : L.valRows;
    int listY = pane == 0 ? L.treeListY : L.valListY;
    if (total <= rows || rows <= 1) return;
    int thY, thH;
    scrollbarGeom(total, 0, rows, thY, thH);  // thH 与 top 无关
    int denom = rows - thH;
    int t = denom <= 0 ? 0 : (my - listY - thH / 2) * (total - rows) / denom;
    t = clampInt(t, 0, total - rows);
    if (pane == 0) {
        treeTop_ = t;
        if (treeSel_ < t || treeSel_ >= t + rows) {
            treeSel_ = clampInt(treeSel_, t, t + rows - 1);
            refreshValues(); valSel_ = 0; valTop_ = 0;
        }
    } else {
        valTop_ = t;
        valSel_ = clampInt(valSel_, t, t + rows - 1);
    }
}

void TuiRegedit::handleMouse(const Key& k) {
    Layout L = calcLayout(con_.getSize());
    bool inTree = inRect(k.mx, k.my, L.treeX, L.treeY, L.treeW, L.treeH);
    bool inVal = inRect(k.mx, k.my, L.valX, L.valY, L.valW, L.valH);

    if (k.mwheel != 0) {
        int d = k.mwheel > 0 ? -3 : 3;
        if (inTree && !visible_.empty()) {
            treeSel_ = clampInt(treeSel_ + d, 0, (int)visible_.size() - 1);
            refreshValues(); valSel_ = 0; valTop_ = 0;
            activePane_ = 0;
        } else if (inVal && !values_.empty()) {
            valSel_ = clampInt(valSel_ + d, 0, (int)values_.size() - 1);
            activePane_ = 1;
        }
        return;
    }
    if (k.mrelease) { sbDrag_ = false; return; }
    if (k.mdrag) {
        if (sbDrag_) scrollbarJump(sbDragPane_, k.my);
        return;
    }
    if (!k.mpress) return;

    if (k.mbutton == 3) {  // 右键: 选中 + 上下文菜单
        if (inTree) {
            int idx = treeIndexAt(L, k.my);
            if (idx >= 0 && idx != treeSel_) {
                treeSel_ = idx; refreshValues(); valSel_ = 0; valTop_ = 0;
            }
            activePane_ = 0;
            if (idx >= 0) contextMenu();
        } else if (inVal) {
            int idx = valIndexAt(L, k.my);
            if (idx >= 0) valSel_ = idx;
            activePane_ = 1;
            if (idx >= 0) contextMenu();
        }
        return;
    }
    if (k.mbutton != 1) return;

    bool dbl = isDoubleClick(k);
    if (k.my == 0) { actionGoto(); return; }  // 单击顶部路径 -> 转到

    if (inTree) {
        int sbW = (int)visible_.size() > L.treeRows ? 1 : 0;
        int sbCol = L.treeX + 1 + (L.treeW - 2 - sbW);
        if (sbW && k.mx == sbCol && k.my >= L.treeListY && k.my < L.treeListY + L.treeRows) {
            activePane_ = 0;
            int thY, thH;
            if (scrollbarGeom((int)visible_.size(), treeTop_, L.treeRows, thY, thH)) {
                int r = k.my - L.treeListY;
                if (r < thY) {
                    treeSel_ = clampInt(treeSel_ - L.treeRows, 0, (int)visible_.size() - 1);
                    refreshValues(); valSel_ = 0; valTop_ = 0;
                } else if (r >= thY + thH) {
                    treeSel_ = clampInt(treeSel_ + L.treeRows, 0, (int)visible_.size() - 1);
                    refreshValues(); valSel_ = 0; valTop_ = 0;
                } else { sbDrag_ = true; sbDragPane_ = 0; }
            }
            return;
        }
        int idx = treeIndexAt(L, k.my);
        if (idx < 0) return;
        activePane_ = 0;
        if (idx != treeSel_) { treeSel_ = idx; refreshValues(); valSel_ = 0; valTop_ = 0; }
        TreeNode* n = visible_[treeSel_];
        int mkx = L.treeX + 1 + n->depth * 2;  // [+]/[-] 起始列
        bool onMarker = (k.mx >= mkx && k.mx < mkx + 3);
        if (dbl || onMarker) toggleExpand(n);
    } else if (inVal) {
        int sbW = (int)values_.size() > L.valRows ? 1 : 0;
        int sbCol = L.valX + 1 + (L.valW - 2 - sbW);
        if (sbW && k.mx == sbCol && k.my >= L.valListY && k.my < L.valListY + L.valRows) {
            activePane_ = 1;
            int thY, thH;
            if (scrollbarGeom((int)values_.size(), valTop_, L.valRows, thY, thH)) {
                int r = k.my - L.valListY;
                if (r < thY) valSel_ = clampInt(valSel_ - L.valRows, 0, (int)values_.size() - 1);
                else if (r >= thY + thH) valSel_ = clampInt(valSel_ + L.valRows, 0, (int)values_.size() - 1);
                else { sbDrag_ = true; sbDragPane_ = 1; }
            }
            return;
        }
        int idx = valIndexAt(L, k.my);
        if (idx < 0) return;
        activePane_ = 1;
        valSel_ = idx;
        if (dbl) actionEditValue();
    }
}

// ---------------- 绘制 ----------------
static Color typeColor(uint32_t t) {
    switch (t) {
        case REG_SZ_T: case REG_EXPAND_SZ_T: return Color::Green;
        case REG_DWORD_T: return Color::Yellow;
        case REG_QWORD_T: return Color::Magenta;
        case REG_BINARY_T: return Color::Red;
        case REG_MULTI_SZ_T: return Color::Cyan;
        default: return Color::White;
    }
}

void TuiRegedit::drawBox(Screen& scr, int x, int y, int w, int h,
                         const std::string& title, bool active) {
    if (w < 4 || h < 3) return;
    const Theme& th = theme();
    Attr bA = active ? th.borderOn : th.borderOff;
    scr.putStr(x, y, "┌" + rep("─", w - 2) + "┐", bA);
    for (int r = 1; r < h - 1; r++) {
        scr.putStr(x, y + r, "│", bA);
        scr.fillRect(x + 1, y + r, w - 2, 1, " ");
        scr.putStr(x + w - 1, y + r, "│", bA);
    }
    scr.putStr(x, y + h - 1, "└" + rep("─", w - 2) + "┘", bA);
    if (!title.empty()) {
        std::string t = " " + title + " ";
        if (utf8DisplayWidth(t) < w - 2)
            scr.putStr(x + 2, y, t, active ? th.titleOn : th.titleOff);
    }
}

void TuiRegedit::drawHeader(Screen& scr, const Layout& L) {
    const Theme& th = theme();
    std::string left = " TUI Regedit ";
    std::string path = currentPath_.empty() ? "" : "— " + currentPath_;
    scr.putStr(0, 0, padDisplay(left, L.W), th.header);
    int lw = utf8DisplayWidth(left);  // 路径高亮显示, 暗示可点击跳转
    if (lw < L.W && !path.empty())
        scr.putStr(lw, 0, truncateDisplay(path, L.W - lw), th.headerPath);
}

void TuiRegedit::drawTreePane(Screen& scr, const Layout& L) {
    const Theme& th = theme();
    bool active = (activePane_ == 0);
    char title[128];
    snprintf(title, sizeof(title), "注册表项 (%d/%d)", treeSel_ + 1, (int)visible_.size());
    drawBox(scr, L.treeX, L.treeY, L.treeW, L.treeH, title, active);
    int sbW = (int)visible_.size() > L.treeRows ? 1 : 0;
    int textW = L.treeW - 2 - sbW;
    if (textW <= 0 || L.treeRows <= 0) return;
    ensureVisible(treeSel_, treeTop_, L.treeRows);
    int thY = 0, thH = 0;
    bool hasSb = sbW && scrollbarGeom((int)visible_.size(), treeTop_, L.treeRows, thY, thH);
    for (int r = 0; r < L.treeRows; r++) {
        int idx = treeTop_ + r;
        int yy = L.treeListY + r;
        if (idx >= (int)visible_.size()) {
            scr.fillRect(L.treeX + 1, yy, textW, 1, " ");
        } else {
            TreeNode* n = visible_[idx];
            bool sel = (idx == treeSel_);
            std::string marker;
            if (n->loaded && !n->hasChildren) marker = " • ";
            else if (n->expanded) marker = "[-]";
            else marker = "[+]";
            std::string text = padDisplay(truncateDisplay(
                std::string((size_t)(n->depth * 2), ' ') + marker + " " + n->name, textW), textW);
            if (sel)
                scr.putStr(L.treeX + 1, yy, text, active ? th.selOn : th.selOff);
            else if (n->depth == 0)
                scr.putStr(L.treeX + 1, yy, text, th.root);
            else
                scr.putStr(L.treeX + 1, yy, text);
        }
        if (hasSb) {
            bool thumb = (r >= thY && r < thY + thH);
            scr.putStr(L.treeX + 1 + textW, yy, thumb ? "█" : "│",
                       thumb ? (active ? th.thumbOn : th.thumbOff) : th.track);
        }
    }
}

void TuiRegedit::drawValuePane(Screen& scr, const Layout& L) {
    const Theme& th = theme();
    bool active = (activePane_ == 1);
    char title[128];
    snprintf(title, sizeof(title), "值 (%d/%d)", values_.empty() ? 0 : valSel_ + 1, (int)values_.size());
    drawBox(scr, L.valX, L.valY, L.valW, L.valH, title, active);
    int sbW = (int)values_.size() > L.valRows ? 1 : 0;
    int iw = L.valW - 2 - sbW;
    if (iw <= 4 || L.valRows <= 0) return;
    int nameW = std::max(8, iw * 30 / 100);
    int typeW = 15;
    int dataW = iw - nameW - typeW - 4;
    if (dataW < 8) { dataW = 8; typeW = iw - nameW - dataW - 4; if (typeW < 8) typeW = 8; }
    int x = L.valX + 1;
    Attr hdrA = th.dlgText; hdrA.bold = true;
    scr.putStr(x, L.valY + 1, padDisplay(truncateDisplay(padDisplay("名称", nameW) + "│ " + padDisplay("类型", typeW) + "│ 数据", iw), iw), hdrA);
    scr.putStr(x, L.valY + 2, truncateDisplay(rep("─", nameW) + "┼─" + rep("─", typeW) + "┼─" + rep("─", dataW), iw), th.track);
    ensureVisible(valSel_, valTop_, L.valRows);
    int thY = 0, thH = 0;
    bool hasSb = sbW && scrollbarGeom((int)values_.size(), valTop_, L.valRows, thY, thH);
    for (int r = 0; r < L.valRows; r++) {
        int idx = valTop_ + r;
        int yy = L.valListY + r;
        if (idx >= (int)values_.size()) {
            scr.fillRect(x, yy, iw, 1, " ");
        } else {
            const RegValue& v = values_[idx];
            bool sel = (idx == valSel_);
            std::string nm = truncateDisplay(v.displayName(), nameW);
            std::string tp = truncateDisplay(v.typeName(), typeW);
            std::string dt = truncateDisplay(v.prettyData(), dataW);
            if (sel) {
                scr.putStr(x, yy, padDisplay(padDisplay(nm, nameW) + "│ " + padDisplay(tp, typeW) + "│ " + dt, iw),
                           active ? th.selOn : th.selOff);
            } else {
                scr.putStr(x, yy, padDisplay(nm, nameW));
                scr.putStr(x + nameW, yy, "│ ", th.track);
                scr.putStr(x + nameW + 2, yy, padDisplay(tp, typeW), A(typeColor(v.type)));
                scr.putStr(x + nameW + 2 + typeW, yy, "│ ", th.track);
                scr.putStr(x + nameW + 2 + typeW + 2, yy, padDisplay(dt, dataW));
            }
        }
        if (hasSb) {
            bool thumb = (r >= thY && r < thY + thH);
            scr.putStr(x + iw, yy, thumb ? "█" : "│",
                       thumb ? (active ? th.thumbOn : th.thumbOff) : th.track);
        }
    }
}

void TuiRegedit::drawStatusBar(Screen& scr, const Layout& L) {
    int beW = 18;
    int msgW = L.W - beW - 3;
    if (msgW < 10) msgW = L.W - 1;
    std::string line = truncateDisplay(" " + padDisplay(truncateDisplay(statusMsg_, msgW), msgW) + " " +
                                       padDisplay(truncateDisplay(reg_->backendName(), beW), beW), L.W);
    scr.putStr(0, L.H - 1, padDisplay(line, L.W), theme().status);
}

void TuiRegedit::draw() {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    scr.begin(s.w, s.h);
    con_.hideCursor();
    if (s.w < 50 || s.h < 11) {
        scr.fillRect(0, 0, s.w, s.h, " ");
        scr.putStr(0, 0, "窗口太小 (需要至少 50x11), 请放大终端。");
        return;
    }
    Layout L = calcLayout(s);
    drawHeader(scr, L);
    drawTreePane(scr, L);
    drawValuePane(scr, L);
    drawStatusBar(scr, L);
}

// ---------------- 按键 ----------------
void TuiRegedit::handleKey(const Key& k) {
    if (k.mouse) { handleMouse(k); return; }
    if (k.type == Key::Unknown) return;
    if (k.type == Key::Tab || k.type == Key::ShiftTab) {
        activePane_ = 1 - activePane_;
        setStatus(activePane_ == 0 ? "已切换到: 注册表项(树)" : "已切换到: 值列表");
        return;
    }
    if (k.type == Key::F1 || (k.type == Key::Char && k.ch == '?')) { dialogHelp(); return; }
    if (k.type == Key::F5) { actionRefresh(); return; }
    if (k.type == Key::F9) { dialogAbout(); return; }
    if (k.type == Key::F10) { actionTheme(); return; }
    if (k.type == Key::Char && (k.isCtrl('c') || k.isCtrl('q'))) { running_ = false; return; }
    if (k.type == Key::Char && (k.ch == 'q' || k.ch == 'Q')) { running_ = false; return; }
    if (k.type == Key::Esc) { running_ = false; return; }
    if (k.type == Key::Char && (k.isCtrl('f') || k.ch == 'f' || k.ch == 'F' || k.ch == '/')) {
        actionSearch(); return;
    }
    if (k.type == Key::Char && (k.isCtrl('l') || k.isCtrl('g'))) { actionGoto(); return; }
    if (k.type == Key::Char && k.isCtrl('p')) { commandPalette(); return; }
    if (k.type == Key::Char && k.isCtrl('e')) { actionExport(); return; }
    if (k.type == Key::Char && k.isCtrl('i')) { actionImport(); return; }

    if (activePane_ == 0) handleTreeKey(k);
    else handleValueKey(k);
}

void TuiRegedit::handleTreeKey(const Key& k) {
    TreeNode* n = selectedNode();
    Layout L = calcLayout(con_.getSize());
    int page = L.treeRows;
    if (page < 1) page = 1;
    auto moved = [&]() { refreshValues(); valSel_ = 0; valTop_ = 0; };
    switch (k.type) {
        case Key::Up: if (treeSel_ > 0) { treeSel_--; moved(); } return;
        case Key::Down: if (treeSel_ + 1 < (int)visible_.size()) { treeSel_++; moved(); } return;
        case Key::Home: treeSel_ = 0; moved(); return;
        case Key::End: treeSel_ = (int)visible_.size() - 1; moved(); return;
        case Key::PgUp: treeSel_ = clampInt(treeSel_ - page, 0, (int)visible_.size() - 1); moved(); return;
        case Key::PgDn: treeSel_ = clampInt(treeSel_ + page, 0, (int)visible_.size() - 1); moved(); return;
        case Key::Right:
        case Key::Enter:
            if (!n) return;
            if (!n->expanded) toggleExpand(n);
            else if (treeSel_ + 1 < (int)visible_.size() && visible_[treeSel_ + 1]->parent == n) {
                treeSel_++; moved();
            }
            return;
        case Key::Left:
            if (!n) return;
            if (n->expanded) toggleExpand(n);
            else if (n->parent) {
                for (size_t i = 0; i < visible_.size(); i++)
                    if (visible_[i] == n->parent) { treeSel_ = (int)i; break; }
                moved();
            }
            return;
        case Key::Backspace:
            if (n && n->parent) {
                for (size_t i = 0; i < visible_.size(); i++)
                    if (visible_[i] == n->parent) { treeSel_ = (int)i; break; }
                moved();
            }
            return;
        case Key::Delete: actionDelete(); return;
        case Key::F2: actionRename(); return;
        default: break;
    }
    if (k.type == Key::Char && !k.ctrl && !k.alt) {
        char32_t c = k.ch;
        if (c == 'j') { if (treeSel_ + 1 < (int)visible_.size()) { treeSel_++; moved(); } }
        else if (c == 'k') { if (treeSel_ > 0) { treeSel_--; moved(); } }
        else if (c == 'h') handleTreeKey(Key{Key::Left});
        else if (c == 'l' || c == 'o') handleTreeKey(Key{Key::Right});
        else if (c == 'n' || c == 'N') actionNewKey();
        else if (c == 'd' || c == 'D') actionDelete();
        else if (c == 'r' || c == 'R') actionRename();
        else if (c == 'e' || c == 'E') { activePane_ = 1; setStatus("已切换到: 值列表"); }
    }
}

void TuiRegedit::handleValueKey(const Key& k) {
    Layout L = calcLayout(con_.getSize());
    int page = L.valRows;
    if (page < 1) page = 1;
    switch (k.type) {
        case Key::Up: if (valSel_ > 0) valSel_--; return;
        case Key::Down: if (valSel_ + 1 < (int)values_.size()) valSel_++; return;
        case Key::Home: valSel_ = 0; return;
        case Key::End: valSel_ = values_.empty() ? 0 : (int)values_.size() - 1; return;
        case Key::PgUp: valSel_ = clampInt(valSel_ - page, 0, (int)values_.size() - 1); return;
        case Key::PgDn: valSel_ = clampInt(valSel_ + page, 0, (int)values_.size() - 1); return;
        case Key::Enter: actionEditValue(); return;
        case Key::Delete: actionDelete(); return;
        case Key::F2: actionRename(); return;
        case Key::Left: activePane_ = 0; setStatus("已切换到: 注册表项(树)"); return;
        default: break;
    }
    if (k.type == Key::Char && !k.ctrl && !k.alt) {
        char32_t c = k.ch;
        if (c == 'j') { if (valSel_ + 1 < (int)values_.size()) valSel_++; }
        else if (c == 'k') { if (valSel_ > 0) valSel_--; }
        else if (c == 'e' || c == 'E') actionEditValue();
        else if (c == 'n' || c == 'N') actionNewValue();
        else if (c == 'd' || c == 'D') actionDelete();
        else if (c == 'r' || c == 'R') actionRename();
    }
}

// ---------------- 对话框 ----------------
void TuiRegedit::dialogMsg(const std::string& title, const std::string& msg) {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    auto lines = splitBy(msg, '\n');
    int contentW = 0;
    for (auto& ln : lines) contentW = std::max(contentW, utf8DisplayWidth(ln));
    contentW = std::max(contentW, utf8DisplayWidth(title));
    int w = std::min(s.w - 6, contentW + 6);
    if (w < 30) w = 30;
    int h = (int)lines.size() + 5;
    if (h > s.h - 2) h = s.h - 2;
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, title, true);
        for (size_t i = 0; i < lines.size() && (int)i < h - 4; i++)
            scr.putStr(x + 2, y + 2 + (int)i, padDisplay(truncateDisplay(lines[i], w - 4), w - 4), th.dlgText);
        scr.putStr(x + 2, y + h - 2, padDisplay(truncateDisplay("按 Enter / Esc 关闭", w - 4), w - 4), th.dlgHint);
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) continue;
        if (k.type == Key::Enter || k.type == Key::Esc || k.type == Key::Char) return;
    }
}

bool TuiRegedit::dialogConfirm(const std::string& title, const std::string& msg) {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    auto lines = splitBy(msg, '\n');
    int contentW = 0;
    for (auto& ln : lines) contentW = std::max(contentW, utf8DisplayWidth(ln));
    int w = std::min(s.w - 6, std::max(contentW + 6, 40));
    int h = (int)lines.size() + 6;
    if (h > s.h - 2) h = s.h - 2;
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, title, true);
        for (size_t i = 0; i < lines.size() && (int)i < h - 5; i++)
            scr.putStr(x + 2, y + 2 + (int)i, padDisplay(truncateDisplay(lines[i], w - 4), w - 4), th.dlgText);
        scr.putStr(x + 2, y + h - 3,
                   padDisplay(truncateDisplay("确定吗?  [Y]是  [N]否  (Enter=是, Esc=否)", w - 4), w - 4),
                   th.dlgHint);
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) continue;
        if (k.type == Key::Enter) return true;
        if (k.type == Key::Esc) return false;
        if (k.type == Key::Char) {
            if (k.ch == 'y' || k.ch == 'Y') return true;
            if (k.ch == 'n' || k.ch == 'N' || k.ch == 'q' || k.ch == 'Q') return false;
        }
    }
}

bool TuiRegedit::dialogInput(const std::string& title, const std::string& prompt,
                             std::string& out, const std::string& initial,
                             const std::string& hint) {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    int w = std::min(s.w - 6, 76);
    if (w < 40) w = s.w - 4;
    int h = hint.empty() ? 8 : 9;
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    std::string buf = initial;
    size_t cursor = buf.size();
    int fieldW = w - 8;
    if (fieldW < 10) fieldW = 10;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, title, true);
        scr.putStr(x + 2, y + 2, padDisplay(truncateDisplay(prompt, w - 4), w - 4), th.dlgText);
        size_t left = 0;
        while (left < cursor) {
            if (utf8DisplayWidth(buf.substr(left, cursor - left)) < fieldW) break;
            left += utf8NextCharLen(buf, left);
        }
        std::string vis = buf.substr(left);
        std::string shown;
        {
            int dw = 0;
            for (size_t i = 0; i < vis.size();) {
                size_t len = 1;
                uint32_t cp = utf8DecodeOne(vis.c_str() + i, vis.size() - i, len);
                int cw = isWideCodepoint(cp) ? 2 : 1;
                if (dw + cw > fieldW) break;
                shown.append(vis.substr(i, len));
                dw += cw; i += len;
            }
        }
        Attr bracket = th.borderOn; bracket.bg = th.dlgText.bg;
        scr.putStr(x + 2, y + 4, "[ ", bracket);
        scr.putStr(x + 4, y + 4, padDisplay(shown, fieldW), th.dlgText);
        scr.putStr(x + 4 + fieldW, y + 4, " ]", bracket);
        if (!hint.empty())
            scr.putStr(x + 2, y + 6, padDisplay(truncateDisplay(hint, w - 4), w - 4),
                       A(Color::BrightBlack, th.dlgText.bg));
        scr.putStr(x + 2, y + h - 2,
                   padDisplay(truncateDisplay("Enter 确认 │ Esc 取消 │ ←→移动 │ 鼠标单击定位 │ Ctrl+U清空", w - 4), w - 4),
                   th.dlgHint);
        con_.present();
        int cxx = (int)utf8DisplayWidth(buf.substr(left, cursor - left));
        con_.moveCursor(x + 4 + cxx, y + 4);
        con_.showCursor();
        con_.flushOut();

        Key k = con_.readKey();
        con_.hideCursor();
        if (k.mouse) {
            if (k.mpress && k.mbutton == 1) {
                isDoubleClick(k);  // 维持双击计时状态
                if (k.my == y + 4 && k.mx >= x + 4 && k.mx < x + 4 + fieldW)
                    cursor = cursorFromColumn(buf, left, k.mx - (x + 4));
            }
            continue;
        }
        if (k.type == Key::Enter) { out = buf; return true; }
        if (k.type == Key::Esc) return false;
        if (k.type == Key::Left) { if (cursor > 0) cursor -= utf8PrevCharLen(buf, cursor); }
        else if (k.type == Key::Right) { if (cursor < buf.size()) cursor += utf8NextCharLen(buf, cursor); }
        else if (k.type == Key::Home) cursor = 0;
        else if (k.type == Key::End) cursor = buf.size();
        else if (k.type == Key::Backspace) {
            if (cursor > 0) { size_t l = utf8PrevCharLen(buf, cursor); buf.erase(cursor - l, l); cursor -= l; }
        }
        else if (k.type == Key::Delete) {
            if (cursor < buf.size()) buf.erase(cursor, utf8NextCharLen(buf, cursor));
        }
        else if (k.type == Key::Char) {
            if (k.isCtrl('c')) return false;
            if (k.isCtrl('u') || (k.ch == 21)) { buf.clear(); cursor = 0; }
            else if (k.isCtrl('a') || (k.ch == 1)) cursor = 0;
            else if (k.isCtrl('e') || (k.ch == 5)) cursor = buf.size();
            else if (k.isCtrl('w') || (k.ch == 23)) {
                while (cursor > 0 && buf[cursor - 1] == ' ') { buf.erase(cursor - 1, 1); cursor--; }
                while (cursor > 0 && buf[cursor - 1] != ' ') {
                    size_t l = utf8PrevCharLen(buf, cursor); buf.erase(cursor - l, l); cursor -= l;
                }
            }
            else if (k.ch >= 0x20 && !k.ctrl && !k.alt) {
                std::string ins = encodeUtf8(k.ch);
                buf.insert(cursor, ins);
                cursor += ins.size();
            }
        }
    }
}

int TuiRegedit::dialogMenu(const std::string& title, const std::vector<std::string>& options,
                           const std::string& hint) {
    if (options.empty()) return -1;
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    int contentW = 0;
    for (auto& o : options) contentW = std::max(contentW, utf8DisplayWidth(o));
    int w = std::min(s.w - 6, std::max(contentW + 8, 36));
    int perPage = std::min((int)options.size(), s.h - 10);
    if (perPage < 3) perPage = 3;
    int h = perPage + (hint.empty() ? 5 : 6);
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    int sel = 0, top = 0;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, title, true);
        ensureVisible(sel, top, perPage);
        for (int r = 0; r < perPage; r++) {
            int idx = top + r;
            int yy = y + 2 + r;
            if (idx >= (int)options.size()) { scr.fillRect(x + 2, yy, w - 4, 1, " "); continue; }
            std::string t = truncateDisplay(options[idx], w - 6);
            if (idx == sel)
                scr.putStr(x + 2, yy, padDisplay("▸ " + t, w - 4), th.dlgSel);
            else
                scr.putStr(x + 2, yy, padDisplay("  " + t, w - 4), th.dlgText);
        }
        int fy = y + 2 + perPage;
        if (!hint.empty()) {
            scr.putStr(x + 2, fy, padDisplay(truncateDisplay(hint, w - 4), w - 4),
                       A(Color::BrightBlack, th.dlgText.bg));
            fy++;
        }
        scr.putStr(x + 2, fy, padDisplay(truncateDisplay("↑↓选择 │ Enter确认 │ Esc取消 │ 单击选/双击确认/滚轮", w - 4), w - 4),
                   th.dlgHint);
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) {
            if (k.mwheel > 0) { if (sel > 0) sel--; }
            else if (k.mwheel < 0) { if (sel + 1 < (int)options.size()) sel++; }
            else if (k.mpress && k.mbutton == 1) {
                bool dbl = isDoubleClick(k);
                int r = k.my - (y + 2);
                if (r >= 0 && r < perPage) {
                    int idx = top + r;
                    if (idx >= 0 && idx < (int)options.size()) {
                        sel = idx;
                        if (dbl) return idx;
                    }
                }
            }
            continue;
        }
        if (k.type == Key::Up) { if (sel > 0) sel--; }
        else if (k.type == Key::Down) { if (sel + 1 < (int)options.size()) sel++; }
        else if (k.type == Key::Home) sel = 0;
        else if (k.type == Key::End) sel = (int)options.size() - 1;
        else if (k.type == Key::PgUp) sel = clampInt(sel - perPage, 0, (int)options.size() - 1);
        else if (k.type == Key::PgDn) sel = clampInt(sel + perPage, 0, (int)options.size() - 1);
        else if (k.type == Key::Enter) return sel;
        else if (k.type == Key::Esc) return -1;
        else if (k.type == Key::Char && !k.ctrl) {
            if (k.ch == 'j' && sel + 1 < (int)options.size()) sel++;
            else if (k.ch == 'k' && sel > 0) sel--;
            else if (k.ch == 'q' || k.ch == 'Q') return -1;
            else if (k.ch >= '1' && k.ch <= '9') {
                int idx = (int)(k.ch - '1');
                if (idx < (int)options.size()) return idx;
            }
        }
    }
}

void TuiRegedit::dialogHelp() {
    std::vector<std::string> lines = {
        "【TUI Regedit 帮助】",
        "",
        "命令面板 (推荐新手用这个):",
        "  Ctrl+P              打开命令面板, 输入命令名过滤, 回车执行",
        "                      所有操作 (转到/查找/新建/删除/导出/刷新…) 都在里面",
        "",
        "面板与导航:",
        "  Tab / Shift+Tab     在“注册表项(树)”与“值”面板间切换",
        "  ↑ ↓ k j             上下移动  |  Home/End 跳到头尾  |  PgUp/PgDn 翻页",
        "  → / Enter / l       (树)展开项, 或进入第一个子项",
        "  ← / h               (树)收起项, 或回到父项",
        "  Backspace           (树)回到父项",
        "  Enter / E           (值)编辑选中的值",
        "",
        "鼠标操作:",
        "  单击                选择项/值, 单击面板空白处切换面板",
        "  单击 [+]/[-]        直接展开/收起该项",
        "  单击顶部路径        打开“转到路径”对话框",
        "  双击                展开收起项 / 打开编辑值",
        "  右键                上下文菜单 (新建/删除/重命名/导出/转到/刷新)",
        "  滚轮                滚动鼠标所在的面板",
        "  右侧滚动条          单击空白处翻页, 按住滑块拖动快速滚动",
        "  对话框              菜单可单击选择、双击确认; 输入框可单击定位光标",
        "",
        "编辑操作:",
        "  N                   新建: 树面板=新建项, 值面板=新建值",
        "  D / Delete          删除: 树面板=删除项(含子项!), 值面板=删除值",
        "  R / F2              重命名: 项或值",
        "  E / Enter           编辑值 (字符串/DWORD/QWORD/二进制/多字符串)",
        "  F5                  刷新当前项",
        "",
        "查找 / 跳转 / 导入导出:",
        "  F / Ctrl+F / /      查找项名或值名 (支持从当前项或整机搜索)",
        "  Ctrl+L / Ctrl+G     转到指定路径 (支持 HKCU\\Software\\... 缩写)",
        "  Ctrl+E              导出当前项为 .reg 文件",
        "  Ctrl+I              导入 .reg 文件 (Windows 调用 reg import)",
        "",
        "其它:",
        "  F1 / ?              显示本帮助",
        "  F9                  关于本软件",
        "  F10                 切换配色主题 (也可在命令面板里找)",
        "  Q / Esc / Ctrl+C    退出程序",
        "",
        "注意:",
        "  • 修改 HKLM 等系统项通常需要【管理员权限】, 请右键“以管理员身份运行”。",
        "  • 删除项会连同其下所有子项与值一起删除, 操作前建议先导出备份。",
        "  • “(默认)”值不能删除与重命名, 只能编辑内容。",
        "  • DWORD/QWORD 支持十进制 (255) 与十六进制 (0xFF) 输入。",
        "  • 二进制值用十六进制编辑, 如: 01 02 AB CD。",
    };
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    int w = std::min(s.w - 4, 78);
    int h = std::min(s.h - 2, 28);
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    int top = 0;
    int perPage = h - 4;
    Attr secA = th.dlgText; secA.bold = true;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, "帮助 (↑↓/滚轮滚动, Esc关闭)", true);
        top = clampInt(top, 0, std::max(0, (int)lines.size() - perPage));
        for (int r = 0; r < perPage; r++) {
            int idx = top + r;
            int yy = y + 2 + r;
            if (idx >= (int)lines.size()) { scr.fillRect(x + 2, yy, w - 4, 1, " "); continue; }
            std::string ln = padDisplay(truncateDisplay(lines[idx], w - 4), w - 4);
            Attr a = th.dlgText;
            if (!lines[idx].empty() && lines[idx].find("【") == 0) a = th.dlgHint;
            else if (!lines[idx].empty() && lines[idx].back() == ':') a = secA;
            scr.putStr(x + 2, yy, ln, a);
        }
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) {
            if (k.mwheel > 0 && top > 0) top--;
            else if (k.mwheel < 0 && top + perPage < (int)lines.size()) top++;
            continue;
        }
        if (k.type == Key::Esc || k.type == Key::Enter || k.type == Key::F1) return;
        if (k.type == Key::Up) { if (top > 0) top--; }
        else if (k.type == Key::Down) { if (top + perPage < (int)lines.size()) top++; }
        else if (k.type == Key::PgUp) top -= perPage;
        else if (k.type == Key::PgDn) top += perPage;
        else if (k.type == Key::Char && (k.ch == 'q' || k.ch == 'Q' || k.ch == '?')) return;
    }
}

void TuiRegedit::dialogAbout() {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    std::vector<std::string> lines = {
        "_____ _   _ ___   ____          _ _ _",
        "|_   _| | | |_ _| |  _ \\ ___  __| (_) |_",
        "  | | | | | || |  | |_) / _ \\/ _` | | __|",
        "  | | | |_| || |  |  _ <  __/ (_| | | |_",
        "  |_|  \\___/|___| |_| \\_\\___|\\__,_|_|\\__|",
        "",
        std::string("TUI Regedit v") + TUI_REGEDIT_VERSION,
        "终端里的 Windows 注册表编辑器",
        "",
        "作者: wudream813",
        "主页: https://github.com/wudream813/TUI-regedit",
        "后端: " + reg_->backendName(),
        "构建: " + buildInfo(),
        "许可: MIT",
    };
    int contentW = 0;
    for (auto& ln : lines) contentW = std::max(contentW, utf8DisplayWidth(ln));
    int w = std::min(s.w - 6, contentW + 10);
    if (w < 44) w = 44;
    int h = (int)lines.size() + 6;
    if (h > s.h - 2) h = s.h - 2;
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    Attr logoA = th.titleOn; logoA.bg = th.dlgText.bg;
    Attr verA = th.dlgHint; verA.bold = true;
    for (;;) {
        draw();
        drawBox(scr, x, y, w, h, "关于", true);
        for (size_t i = 0; i < lines.size() && (int)i < h - 5; i++) {
            std::string ln = centerDisplay(truncateDisplay(lines[i], w - 4), w - 4);
            Attr a = th.dlgText;
            if (i < 5) a = logoA;
            else if (i == 6) a = verA;
            scr.putStr(x + 2, y + 2 + (int)i, ln, a);
        }
        scr.putStr(x + 2, y + h - 2, centerDisplay("按任意键关闭", w - 4), th.dlgHint);
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) {
            if (k.mpress) return;
            continue;
        }
        if (k.type != Key::Unknown) return;
    }
}

bool TuiRegedit::dialogEditDword(const std::string& title, uint32_t& v) {
    for (;;) {
        std::string out;
        bool ok = dialogInput(title, "当前值: " + u32ToHex(v), out, std::to_string(v),
            "请输入十进制 (如 255) 或十六进制 (如 0xFF), 范围 0 ~ 4294967295");
        if (!ok) return false;
        uint32_t nv; std::string err;
        if (!stringToU32(out, nv, err)) { dialogMsg("输入无效", err); continue; }
        v = nv;
        return true;
    }
}

bool TuiRegedit::dialogEditQword(const std::string& title, uint64_t& v) {
    for (;;) {
        std::string out;
        bool ok = dialogInput(title, "当前值: " + u64ToHex(v), out, std::to_string(v),
            "请输入十进制或十六进制 (0x...), 范围 0 ~ 18446744073709551615");
        if (!ok) return false;
        uint64_t nv; std::string err;
        if (!stringToU64(out, nv, err)) { dialogMsg("输入无效", err); continue; }
        v = nv;
        return true;
    }
}

bool TuiRegedit::dialogEditBinary(const std::string& title, std::vector<uint8_t>& data) {
    for (;;) {
        std::string cur = data.empty() ? "(当前为空)" : bytesToHex(data, true, 256);
        std::string out;
        std::string initial = bytesToHex(data, true, 256);
        bool ok = dialogInput(title, "当前: " + truncateDisplay(cur, 60), out, initial,
            "十六进制字节, 如: 01 02 AB CD (空格可选, 长度须为偶数)");
        if (!ok) return false;
        std::vector<uint8_t> nd; std::string err;
        if (!hexToBytes(out, nd, err)) { dialogMsg("输入无效", err); continue; }
        data = nd;
        return true;
    }
}

bool TuiRegedit::dialogEditMulti(const std::string& title, std::vector<std::string>& lines) {
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    int w = std::min(s.w - 6, 76);
    int h = std::min(s.h - 4, 20);
    int x = (s.w - w) / 2, y = (s.h - h) / 2;
    int sel = 0, top = 0;
    int perPage = h - 6;
    if (perPage < 3) perPage = 3;
    for (;;) {
        if (!lines.empty()) sel = clampInt(sel, 0, (int)lines.size() - 1);
        else sel = 0;
        draw();
        drawBox(scr, x, y, w, h, title, true);
        ensureVisible(sel, top, perPage);
        for (int r = 0; r < perPage; r++) {
            int idx = top + r;
            int yy = y + 2 + r;
            if (idx >= (int)lines.size()) {
                if (lines.empty() && r == 0)
                    scr.putStr(x + 2, yy, padDisplay("(空, 按 A 添加一行)", w - 4),
                               A(Color::BrightBlack, th.dlgText.bg));
                else scr.fillRect(x + 2, yy, w - 4, 1, " ");
                continue;
            }
            char num[16]; snprintf(num, sizeof(num), "%2d: ", idx + 1);
            std::string t = padDisplay(std::string(num) + truncateDisplay(lines[idx], w - 8), w - 4);
            if (idx == sel) scr.putStr(x + 2, yy, t, th.dlgSel);
            else scr.putStr(x + 2, yy, t, th.dlgText);
        }
        scr.putStr(x + 2, y + h - 3,
                   padDisplay(truncateDisplay("Enter/E编辑 │ A添加 │ D删除 │ F2保存 │ Esc取消 │ 单击选/双击改", w - 4), w - 4),
                   th.dlgHint);
        con_.present();
        Key k = con_.readKey();
        if (k.mouse) {
            if (k.mwheel > 0) { if (sel > 0) sel--; }
            else if (k.mwheel < 0) { if (sel + 1 < (int)lines.size()) sel++; }
            else if (k.mpress && k.mbutton == 1) {
                bool dbl = isDoubleClick(k);
                int r = k.my - (y + 2);
                if (r >= 0 && r < perPage) {
                    int idx = top + r;
                    if (idx >= 0 && idx < (int)lines.size()) {
                        sel = idx;
                        if (dbl) {
                            std::string out = lines[sel];
                            if (dialogInput("编辑行", "行内容:", out, lines[sel])) lines[sel] = out;
                        }
                    }
                }
            }
            continue;
        }
        if (k.type == Key::Up) { if (sel > 0) sel--; }
        else if (k.type == Key::Down) { if (sel + 1 < (int)lines.size()) sel++; }
        else if (k.type == Key::Esc) return false;
        else if (k.type == Key::F2) return true;
        else if (k.type == Key::Enter) {
            if (lines.empty()) {
                std::string out;
                if (dialogInput("添加行", "新行内容:", out)) lines.push_back(out);
            } else {
                std::string out = lines[sel];
                if (dialogInput("编辑行", "行内容:", out, lines[sel])) lines[sel] = out;
            }
        }
        else if (k.type == Key::Delete) { if (!lines.empty()) lines.erase(lines.begin() + sel); }
        else if (k.type == Key::Char && !k.ctrl) {
            if (k.ch == 'e' || k.ch == 'E') {
                if (!lines.empty()) {
                    std::string out = lines[sel];
                    if (dialogInput("编辑行", "行内容:", out, lines[sel])) lines[sel] = out;
                }
            }
            else if (k.ch == 'a' || k.ch == 'A') {
                std::string out;
                if (dialogInput("添加行", "新行内容:", out)) {
                    lines.insert(lines.begin() + sel + (lines.empty() ? 0 : 1), out);
                    if (!lines.empty()) sel++;
                }
            }
            else if (k.ch == 'd' || k.ch == 'D') { if (!lines.empty()) lines.erase(lines.begin() + sel); }
            else if (k.ch == 'q' || k.ch == 'Q') return false;
        }
        else if (k.type == Key::Char && k.isCtrl('s')) return true;
    }
}

struct PaletteCmd {
    std::string name;
    std::string keys;
    std::function<void()> run;
};

void TuiRegedit::commandPalette() {
    std::vector<PaletteCmd> all = {
        {"转到路径", "Ctrl+L", [&]{ actionGoto(); }},
        {"查找", "F", [&]{ actionSearch(); }},
        {"新建项", "N", [&]{ actionNewKey(); }},
        {"新建值", "N", [&]{ actionNewValue(); }},
        {"编辑值", "E", [&]{ actionEditValue(); }},
        {"删除所选项", "D", [&]{ actionDelete(); }},
        {"重命名", "R", [&]{ actionRename(); }},
        {"导出当前项", "Ctrl+E", [&]{ actionExport(); }},
        {"导入 .reg", "Ctrl+I", [&]{ actionImport(); }},
        {"刷新", "F5", [&]{ actionRefresh(); }},
        {"切换配色主题", "F10", [&]{ actionTheme(); }},
        {"切换面板", "Tab", [&]{
            activePane_ = 1 - activePane_;
            setStatus(activePane_ == 0 ? "已切换到: 注册表项(树)" : "已切换到: 值列表");
        }},
        {"展开/收起当前项", "→/←", [&]{ if (selectedNode()) toggleExpand(selectedNode()); }},
        {"帮助", "F1", [&]{ dialogHelp(); }},
        {"关于", "F9", [&]{ dialogAbout(); }},
        {"退出", "Q", [&]{ running_ = false; }},
    };
    Size s = con_.getSize();
    Screen& scr = con_.screen();
    const Theme& th = theme();
    int w = std::min(s.w - 6, 64);
    int listH = std::min(10, s.h - 12);
    if (listH < 4) listH = 4;
    int h = listH + 7;
    int x = (s.w - w) / 2;
    int y = std::max(2, (s.h - h) / 2 - 2);  // 靠上, 类 VSCode
    std::string filter;
    size_t cursor = 0;
    int sel = 0, top = 0;
    int fieldW = w - 8;
    if (fieldW < 10) fieldW = 10;
    for (;;) {
        std::vector<int> hit;
        for (size_t i = 0; i < all.size(); i++)
            if (fuzzyMatchCmd(filter, all[i].name)) hit.push_back((int)i);
        sel = hit.empty() ? 0 : clampInt(sel, 0, (int)hit.size() - 1);
        ensureVisible(sel, top, listH);
        draw();
        char title[64];
        snprintf(title, sizeof(title), "命令面板 (%d/%d)", (int)hit.size(), (int)all.size());
        drawBox(scr, x, y, w, h, title, true);
        size_t left = 0;
        while (left < cursor && utf8DisplayWidth(filter.substr(left, cursor - left)) >= fieldW)
            left += utf8NextCharLen(filter, left);
        std::string vis = filter.substr(left);
        std::string shown;
        {
            int dw = 0;
            for (size_t i = 0; i < vis.size();) {
                size_t len = 1;
                uint32_t cp = utf8DecodeOne(vis.c_str() + i, vis.size() - i, len);
                int cw = isWideCodepoint(cp) ? 2 : 1;
                if (dw + cw > fieldW) break;
                shown.append(vis.substr(i, len));
                dw += cw; i += len;
            }
        }
        Attr promptA = th.borderOn; promptA.bg = th.dlgText.bg;
        scr.putStr(x + 2, y + 2, "> ", promptA);
        scr.putStr(x + 4, y + 2, padDisplay(shown, fieldW), th.dlgText);
        Attr sepA = th.track; sepA.bg = th.dlgText.bg;
        scr.putStr(x + 2, y + 3, rep("─", w - 4), sepA);
        for (int r = 0; r < listH; r++) {
            int idx = top + r;
            int yy = y + 4 + r;
            if (idx >= (int)hit.size()) {
                if (hit.empty() && r == 0)
                    scr.putStr(x + 2, yy, padDisplay("无匹配命令", w - 4),
                               A(Color::BrightBlack, th.dlgText.bg));
                else scr.fillRect(x + 2, yy, w - 4, 1, " ");
                continue;
            }
            const auto& c = all[hit[idx]];
            std::string t = truncateDisplay(c.name + "  (" + c.keys + ")", w - 6);
            if (idx == sel)
                scr.putStr(x + 2, yy, padDisplay("▸ " + t, w - 4), th.dlgSel);
            else
                scr.putStr(x + 2, yy, padDisplay("  " + t, w - 4), th.dlgText);
        }
        scr.putStr(x + 2, y + h - 2, padDisplay(truncateDisplay("↑↓选择 │ Enter执行 │ Esc关闭 │ 输入过滤", w - 4), w - 4),
                   th.dlgHint);
        con_.present();
        int cxx = (int)utf8DisplayWidth(filter.substr(left, cursor - left));
        con_.moveCursor(x + 4 + cxx, y + 2);
        con_.showCursor();
        con_.flushOut();

        Key k = con_.readKey();
        con_.hideCursor();
        if (k.mouse) {
            if (k.mwheel > 0) { if (sel > 0) sel--; }
            else if (k.mwheel < 0) { if (sel + 1 < (int)hit.size()) sel++; }
            else if (k.mpress && k.mbutton == 1) {
                bool dbl = isDoubleClick(k);
                if (k.my == y + 2 && k.mx >= x + 4 && k.mx < x + 4 + fieldW) {
                    cursor = cursorFromColumn(filter, left, k.mx - (x + 4));
                } else {
                    int r = k.my - (y + 4);
                    if (r >= 0 && r < listH) {
                        int idx = top + r;
                        if (idx >= 0 && idx < (int)hit.size()) {
                            sel = idx;
                            if (dbl) { auto fn = all[hit[idx]].run; fn(); return; }
                        }
                    }
                }
            }
            continue;
        }
        if (k.type == Key::Enter) {
            if (!hit.empty()) { auto fn = all[hit[sel]].run; fn(); }
            return;
        }
        if (k.type == Key::Esc) return;
        if (k.type == Key::Char && k.isCtrl('c')) return;
        if (k.type == Key::Up) { if (sel > 0) sel--; }
        else if (k.type == Key::Down) { if (sel + 1 < (int)hit.size()) sel++; }
        else if (k.type == Key::Home) sel = 0;
        else if (k.type == Key::End) sel = hit.empty() ? 0 : (int)hit.size() - 1;
        else if (k.type == Key::PgUp) sel = hit.empty() ? 0 : clampInt(sel - listH, 0, (int)hit.size() - 1);
        else if (k.type == Key::PgDn) sel = hit.empty() ? 0 : clampInt(sel + listH, 0, (int)hit.size() - 1);
        else if (k.type == Key::Left) { if (cursor > 0) cursor -= utf8PrevCharLen(filter, cursor); }
        else if (k.type == Key::Right) { if (cursor < filter.size()) cursor += utf8NextCharLen(filter, cursor); }
        else if (k.type == Key::Backspace) {
            if (cursor > 0) {
                size_t l = utf8PrevCharLen(filter, cursor);
                filter.erase(cursor - l, l); cursor -= l; sel = 0; top = 0;
            }
        }
        else if (k.type == Key::Delete) {
            if (cursor < filter.size()) {
                filter.erase(cursor, utf8NextCharLen(filter, cursor)); sel = 0; top = 0;
            }
        }
        else if (k.type == Key::Char && k.isCtrl('u')) { filter.clear(); cursor = 0; sel = 0; top = 0; }
        else if (k.type == Key::Char && !k.ctrl && !k.alt && k.ch >= 0x20) {
            std::string ins = encodeUtf8(k.ch);
            filter.insert(cursor, ins); cursor += ins.size(); sel = 0; top = 0;
        }
    }
}

// ---------------- 动作 ----------------
void TuiRegedit::actionEditValue() {
    if (values_.empty()) { setStatus("当前项没有可编辑的值。"); return; }
    if (valSel_ < 0 || valSel_ >= (int)values_.size()) return;
    RegValue v = values_[valSel_];
    std::string err;
    bool changed = false;
    if (v.type == REG_DWORD_T) {
        uint32_t n = v.getDword();
        if (!dialogEditDword("编辑 DWORD: " + v.displayName(), n)) { setStatus("已取消编辑。"); return; }
        v.setDword(n); changed = true;
    } else if (v.type == REG_QWORD_T) {
        uint64_t n = v.getQword();
        if (!dialogEditQword("编辑 QWORD: " + v.displayName(), n)) { setStatus("已取消编辑。"); return; }
        v.setQword(n); changed = true;
    } else if (v.type == REG_BINARY_T || v.type == REG_NONE_T) {
        if (!dialogEditBinary("编辑二进制: " + v.displayName(), v.data)) { setStatus("已取消编辑。"); return; }
        changed = true;
    } else if (v.type == REG_MULTI_SZ_T) {
        auto lines = v.getMultiStrings();
        if (!dialogEditMulti("编辑多字符串: " + v.displayName(), lines)) { setStatus("已取消编辑。"); return; }
        v.setMultiStrings(lines); changed = true;
    } else {
        std::string out;
        std::string t = (v.type == REG_EXPAND_SZ_T ? "编辑可扩充字符串: " : "编辑字符串: ") + v.displayName();
        if (!dialogInput(t, "数值数据:", out, v.getString())) { setStatus("已取消编辑。"); return; }
        v.setString(out); changed = true;
    }
    if (!changed) return;
    if (reg_->setValue(currentPath_, v, err)) {
        setStatus("已保存: " + v.displayName());
        std::string keepName = v.name;
        refreshValues();
        for (size_t i = 0; i < values_.size(); i++)
            if (values_[i].name == keepName) { valSel_ = (int)i; break; }
    } else {
        dialogMsg("保存失败", err);
        setStatus("保存失败: " + err);
    }
}

void TuiRegedit::actionNewKey() {
    TreeNode* n = selectedNode();
    if (!n) return;
    std::string name;
    if (!dialogInput("新建项", "在 [" + truncateDisplay(n->fullPath, 55) + "] 下新建子项, 请输入名称:", name,
                     "", "名称不能包含反斜杠 \\")) { setStatus("已取消新建。"); return; }
    name = trimStr(name);
    if (name.empty()) { dialogMsg("无效名称", "项名称不能为空。"); return; }
    if (name.find('\\') != std::string::npos) { dialogMsg("无效名称", "项名称不能包含 \\。"); return; }
    std::string err;
    std::string np = joinPath(n->fullPath, name);
    if (!reg_->createKey(np, err)) { dialogMsg("新建失败", err); setStatus("新建失败: " + err); return; }
    loadChildren(n, true);
    n->expanded = true;
    rebuildVisible();
    ensurePathVisible(np);
    setStatus("已新建项: " + np);
}

void TuiRegedit::actionNewValue() {
    if (currentPath_.empty()) return;
    int t = dialogMenu("新建值 - 选择类型", {
        "字符串值 (REG_SZ)",
        "可扩充字符串值 (REG_EXPAND_SZ)",
        "二进制值 (REG_BINARY)",
        "DWORD (32 位)值",
        "QWORD (64 位)值",
        "多字符串值 (REG_MULTI_SZ)",
    }, "在 [" + truncateDisplay(currentPath_, 50) + "] 下新建值");
    if (t < 0) { setStatus("已取消新建。"); return; }
    uint32_t types[] = {REG_SZ_T, REG_EXPAND_SZ_T, REG_BINARY_T, REG_DWORD_T, REG_QWORD_T, REG_MULTI_SZ_T};
    RegValue v;
    v.type = types[t];
    if (v.type == REG_DWORD_T) v.setDword(0);
    else if (v.type == REG_QWORD_T) v.setQword(0);
    std::string name;
    if (!dialogInput("新建值", "请输入值名称:", name, "", "名称不能为空 (空名称保留给“(默认)”)")) {
        setStatus("已取消新建。"); return;
    }
    name = trimStr(name);
    if (name.empty()) { dialogMsg("无效名称", "新建的值名称不能为空。\n“(默认)”值已存在, 请直接编辑它。"); return; }
    for (auto& e : values_) if (e.name == name) { dialogMsg("新建失败", "同名值已存在。"); return; }
    v.name = name;
    if (v.type == REG_SZ_T || v.type == REG_EXPAND_SZ_T) {
        std::string out;
        if (dialogInput("新建值 - 数据", "数值数据:", out)) v.setString(out);
    } else if (v.type == REG_DWORD_T) {
        uint32_t n = 0;
        if (dialogEditDword("新建 DWORD - 数据", n)) v.setDword(n);
    } else if (v.type == REG_QWORD_T) {
        uint64_t n = 0;
        if (dialogEditQword("新建 QWORD - 数据", n)) v.setQword(n);
    } else if (v.type == REG_BINARY_T) {
        dialogEditBinary("新建二进制 - 数据", v.data);
    } else if (v.type == REG_MULTI_SZ_T) {
        std::vector<std::string> lines;
        if (dialogEditMulti("新建多字符串 - 数据", lines)) v.setMultiStrings(lines);
    }
    std::string err;
    if (!reg_->setValue(currentPath_, v, err)) { dialogMsg("新建失败", err); setStatus("新建失败: " + err); return; }
    refreshValues();
    for (size_t i = 0; i < values_.size(); i++)
        if (values_[i].name == name) { valSel_ = (int)i; break; }
    activePane_ = 1;
    setStatus("已新建值: " + name);
}

void TuiRegedit::actionDelete() {
    std::string err;
    if (activePane_ == 0) {
        TreeNode* n = selectedNode();
        if (!n) return;
        if (isRootPath(n->fullPath)) { dialogMsg("无法删除", "不能删除根项。"); return; }
        std::string doomed = n->fullPath;
        TreeNode* parentNode = n->parent;
        std::string msg = "即将删除项 (含其下所有子项与值!):\n" + doomed + "\n\n此操作不可撤销, 建议先导出备份。";
        if (!dialogConfirm("确认删除项", msg)) { setStatus("已取消删除。"); return; }
        std::string parent = parentPath(doomed);
        if (!reg_->deleteKeyTree(doomed, err)) { dialogMsg("删除失败", err); setStatus("删除失败: " + err); return; }
        if (parentNode) loadChildren(parentNode, true);
        rebuildVisible();
        ensurePathVisible(parent);
        setStatus("已删除项: " + doomed);
    } else {
        if (values_.empty()) { setStatus("没有可删除的值。"); return; }
        const RegValue& v = values_[valSel_];
        if (v.name.empty()) { dialogMsg("无法删除", "不能删除“(默认)”值, 只能编辑清空它的内容。"); return; }
        std::string msg = "即将删除值:\n项: " + currentPath_ + "\n值: " + v.displayName() + " [" + v.typeName() + "]";
        if (!dialogConfirm("确认删除值", msg)) { setStatus("已取消删除。"); return; }
        if (!reg_->deleteValue(currentPath_, v.name, err)) { dialogMsg("删除失败", err); setStatus("删除失败: " + err); return; }
        refreshValues();
        setStatus("已删除值: " + v.displayName());
    }
}

void TuiRegedit::actionRename() {
    std::string err;
    if (activePane_ == 0) {
        TreeNode* n = selectedNode();
        if (!n) return;
        if (isRootPath(n->fullPath)) { dialogMsg("无法重命名", "不能重命名根项。"); return; }
        std::string out;
        if (!dialogInput("重命名项", "将 [" + n->name + "] 重命名为:", out, n->name)) { setStatus("已取消重命名。"); return; }
        out = trimStr(out);
        if (out.empty() || out.find('\\') != std::string::npos) { dialogMsg("无效名称", "名称不能为空且不能包含 \\。"); return; }
        if (out == n->name) { setStatus("名称未变。"); return; }
        if (!reg_->renameKey(n->fullPath, out, err)) { dialogMsg("重命名失败", err); setStatus("重命名失败: " + err); return; }
        std::string np = joinPath(parentPath(n->fullPath), out);
        TreeNode* p = n->parent;
        if (p) loadChildren(p, true);
        rebuildVisible();
        ensurePathVisible(np);
        setStatus("已重命名为: " + out);
    } else {
        if (values_.empty()) return;
        const RegValue& v = values_[valSel_];
        if (v.name.empty()) { dialogMsg("无法重命名", "不能重命名“(默认)”值。"); return; }
        std::string out;
        if (!dialogInput("重命名值", "将 [" + v.displayName() + "] 重命名为:", out, v.name)) { setStatus("已取消重命名。"); return; }
        out = trimStr(out);
        if (out.empty()) { dialogMsg("无效名称", "新名称不能为空。"); return; }
        if (out == v.name) { setStatus("名称未变。"); return; }
        if (!reg_->renameValue(currentPath_, v.name, out, err)) { dialogMsg("重命名失败", err); setStatus("重命名失败: " + err); return; }
        refreshValues();
        for (size_t i = 0; i < values_.size(); i++)
            if (values_[i].name == out) { valSel_ = (int)i; break; }
        setStatus("已重命名为: " + out);
    }
}

void TuiRegedit::actionSearch() {
    std::string keyword;
    if (!dialogInput("查找", "请输入关键字 (匹配项名 / 值名 / 字符串数据):", keyword, "",
                     "不区分大小写, 支持中文")) { setStatus("已取消查找。"); return; }
    keyword = trimStr(keyword);
    if (keyword.empty()) { setStatus("关键字为空。"); return; }
    int scope = dialogMenu("查找范围", {"从当前项开始: " + truncateDisplay(currentPath_, 40), "从整机根开始 (较慢)"});
    if (scope < 0) { setStatus("已取消查找。"); return; }
    std::vector<std::string> starts;
    if (scope == 0) starts.push_back(currentPath_);
    else starts = reg_->listRoots();

    setStatus("正在搜索 [" + keyword + "] ...");
    draw();
    con_.present();

    struct Hit { std::string path; std::string desc; };
    std::vector<Hit> hits;
    size_t visited = 0;
    const size_t kMaxVisit = 60000, kMaxHits = 300;
    bool truncated = false;
    std::vector<std::string> stack = starts;
    while (!stack.empty() && hits.size() < kMaxHits && visited < kMaxVisit) {
        std::string p = stack.back(); stack.pop_back();
        visited++;
        if (containsIgnoreCase(baseName(p), keyword)) {
            hits.push_back({p, "项名匹配"});
            if (hits.size() >= kMaxHits) { truncated = true; break; }
        }
        auto vals = reg_->listValues(p);
        for (auto& v : vals) {
            bool hm = containsIgnoreCase(v.displayName(), keyword);
            std::string desc;
            if (hm) desc = "值名: " + v.displayName();
            else if ((v.type == REG_SZ_T || v.type == REG_EXPAND_SZ_T || v.type == REG_MULTI_SZ_T) &&
                     containsIgnoreCase(v.getString(), keyword)) {
                desc = "数据: " + truncateDisplay(v.prettyData(40), 40);
            }
            if (!desc.empty()) {
                hits.push_back({p, v.displayName() + " (" + desc + ")"});
                if (hits.size() >= kMaxHits) { truncated = true; break; }
            }
        }
        if (truncated) break;
        auto subs = reg_->listSubkeys(p);
        for (auto it = subs.rbegin(); it != subs.rend(); ++it)
            stack.push_back(joinPath(p, *it));
    }
    if (visited >= kMaxVisit) truncated = true;

    if (hits.empty()) {
        dialogMsg("查找结果", "未找到包含 [" + keyword + "] 的项或值。\n(已搜索 " + std::to_string(visited) + " 个项)");
        setStatus("查找 [" + keyword + "]: 未找到。");
        return;
    }
    std::vector<std::string> opts;
    for (auto& h : hits) opts.push_back(truncateDisplay(h.path, 60) + "  — " + truncateDisplay(h.desc, 30));
    std::string hint = "共 " + std::to_string(hits.size()) + " 个结果 (搜索 " +
        std::to_string(visited) + " 项" + (truncated ? ", 结果已截断" : "") + "), 回车跳转";
    int idx = dialogMenu("查找结果: " + keyword, opts, hint);
    if (idx < 0) { setStatus("已关闭查找结果。"); return; }
    if (ensurePathVisible(hits[idx].path)) {
        activePane_ = 0;
        setStatus("已跳转到: " + hits[idx].path);
    } else {
        dialogMsg("跳转失败", "无法定位到: " + hits[idx].path);
    }
}

void TuiRegedit::actionExport() {
    if (currentPath_.empty()) return;
    std::string def = baseName(currentPath_.empty() ? "regedit" : currentPath_) + ".reg";
    std::string fp;
    if (!dialogInput("导出 .reg", "将 [" + truncateDisplay(currentPath_, 50) + "] 导出到文件:", fp, def,
                     "Windows 下调用 reg export; Mock 下生成标准 .reg 文本")) {
        setStatus("已取消导出。"); return;
    }
    fp = trimStr(fp);
    if (fp.empty()) { dialogMsg("导出失败", "文件名不能为空。"); return; }
    std::string err;
    if (reg_->exportReg(currentPath_, fp, err)) {
        dialogMsg("导出成功", "已导出到:\n" + fp);
        setStatus("已导出到: " + fp);
    } else {
        dialogMsg("导出失败", err);
        setStatus("导出失败: " + err);
    }
}

void TuiRegedit::actionImport() {
#ifdef _WIN32
    std::string fp;
    if (!dialogInput("导入 .reg", "请输入要导入的 .reg 文件路径:", fp, "",
                     "将调用系统命令: reg import \"文件\"")) { setStatus("已取消导入。"); return; }
    fp = trimStr(fp);
    if (fp.empty()) { setStatus("文件名为空。"); return; }
    if (fp.size() >= 2 && fp.front() == '"' && fp.back() == '"') fp = fp.substr(1, fp.size() - 2);
    std::string cmd = "reg import \"" + fp + "\"";
    setStatus("正在导入, 请稍候...");
    draw(); con_.present();
    int rc = _wsystem(utf8ToWide(cmd).c_str());
    if (rc == 0) {
        actionRefresh();
        dialogMsg("导入成功", "已导入:\n" + fp);
        setStatus("已导入: " + fp);
    } else {
        dialogMsg("导入失败", "reg import 退出码: " + std::to_string(rc) + "\n请检查文件路径与管理员权限。");
        setStatus("导入失败。");
    }
#else
    dialogMsg("导入", "Mock 演示模式不支持导入。\n在 Windows 真机上可用 Ctrl+I 导入 .reg 文件。");
    setStatus("Mock 模式不支持导入。");
#endif
}

void TuiRegedit::actionRefresh() {
    TreeNode* n = selectedNode();
    if (n) loadChildren(n, true);
    std::string keep = currentPath_;
    rebuildVisible();
    for (size_t i = 0; i < visible_.size(); i++)
        if (visible_[i]->fullPath == keep) { treeSel_ = (int)i; break; }
    refreshValues();
    setStatus("已刷新: " + keep);
}

void TuiRegedit::actionGoto() {
    std::string input;
    if (!dialogInput("转到注册表项", "输入完整路径:", input, currentPath_,
                     "支持缩写 HKCR/HKCU/HKLM/HKU/HKCC, 也可用 / 代替 \\")) {
        setStatus("已取消转到。"); return;
    }
    std::string p = normalizeRegPath(input);
    if (p.empty()) {
        dialogMsg("路径无效", "根项无法识别。\n示例: HKCU\\Software\\Microsoft");
        setStatus("路径无效。");
        return;
    }
    if (reg_->exists(p)) {
        ensurePathVisible(p);
        activePane_ = 0;
        setStatus("已转到: " + p);
        return;
    }
    std::string q = p;
    while (!q.empty() && !reg_->exists(q)) q = parentPath(q);
    if (!q.empty()) ensurePathVisible(q);
    activePane_ = 0;
    dialogMsg("项不存在", "以下项不存在:\n" + p + "\n\n已定位到最近的已存在父项:\n" + (q.empty() ? "(无)" : q));
    setStatus("目标不存在, 已定位到: " + q);
}

void TuiRegedit::actionTheme() {
    std::vector<std::string> opts;
    for (size_t i = 0; i < allThemes().size(); i++)
        opts.push_back(allThemes()[i].name + (i == (size_t)themeIndex_ ? "  (当前)" : ""));
    int c = dialogMenu("配色主题", opts, "选择后立即生效并保存偏好");
    if (c < 0) { setStatus("已取消。"); return; }
    themeIndex_ = c;
    saveTheme();
    setStatus("已切换配色: " + allThemes()[(size_t)themeIndex_].name);
}

void TuiRegedit::contextMenu() {
    if (activePane_ == 0) {
        int c = dialogMenu("操作", {
            "新建项 (N)", "重命名项 (R)", "删除项 (D)",
            "导出当前项 (Ctrl+E)", "转到路径 (Ctrl+L)", "刷新 (F5)",
        });
        if (c == 0) actionNewKey();
        else if (c == 1) actionRename();
        else if (c == 2) actionDelete();
        else if (c == 3) actionExport();
        else if (c == 4) actionGoto();
        else if (c == 5) actionRefresh();
    } else {
        int c = dialogMenu("操作", {
            "编辑值 (E)", "新建值 (N)", "重命名 (R)",
            "删除值 (D)", "转到路径 (Ctrl+L)", "刷新 (F5)",
        });
        if (c == 0) actionEditValue();
        else if (c == 1) actionNewValue();
        else if (c == 2) actionRename();
        else if (c == 3) actionDelete();
        else if (c == 4) actionGoto();
        else if (c == 5) actionRefresh();
    }
}
