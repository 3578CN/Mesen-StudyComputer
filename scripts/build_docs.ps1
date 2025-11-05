<#
名称：build_docs.ps1
说明：在仓库根使用 Doxygen 生成文档并（可选）打开生成的 HTML 预览。
作者：自动生成（根据用户指示添加）
日期：2025-11-05
备注：脚本使用中文提示，若需调整 Doxyfile 路径请编辑本脚本或把 Doxyfile 放到仓库根。
#>

param(
    [switch]$OpenAfter = $true
)

# 获取脚本与仓库根路径
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path
$doxyfile = Join-Path $repoRoot "Doxyfile"

if (-not (Test-Path $doxyfile)) {
    Write-Host "未找到 Doxyfile：$doxyfile`n请确保 Doxyfile 位于仓库根或修改脚本以指向正确位置。"
    exit 1
}

# 检查 doxygen 是否安装
$doxygenCmd = Get-Command doxygen -ErrorAction SilentlyContinue
if (-not $doxygenCmd) {
    Write-Host "未检测到 doxygen。请先安装 Doxygen：https://www.doxygen.nl/ 。"
    exit 1
}

Write-Host "开始使用 Doxygen 生成文档（Doxyfile：$doxyfile）..."
& doxygen $doxyfile

if ($LASTEXITCODE -ne 0) {
    Write-Host "Doxygen 执行失败，退出码：$LASTEXITCODE"
    exit $LASTEXITCODE
}

$htmlIndex = Join-Path $repoRoot "docs\doxygen\html\index.html"
if ($OpenAfter -and (Test-Path $htmlIndex)) {
    Write-Host "生成完成，打开：$htmlIndex"
    Start-Process -FilePath (Resolve-Path $htmlIndex)
} else {
    Write-Host "生成完成，输出目录：$(Join-Path $repoRoot 'docs\doxygen\html')"
}

exit 0
