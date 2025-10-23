/*------------------------------------------------------------------------
名称：OutputDebug 调试输出封装
说明：提供简洁的 OutputDebug 函数封装，向调试输出窗口写入以 "[Mesen NowTime][MyClass::MyMethod]" 开头的日志行。
     新增对基本类型（bool、整型、浮点、const char*、std::string 等）直接传入的支持，
     所有重载最终调用 OutputDebugInternal。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-04
备注：使用 OutputDebugStringA 输出窄字符串，若输入不包含换行则自动追加换行。
------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------
用法（每行一条：用法 -> 示例输出）
时间与函数名为示例占位，运行时以实际值为准。
======================================================================================================================
OutputDebug("plain message")						-> [Mesen 12:34:56.789][MyClass::MyMethod] plain message
OutputDebug("Value=%d, name=%s", 42, "foo")	-> [Mesen 12:34:56.789][MyClass::MyMethod] Value=42, name=foo
OutputDebug("error: %s", "disk full")			->	[Mesen 12:34:56.789][MyClass::MyMethod] error: disk full
OutputDebug(std::string("hello"))				->	[Mesen 12:34:56.789][MyClass::MyMethod] hello
OutputDebug(true)										->	[Mesen 12:34:56.789][MyClass::MyMethod] true
OutputDebug(false)									->	[Mesen 12:34:56.789][MyClass::MyMethod] false
OutputDebug(123)										->	[Mesen 12:34:56.789][MyClass::MyMethod] 123
OutputDebug(3.14159)									->	[Mesen 12:34:56.789][MyClass::MyMethod] 3.14159
OutputDebug(MyEnum::Value)							->	[Mesen 12:34:56.789][MyClass::MyMethod] 0
OutputDebug(myObj)									->	[Mesen 12:34:56.789][MyClass::MyMethod] <myObj 的 operator<< 输出>
---------------------------------------------------------------------------------------------------------------------*/
#pragma once

#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <type_traits>
#include <cstdarg>
#include <cstdio>

// 前向声明需要的函数，而不是包含完整的 <Windows.h> 头文件。
extern "C" __declspec(dllimport) void __stdcall OutputDebugStringA(const char *lpOutputString);

#ifdef __INTELLISENSE__
// 给 IntelliSense / 静态分析器一个可见的定义以消除 VCR001 警告。
// 仅供编辑器分析使用，真实构建仍从 Kernel32.lib 获取符号。
extern "C" inline void __stdcall OutputDebugStringA(const char *lpOutputString) {}
#endif

/// <summary>
/// 内部实现：接收调用处的函数签名 caller 并输出消息。
/// </summary>
inline void OutputDebugInternal(const char *caller, const std::string &msg)
{
    // 获取当前本地时间并格式化为 HH:MM:SS.mmm（含毫秒）
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#ifdef _MSC_VER
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms.count();

    // 前缀带时间，例如："【Mesen 12:44:53】"
    std::string prefix = "[Mesen ";
    prefix += ss.str();
    prefix += "]";

    // 解析 caller，尽量提取类::方法 或 函数名（启发式）
    std::string func;
    if (caller)
    {
        func = caller;
        size_t paramsPos = func.find('(');
        if (paramsPos != std::string::npos)
        {
            func = func.substr(0, paramsPos);
        }
        size_t lastSpace = func.find_last_of(' ');
        if (lastSpace != std::string::npos)
        {
            func = func.substr(lastSpace + 1);
        }
    }

    // 组装最终输出：时间前缀 + [类::方法] + 原消息
    std::string out = prefix;
    if (!func.empty())
    {
        out += "[" + func + "] ";
    }
    out += msg;
    // 保证以换行结尾
    if (out.empty() || out.back() != '\n')
        out.push_back('\n');

    // 直接调用窄字符版本的 OutputDebugString
    OutputDebugStringA(out.c_str());
}

// ------------------------------------------------------------
// 类型友好的外层 API：OutputDebugFmt 系列重载
// ------------------------------------------------------------

// 支持 printf 风格格式化的重载（可变参数）
// 使用 vsnprintf 生成临时字符串，再传给 OutputDebugInternal。
// 注意：确保传入的格式化字符串与参数匹配，非宽字符（窄字符串）。
inline void OutputDebugFmt(const char *caller, const char *fmt, ...)
{
    if (!fmt)
    {
        OutputDebugInternal(caller, "(null)");
        return;
    }

    va_list args;
    va_start(args, fmt);

    // 先用 va_copy 计算所需长度
    va_list argsCopy;
    va_copy(argsCopy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, argsCopy);
    va_end(argsCopy);

    if (len < 0)
    {
        va_end(args);
        OutputDebugInternal(caller, "(format error)");
        return;
    }

    std::string buf;
    buf.resize(static_cast<size_t>(len));
    std::vsnprintf(&buf[0], buf.size() + 1, fmt, args); // 包括终止符
    va_end(args);

    OutputDebugInternal(caller, buf);
}

// 字符串版本（std::string）
inline void OutputDebugFmt(const char *caller, const std::string &msg)
{
    OutputDebugInternal(caller, msg);
}

// bool 版本：输出 "true"/"false"
inline void OutputDebugFmt(const char *caller, bool v)
{
    OutputDebugInternal(caller, v ? "true" : "false");
}

// 整型通用处理：使用 enable_if 区分整型与枚举
template <typename T>
inline typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type
OutputDebugFmt(const char *caller, T v)
{
    std::ostringstream ss;
    ss << v;
    OutputDebugInternal(caller, ss.str());
}

// 浮点数通用处理
template <typename T>
inline typename std::enable_if<std::is_floating_point<T>::value>::type
OutputDebugFmt(const char *caller, T v)
{
    std::ostringstream ss;
    ss << v;
    OutputDebugInternal(caller, ss.str());
}

// 枚举友好处理（输出为整数）
// 如果需要更友好的枚举名称，可在调用处自行转换
template <typename T>
inline typename std::enable_if<std::is_enum<T>::value>::type
OutputDebugFmt(const char *caller, T v)
{
    using UT = typename std::underlying_type<T>::type;
    std::ostringstream ss;
    ss << static_cast<UT>(v);
    OutputDebugInternal(caller, ss.str());
}

// 其他通用类型（fallback）：使用 std::ostringstream 尝试格式化
template <typename T>
inline typename std::enable_if<!std::is_arithmetic<T>::value && !std::is_enum<T>::value>::type
OutputDebugFmt(const char *caller, const T &v)
{
    std::ostringstream ss;
    ss << v; // 依赖类型提供 operator<<
    OutputDebugInternal(caller, ss.str());
}

// 宏保留：根据编译器展开适当的内置函数签名
#if defined(_MSC_VER)
/*
OutputDebug 调试输出

OutputDebug("plain message")						->[Mesen 12:34 : 56.789][MyClass::MyMethod] plain message
OutputDebug("Value=%d, name=%s", 42, "foo")	->[Mesen 12:34 : 56.789][MyClass::MyMethod] Value = 42, name = foo
OutputDebug("error: %s", "disk full")			->[Mesen 12:34 : 56.789][MyClass::MyMethod] error: disk full
OutputDebug(std::string("hello"))				->[Mesen 12:34 : 56.789][MyClass::MyMethod] hello
OutputDebug(true)										->[Mesen 12:34 : 56.789][MyClass::MyMethod] true
OutputDebug(false)									->[Mesen 12:34 : 56.789][MyClass::MyMethod] false
OutputDebug(123)										->[Mesen 12:34 : 56.789][MyClass::MyMethod] 123
OutputDebug(3.14159)									->[Mesen 12:34 : 56.789][MyClass::MyMethod] 3.14159
OutputDebug(MyEnum::Value)							->[Mesen 12:34 : 56.789][MyClass::MyMethod] 0
OutputDebug(myObj)									->[Mesen 12:34 : 56.789][MyClass::MyMethod] <myObj 的 operator<< 输出>
*/
#define OutputDebug(...) OutputDebugFmt(__FUNCSIG__, __VA_ARGS__)
#elif defined(__GNUC__) || defined(__clang__)
#define OutputDebug(...) OutputDebugFmt(__PRETTY_FUNCTION__, __VA_ARGS__)
#else
#define OutputDebug(...) OutputDebugFmt(__func__, __VA_ARGS__)
#endif