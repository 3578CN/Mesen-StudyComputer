## Aot 发布命令

先用 msbuild 编译：
```bash
cd D:\Git代码\Mesen(Github)
msbuild Mesen.sln /p:Configuration=Release /p:Platform=x64
```
再执行 AOT 发布：
```bash
dotnet publish -c Release -r win-x64 -p:PublishAot=true -p:PublishSingleFile=false -p:SelfContained=true
```

## 开发日志
2025-10-23 已完成 bbk_bios10.nes 的 Mapper 主要功能支持，所有游戏运行正常。

下一步计划：
- 磁盘管理，文件管理器的开发。
- 键盘映射优化，有些键位没反应的问题。
