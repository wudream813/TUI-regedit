// main.cpp - TUI Regedit 入口
// Windows: 默认操作真实注册表; 加 --mock 可用演示数据试玩
// Linux/macOS: 只能用 --mock 演示数据 (无真实注册表)
#include "app.hpp"
#include "registry.hpp"
#include <iostream>
#include <string>

static void printHelp(const char* prog) {
    std::cout <<
        "TUI Regedit - 终端里的 Windows 注册表编辑器\n"
        "\n"
        "用法:\n"
        "  " << prog << " [选项]\n"
        "\n"
        "选项:\n"
        "  --mock        使用内存模拟数据 (试玩 / 演示, 不触碰真实注册表)\n"
        "  -h, --help    显示本帮助\n"
        "  -v, --version 显示版本\n"
        "\n"
#ifdef _WIN32
        "说明:\n"
        "  默认直接编辑真实注册表。修改 HKLM 等系统项请右键“以管理员身份运行”。\n"
        "  删除项会连同子项一起删除且不可撤销, 建议先用 Ctrl+E 导出备份。\n"
#else
        "说明:\n"
        "  当前系统没有 Windows 注册表, 程序将使用内置演示数据运行。\n"
        "  完整功能请在 Windows 上编译运行。\n"
#endif
        ;
}

int main(int argc, char* argv[]) {
    bool useMock = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--mock") useMock = true;
        else if (a == "-h" || a == "--help") { printHelp(argv[0]); return 0; }
        else if (a == "-v" || a == "--version") { std::cout << "TUI Regedit 1.2.0\n"; return 0; }
        else { std::cerr << "未知参数: " << a << "\n用 --help 查看用法。\n"; return 1; }
    }

#ifndef _WIN32
    if (!useMock) {
        std::cout << "提示: 非 Windows 系统, 自动使用演示数据 (--mock)。\n"
                     "按回车继续..." << std::flush;
        std::string dummy;
        std::getline(std::cin, dummy);
    }
    useMock = true;
#endif

    std::unique_ptr<IRegistry> reg;
    if (useMock) reg = createMockRegistry();
    else reg = createRegistry();

    try {
        TuiRegedit app(std::move(reg));
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
