# TUI Regedit — 终端里的注册表编辑器

> 在终端里, 像 `regedit` 一样浏览 / 查找 / 新增 / 删除 / 改值 / 导出。
> 中文界面, 支持鼠标, 单个 `.exe` 即开即用, 无需安装。

![平台](https://img.shields.io/badge/平台-Windows%20x64-blue)
![语言](https://img.shields.io/badge/语言-C++17-lightgrey)

---

## 下载与运行

1. 打开右侧 [Releases](../../releases), 下载 `tui-regedit-msvc-x64.exe`(推荐, 体积小) 或 `tui-regedit-mingw-x64.exe`。
2. 双击运行即可。改系统项 (如 `HKLM`) 时请**右键 → 以管理员身份运行**。
3. 退出: 按 `Q` 或 `Esc`。

> Windows 首次运行若提示“未知发布者”, 点「仍要运行」即可。

---

## 界面一览

```text
 TUI Regedit — HKEY_CURRENT_USER\Software          ← 顶栏: 当前路径, 单击可跳转
┌─注册表项 (3/120)─────┐┌─值 (2/2)───────────────────┐
│[-] HKEY_CURRENT_USER ││名称      │类型    │数据    │ ← 双面板, 边框高亮=当前面板
│  [+] Software        ││(默认)    │REG_SZ  │        │
│  [+] System          ││Theme     │REG_SZ  │深色    │
└──────────────────────┘└────────────────────────────┘
 就绪。Ctrl+P 命令面板 │ ? 帮助。       Windows 注册表
```

## 快捷键

| 按键 | 功能 |
|---|---|
| `Ctrl+P` | 命令面板: 输入命令名过滤, 回车执行 (新手推荐) |
| `Tab` | 在「注册表项 / 值」面板间切换 |
| `↑↓ ←→` | 移动 / 展开收起 (也可用 `h j k l o`) |
| `Home/End` `PgUp/PgDn` | 跳到头尾 / 翻页 |
| `Enter` | 树=展开·进入子项, 值=编辑 |
| `N` / `D` / `R` | 新建 / 删除 / 重命名 (随当前面板变化) |
| `E` | 编辑值 (也可在树面板按 `E` 跳到值面板) |
| `F` `Ctrl+F` `/` | 查找项名·值名·数据 |
| `Ctrl+L` `Ctrl+G` | 转到路径 (支持 `HKCU\…` 缩写) |
| `Ctrl+E` / `Ctrl+I` | 导出 / 导入 `.reg` |
| `F5` | 刷新 |
| `F9` | 关于本软件 |
| `F10` | 切换配色主题 (5 种, 自动记住选择) |
| `F1` `?` | 帮助 |
| `Q` `Esc` `Ctrl+C` | 退出 |

也支持鼠标: 单击选择 / 双击展开编辑 / 右键菜单 / 滚轮滚动 /
单击 `+` `-` 展开收起 / 单击顶部路径跳转 / 右侧滚动条翻页与拖拽。

## 配色主题

按 `F10` 或在命令面板 (`Ctrl+P`) 输入“配色”, 可在 5 种主题间切换,
选择会自动保存, 下次启动保持:

| 主题 | 说明 |
|---|---|
| 深色 (默认) | 经典蓝底高亮深色风 |
| 浅色 | 白底黑字, 适合明亮环境 |
| 复古绿 | 绿荧光终端风 |
| 海洋 | 青蓝配色 |
| 单色 | 纯黑白, 兼容性最好 |

配置文件位置:

- Windows: `%APPDATA%\TuiRegedit\config.ini`
- Linux/macOS: `~/.config/tui-regedit/config.ini`

## 命令面板

按 `Ctrl+P` 打开, 输入几个字过滤命令, 回车执行。
转到 / 查找 / 新建 / 删除 / 导出 / 切换主题 / 帮助 / 关于……都在里面。

## 值类型支持

| 类型 | 新建 | 编辑 |
|---|:---:|:---:|
| 字符串 `REG_SZ`、可扩充 `REG_EXPAND_SZ` | ✅ | ✅ 文本 |
| `DWORD` / `QWORD` | ✅ | ✅ 十进制或 `0x` 十六进制 |
| 二进制 `REG_BINARY` | ✅ | ✅ 十六进制 (`01 02 AB CD`) |
| 多字符串 `REG_MULTI_SZ` | ✅ | ✅ 逐行编辑器 |

> `REG_NONE` 等未知类型按二进制查看与编辑。

## 注意事项

- 删除项会连同其下所有子项与值一起删除, 且**不可撤销** —— 建议先 `Ctrl+E` 导出备份。
- “(默认)”值不能删除与重命名, 只能编辑内容。
- 改 `HKLM` 等系统项需要管理员权限, 否则会报“拒绝访问”。
- 查找默认搜当前项子树; 选整机搜索会遍历较多项, 耐心等待几秒。

## 从源码构建

```bash
# Linux / macOS (Mock 注册表演示): g++ -std=c++17 src/*.cpp -o tui-regedit
make

# Windows MSVC (x64 Native Tools 命令提示符):
cl /std:c++17 /EHsc /O2 /Fe:tui-regedit.exe src\*.cpp advapi32.lib

# Windows MinGW (交叉编译, 无需 DLL):
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -o tui-regedit.exe src/*.cpp -ladvapi32
```

> Linux/macOS 下用的是内置 Mock 注册表 (演示数据, 改动不保存)。

---

MIT License · 作者 [wudream813](https://github.com/wudream813)
