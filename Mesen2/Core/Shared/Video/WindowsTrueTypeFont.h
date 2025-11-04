/*------------------------------------------------------------------------
名称：WindowsTrueTypeFont
说明：封装Windows平台HUD文字的TrueType字体处理。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-05
备注：依赖GDI访问TrueType字形。
------------------------------------------------------------------------*/
#pragma once
#ifdef _WIN32

#include "pch.h"
#include <mutex>

class WindowsTrueTypeFont
{
public:
    struct GlyphBitmap
    {
        std::vector<uint8_t> Pixels;
        int Width = 0;
        int Height = 0;
        int OffsetX = 0;
        int OffsetY = 0;
        int Advance = 0;
    };

    static WindowsTrueTypeFont& GetInstance();

    bool EnsureReady();
    const GlyphBitmap& GetGlyph(uint32_t codepoint);

    int GetLineHeight() const;
    int GetBaselineOffset() const;
    int GetSpaceAdvance() const;

private:
    WindowsTrueTypeFont();
    ~WindowsTrueTypeFont();

    WindowsTrueTypeFont(const WindowsTrueTypeFont&) = delete;
    WindowsTrueTypeFont& operator=(const WindowsTrueTypeFont&) = delete;

    bool Initialize();
    bool CreateFontWithName(const wchar_t* fontName);
    bool UpdateFontMetrics();
    GlyphBitmap BuildGlyph(uint32_t codepoint);
    GlyphBitmap BuildFallbackGlyph() const;

    void* _hdc = nullptr;
    void* _font = nullptr;
    void* _defaultFont = nullptr;

    int _lineHeight = 0;
    int _baseline = 0;
    int _spaceAdvance = 0;
    bool _initialized = false;

    std::unordered_map<uint32_t, GlyphBitmap> _glyphCache;
    GlyphBitmap _missingGlyph;
    std::mutex _mutex;

    static constexpr int _fontPixelHeight = 18;
};

#endif
