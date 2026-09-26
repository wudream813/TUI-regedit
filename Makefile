# Makefile - Linux / macOS 演示版构建 (Mock 数据)
# 另附 windows 目标: 在 Linux 上用 MinGW 交叉编译 Windows 版
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc
TARGET = tui-regedit
SRCS = src/main.cpp src/app.cpp src/tui.cpp src/registry_mock.cpp

all: $(TARGET)

$(TARGET): $(SRCS) src/*.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

# 需要 mingw-w64 (Debian/Ubuntu: sudo apt install g++-mingw-w64-x86-64)
# -static: 静态链接 libstdc++/libgcc, 生成的 exe 在任意 Windows 上双击即用
windows:
	x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Isrc -static \
		-static-libgcc -static-libstdc++ \
		-o tui-regedit.exe src/main.cpp src/app.cpp src/tui.cpp \
		src/registry_mock.cpp src/registry_win.cpp -ladvapi32
	@echo "==> tui-regedit.exe (Windows x64, 静态链接) 构建完成"

run: $(TARGET)
	./$(TARGET) --mock

clean:
	rm -f $(TARGET) tui-regedit.exe

.PHONY: all windows run clean
