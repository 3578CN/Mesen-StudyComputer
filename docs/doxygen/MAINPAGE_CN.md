# Mesen 项目 API 文档（中文）

欢迎使用 Mesen 项目生成的 API 文档（中文界面）。本页面为文档主页，包含快速导航与构建说明。

## 简介
本文档由 Doxygen 自动生成，包含项目的命名空间、类、方法、视图等 API 说明。请注意：源码中的注释语言保持原样，Doxygen 不会自动翻译源代码注释。

## 本地生成说明
1. 安装 Doxygen（https://www.doxygen.nl/）。
2. 在仓库根运行：

```powershell
doxygen Doxyfile
# 或使用本仓库的构建脚本：
.\scripts\build_docs.ps1
```

生成的静态站点位于：`docs/doxygen/html/`，打开 `index.html` 进行预览。

## 常见操作
- 若需图形（类图/调用图），请安装 Graphviz 并在 `Doxyfile` 中将 `HAVE_DOT = YES`。
- 若希望把函数/类说明翻译为中文，需要把对应的注释或 README 文件翻译为中文；此操作不会自动完成。

## 需要我帮忙的地方
如果你希望我把特定模块或若干 README 翻译为中文，请列出文件或目录，我可以逐步翻译并更新文档。
