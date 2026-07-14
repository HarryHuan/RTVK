# AGENTS.md — RTVK 项目规范

## 代码风格

- **正确缩进**：使用 4 空格缩进，不用 Tab；花括号另起一行（Allman 风格）
- **紧凑但不挤**：单行短语句保持紧凑（如 `if (x) return;`），多行结构留空行分隔
- **命名**：类名 PascalCase，函数名 camelCase，成员变量 `m_` 前缀

## 项目约定

- C++17，CMake 3.25+，Qt 6.11，Vulkan 1.3
- 模块：`src/core` / `src/sim` / `src/render` / `src/ui`
- 构建：`cmake --build build --config Debug`
- 运行前设置 PATH：`$env:PATH = "C:\Qt\6.11.1\msvc2022_64\bin;$env:PATH"`
- 不要提交 `vk.log`、`build/`、`*.spv`