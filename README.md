# TUI Regedit

用纯 C++17 编写的终端版 Windows 注册表编辑器 —— **零第三方依赖**，只用标准库 + Win32 API。

左树右值、鼠标操作、路径跳转、对话框编辑、新建/删除/重命名、查找、导入导出 `.reg`，
`regedit` 的常用操作在终端里全都能做。

```
 TUI Regedit — HKEY_CURRENT_USER\Software\TuiRegedit
┌ 注册表项 (5/9) ─────────────┐┌ 值 (3/8) ─────────────────────────────────┐
│ [+] HKEY_CLASSES_ROOT       ││ 名称          │ 类型       │ 数据           │
│ [-] HKEY_CURRENT_USER       ││ ──────────────┼────────────┼──────────────  │
│   [-] Software              ││ (默认)        │ REG_SZ     │ "默认示例值"   │
│     [-] Microsoft           ││ BigNumber     │ REG_QWORD  │ 0x00007048...  │
│     [+] TuiRegedit          ││ Enabled       │ REG_DWORD  │ 0x00000001 (1) │
│ [+] HKEY_LOCAL_MACHINE      ││ UserName      │ REG_SZ     │ "张三"         │
│ [+] HKEY_USERS              ││ ...                                        │
│ [+] HKEY_CURRENT_CONFIG     ││                                            │
└─────────────────────────────┘└────────────────────────────────────────────┘
 就绪。鼠标: 单击选择/双击打开/右键菜单/滚轮滚动。      Windows Registry (真实)
 Tab切换 │ ↑↓移动 │ E编辑 │ N新建 │ D删除 │ F查找 │ Ctrl+L跳转 │ ?帮助 │ Q退出
```

## 功能

| 功能 | 说明 |
|---|---|
| 浏览 | 五大根项懒加载树 + 值列表（名称 / 类型 / 数据） |
| 鼠标 | 单击选择 & 切换面板、单击 `[+]`/`[-]` 展开收起、双击打开、右键菜单、滚轮滚动 |
| 转到路径 | `Ctrl+L` 直接输入路径跳转，支持 `HKCU\Software\...` 缩写与 `/` 分隔符 |
| 编辑值 | `REG_SZ` / `EXPAND_SZ` / `DWORD` / `QWORD` / `BINARY` / `MULTI_SZ` 专用编辑器 |
| 新建 | 新建项、新建 6 种类型的值 |
| 删除 / 重命名 | 项（含整棵子树，二次确认）、值；项重命名用 `RegRenameKey` |
| 查找 | 按项名 / 值名 / 字符串数据搜索，可从当前项或整机搜，结果列表一键跳转 |
| 导出 / 导入 | `Ctrl+E` 导出 `.reg`（调用 `reg export`），`Ctrl+I` 导入（调用 `reg import`） |
| 中文支持 | 全 UTF-8，CJK 双宽字符对齐，注册表 UTF-16 ⇄ UTF-8 自动转换 |
| 高性能渲染 | 双缓冲 + 脏矩形 diff，每帧只输出变化部分，无闪烁 |
| 安全模式 | `--mock` 用内存演示数据试玩，不碰真实注册表 |

## 在 Windows 上构建与运行

把整个 `tui-regedit` 文件夹拷到 Windows 机器上，然后二选一：

**方法 A：双击 `build.bat`**（自动识别 MSVC 或 MinGW，一键生成 `build\tui-regedit.exe`）

**方法 B：手动编译**

```bat
:: MSVC (在 Developer Command Prompt 里)
cl /std:c++17 /EHsc /O2 /Fe:tui-regedit.exe src\main.cpp src\app.cpp src\tui.cpp src\registry_mock.cpp src\registry_win.cpp advapi32.lib /source-charset:utf-8 /execution-charset:utf-8

:: MinGW (MSYS2 / mingw-w64)
g++ -std=c++17 -O2 -Isrc -o tui-regedit.exe src\main.cpp src\app.cpp src\tui.cpp src\registry_mock.cpp src\registry_win.cpp -ladvapi32
```

运行：

```bat
tui-regedit.exe          :: 编辑真实注册表
tui-regedit.exe --mock   :: 演示数据试玩 (安全，推荐先玩这个)
```

> ⚠️ **权限与安全**
> - 修改 `HKEY_LOCAL_MACHINE` 等系统项需要**右键 → 以管理员身份运行**。
> - 删除项会连同子项一起删且**不可撤销**，建议先 `Ctrl+E` 导出备份。
> - 建议先用 `--mock` 熟悉操作。

## 按键一览

| 按键 | 作用 |
|---|---|
| `Tab` / `Shift+Tab` | 树面板 ⇄ 值面板切换 |
| `↑↓` `k j` `Home/End` `PgUp/PgDn` | 移动光标 |
| `→` / `Enter` / `l` | （树）展开项 / 进入首个子项 |
| `←` / `h` / `Backspace` | （树）收起项 / 回到父项 |
| `Enter` / `E` | （值）编辑 |
| `N` | 新建（树=新建项，值=新建值） |
| `D` / `Delete` | 删除（二次确认） |
| `R` / `F2` | 重命名 |
| `F` / `Ctrl+F` / `/` | 查找 |
| `Ctrl+L` / `Ctrl+G` | 转到指定路径 |
| `Ctrl+E` / `Ctrl+I` | 导出 / 导入 `.reg` |
| `F5` | 刷新 |
| `F1` / `?` | 帮助 |
| `Q` / `Esc` / `Ctrl+C` | 退出 |

| 鼠标 | 作用 |
|---|---|
| 单击 | 选择项/值；单击另一面板切换过去；单击 `[+]`/`[-]` 展开收起 |
| 双击 | 展开收起项 / 打开编辑值 |
| 右键 | 上下文菜单（新建/删除/重命名/导出/转到/刷新） |
| 滚轮 | 滚动鼠标所在的面板 |
| 对话框内 | 菜单可单击选择、双击确认；输入框可单击定位光标；帮助可滚轮翻页 |

编辑小抄：`DWORD`/`QWORD` 支持十进制（`255`）与十六进制（`0xFF`）；
二进制用十六进制（`01 02 AB CD`）；多字符串是多行编辑器（`A`加行、`D`删行、`F2`保存）。

## 在 Linux / macOS 上试玩（演示数据）

```sh
make && ./tui-regedit --mock
# 或交叉编译 Windows 版（需 mingw-w64）:
make windows
```

## 工程结构

```
tui-regedit/
├── build.bat            # Windows 一键构建 (MSVC/MinGW)
├── Makefile             # Linux/macOS 构建 + 交叉编译
├── README.md
└── src/
    ├── main.cpp         # 入口与参数 (--mock/--help/--version)
    ├── app.hpp/.cpp     # 主界面: 双面板、对话框、鼠标、跳转、右键菜单
    ├── tui.hpp/.cpp     # 自研跨平台 TUI 底层 (双缓冲渲染 + 键盘/鼠标输入)
    ├── util.hpp         # UTF-8/宽字符、HEX、DWORD 解析、CJK 显示宽度
    ├── registry.hpp     # 注册表抽象接口 IRegistry (UTF-8 约定)
    ├── registry_win.cpp # Windows 真实实现 (Reg* API, 仅 _WIN32 编译)
    └── registry_mock.cpp# 内存模拟实现 (演示 + 单元测试)
```

设计上 `IRegistry` 把界面与数据源解耦：Windows 下默认走真实注册表，
`--mock` 或非 Windows 系统走内存模拟数据，同一套 TUI 代码两边复用。

## License

MIT，可自由使用与修改。
