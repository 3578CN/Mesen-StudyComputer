# 发布Aot

先用 msbuild 编译：
```bash
cd D:\Git代码\Emulator(Azure)\Mesen2
msbuild Mesen.sln /p:Configuration=Release /p:Platform=x64
```
再执行 AOT 发布：
```bash
dotnet publish -c Release -r win-x64 -p:PublishAot=true -p:PublishSingleFile=false -p:SelfContained=true
```