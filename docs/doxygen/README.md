# Doxygen 文档目录说明

说明：本目录为 Doxygen 文档生成相关的说明与使用步骤。已在仓库根新增 `Doxyfile`，默认将输入设置为 `Mesen(Github)/Mesen2` 并输出到 `docs/doxygen/html/`。

依赖：
- Doxygen（必须）
- Graphviz（可选，用于生成类图/调用图；如需启用请安装并在 `Doxyfile` 中把 `HAVE_DOT` 设置为 `YES`）

快速上手（PowerShell）：

1. 使用 doxygen（在仓库根执行）：

```powershell
# 在仓库根运行：
doxygen Doxyfile
```

2. 使用提供的构建脚本（脚本会检查 doxygen，并在生成后打开默认浏览器预览）：

```powershell
.\scripts\build_docs.ps1
```

默认假设与注意事项：
- 默认 `INPUT` 指向 `Mesen(Github)/Mesen2`，如果你希望包含其他目录（例如 `InteropDLL` 或 `UI`），请修改仓库根的 `Doxyfile` 中的 `INPUT` 条目。 
- 若路径中包含空格或特殊字符，建议在 `Doxyfile` 中使用相对路径或调整为合适的路径格式。
- 本仓库未自动安装 Doxygen 或 Graphviz；请在本地环境安装后运行脚本。

下一步建议：
- 若你确认要我在此环境中执行一次生成验证，请回复“请生成”，我会在生成前再次询问并尝试运行（若当前环境已安装 doxygen）。
