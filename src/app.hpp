// app.hpp - TUI Regedit 主应用
#pragma once
#include "registry.hpp"
#include "tui.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

// 配色主题
struct Theme {
    std::string id;    // 配置文件用
    std::string name;  // 显示名
    Attr header, headerPath, status;
    Attr borderOn, borderOff, titleOn, titleOff;
    Attr selOn, selOff, root;
    Attr thumbOn, thumbOff, track;
    Attr dlgText, dlgHint, dlgSel;
};

class TuiRegedit {
public:
    explicit TuiRegedit(std::unique_ptr<IRegistry> reg);
    void run();

private:
    struct TreeNode {
        std::string name;
        std::string fullPath;
        int depth = 0;
        bool expanded = false;
        bool loaded = false;
        bool hasChildren = true; // 懒加载前假设有
        TreeNode* parent = nullptr;
        std::vector<std::unique_ptr<TreeNode>> children;
    };

    // 主界面布局 (绘制与鼠标命中共用)
    struct Layout {
        int W = 80, H = 24;
        int treeX = 0, treeY = 1, treeW = 40, treeH = 20;
        int valX = 40, valY = 1, valW = 40, valH = 20;
        int treeListY = 2, treeRows = 18;  // 树列表区起始行与行数
        int valListY = 4, valRows = 16;    // 值列表区起始行与行数
    };

    std::unique_ptr<IRegistry> reg_;
    Console& con_;

    std::vector<std::unique_ptr<TreeNode>> roots_;
    std::vector<TreeNode*> visible_;
    int treeSel_ = 0, treeTop_ = 0;

    std::vector<RegValue> values_;
    int valSel_ = 0, valTop_ = 0;
    int activePane_ = 0; // 0=树 1=值
    bool running_ = true;
    std::string statusMsg_;
    std::string currentPath_;
    int themeIndex_ = 0;

    // 鼠标双击检测状态
    std::chrono::steady_clock::time_point lastMouseTime_{};
    int lastMouseX_ = -1, lastMouseY_ = -1, lastMouseBtn_ = 0;
    // 滚动条拖动状态
    bool sbDrag_ = false;
    int sbDragPane_ = -1;  // 0=树 1=值

    // ---- 主题 ----
    static const std::vector<Theme>& allThemes();
    const Theme& theme() const { return allThemes()[(size_t)themeIndex_]; }
    void loadTheme();
    void saveTheme();

    // ---- 树操作 ----
    void initRoots();
    void rebuildVisible();
    void addVisible(TreeNode* n);
    void loadChildren(TreeNode* n, bool force = false);
    void refreshValues();
    TreeNode* selectedNode();
    void ensureVisible(int sel, int& top, int page);
    bool ensurePathVisible(const std::string& fullPath);
    void toggleExpand(TreeNode* n);

    // ---- 布局与鼠标命中 ----
    Layout calcLayout(const Size& s);
    int treeIndexAt(const Layout& L, int my);  // 行号 -> visible_ 下标, 无效 -1
    int valIndexAt(const Layout& L, int my);   // 行号 -> values_ 下标, 无效 -1
    bool isDoubleClick(const Key& k);
    void handleMouse(const Key& k);
    void scrollbarJump(int pane, int my);

    // ---- 绘制 ----
    void draw();
    void drawHeader(Screen& scr, const Layout& L);
    void drawTreePane(Screen& scr, const Layout& L);
    void drawValuePane(Screen& scr, const Layout& L);
    void drawStatusBar(Screen& scr, const Layout& L);
    void drawBox(Screen& scr, int x, int y, int w, int h,
                 const std::string& title, bool active);

    // ---- 主循环 ----
    void handleKey(const Key& k);
    void handleTreeKey(const Key& k);
    void handleValueKey(const Key& k);

    // ---- 对话框 (返回 true=确认) ----
    void dialogMsg(const std::string& title, const std::string& msg);
    bool dialogConfirm(const std::string& title, const std::string& msg);
    bool dialogInput(const std::string& title, const std::string& prompt,
                     std::string& out, const std::string& initial = "",
                     const std::string& hint = "");
    int dialogMenu(const std::string& title, const std::vector<std::string>& options,
                   const std::string& hint = "");
    void dialogHelp();
    void dialogAbout();
    bool dialogEditDword(const std::string& title, uint32_t& v);
    bool dialogEditQword(const std::string& title, uint64_t& v);
    bool dialogEditBinary(const std::string& title, std::vector<uint8_t>& data);
    bool dialogEditMulti(const std::string& title, std::vector<std::string>& lines);
    void commandPalette();

    // ---- 动作 ----
    void actionEditValue();
    void actionNewKey();
    void actionNewValue();
    void actionDelete();
    void actionRename();
    void actionSearch();
    void actionExport();
    void actionImport();
    void actionRefresh();
    void actionGoto();
    void actionTheme();
    void contextMenu();
    void setStatus(const std::string& s) { statusMsg_ = s; }
};
