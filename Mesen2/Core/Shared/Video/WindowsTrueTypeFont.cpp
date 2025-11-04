/*------------------------------------------------------------------------
名称：WindowsTrueTypeFont
说明：实现Windows平台HUD文字的TrueType字体读取与字形缓存。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-11-05
备注：通过GDI的GetGlyphOutline接口生成灰度点阵。
------------------------------------------------------------------------*/
#include "pch.h"
#ifdef _WIN32

#include "Shared/Video/WindowsTrueTypeFont.h"

#ifndef _WINDOWS_
using HANDLE = void*;
using HDC = HANDLE;
using HFONT = HANDLE;
using HGDIOBJ = HANDLE;
using LPCWSTR = const wchar_t*;
using LPVOID = void*;
using DWORD = unsigned long;
using UINT = unsigned int;
using BOOL = int;
using BYTE = unsigned char;
using LONG = long;

struct POINT
{
    LONG x;
    LONG y;
};

struct FIXED
{
    short fract;
    short value;
};

struct MAT2
{
    FIXED eM11;
    FIXED eM12;
    FIXED eM21;
    FIXED eM22;
};

struct GLYPHMETRICS
{
    UINT gmBlackBoxX;
    UINT gmBlackBoxY;
    POINT gmptGlyphOrigin;
    short gmCellIncX;
    short gmCellIncY;
};

#ifndef DEFAULT_CHARSET
#define DEFAULT_CHARSET 1
#endif
#ifndef OUT_TT_PRECIS
#define OUT_TT_PRECIS 4
#endif
#ifndef CLIP_DEFAULT_PRECIS
#define CLIP_DEFAULT_PRECIS 0
#endif
#ifndef ANTIALIASED_QUALITY
#define ANTIALIASED_QUALITY 4
#endif
#ifndef DEFAULT_PITCH
#define DEFAULT_PITCH 0
#endif
#ifndef FF_DONTCARE
#define FF_DONTCARE 0
#endif
#ifndef FW_NORMAL
#define FW_NORMAL 400
#endif
#ifndef GGO_GRAY8_BITMAP
#define GGO_GRAY8_BITMAP 0x00000006
#endif
#ifndef GDI_ERROR
#define GDI_ERROR 0xFFFFFFFF
#endif

extern "C" __declspec(dllimport) HDC __stdcall CreateCompatibleDC(HDC hdc);
extern "C" __declspec(dllimport) BOOL __stdcall DeleteDC(HDC hdc);
extern "C" __declspec(dllimport) HFONT __stdcall CreateFontW(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
    DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision,
    DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName);
extern "C" __declspec(dllimport) HGDIOBJ __stdcall SelectObject(HDC hdc, HGDIOBJ hgdiobj);
extern "C" __declspec(dllimport) BOOL __stdcall DeleteObject(HGDIOBJ hObject);
extern "C" __declspec(dllimport) UINT __stdcall GetGlyphOutlineW(HDC hdc, UINT uChar, UINT fuFormat,
    GLYPHMETRICS* lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2);
#endif

namespace
{
    constexpr MAT2 IdentityMat = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    constexpr wchar_t const* FontCandidates[] = {
        L"Microsoft YaHei UI",
        L"Microsoft YaHei",
        L"SimSun",
        L"SimHei",
        L"DengXian",
        L"Arial Unicode MS",
        L"MS Gothic"
    };
}

WindowsTrueTypeFont& WindowsTrueTypeFont::GetInstance()
{
    static WindowsTrueTypeFont instance;
    return instance;
}

WindowsTrueTypeFont::WindowsTrueTypeFont()
{
    _lineHeight = _fontPixelHeight;
    _baseline = _fontPixelHeight;
    _spaceAdvance = _fontPixelHeight / 2;
    _missingGlyph = BuildFallbackGlyph();
}

WindowsTrueTypeFont::~WindowsTrueTypeFont()
{
    HDC hdc = reinterpret_cast<HDC>(_hdc);
    if(hdc) {
        if(_defaultFont) {
            SelectObject(hdc, reinterpret_cast<HGDIOBJ>(_defaultFont));
        }
        if(_font) {
            DeleteObject(reinterpret_cast<HGDIOBJ>(_font));
        }
        DeleteDC(hdc);
    }
}

bool WindowsTrueTypeFont::EnsureReady()
{
    std::lock_guard<std::mutex> guard(_mutex);
    if(_initialized) {
        return true;
    }

    if(Initialize()) {
        _initialized = true;
        return true;
    }

    return false;
}

bool WindowsTrueTypeFont::Initialize()
{
    if(!_hdc) {
        _hdc = CreateCompatibleDC(nullptr);
        if(!_hdc) {
            return false;
        }
    }

    for(const wchar_t* name : FontCandidates) {
        if(CreateFontWithName(name)) {
            return true;
        }
    }

    return false;
}

bool WindowsTrueTypeFont::CreateFontWithName(const wchar_t* fontName)
{
    HDC hdc = reinterpret_cast<HDC>(_hdc);
    HFONT font = CreateFontW(-_fontPixelHeight, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
        OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, fontName);
    if(!font) {
        return false;
    }

    HGDIOBJ previous = SelectObject(hdc, reinterpret_cast<HGDIOBJ>(font));
    if(!previous) {
        DeleteObject(reinterpret_cast<HGDIOBJ>(font));
        return false;
    }

    if(!_defaultFont) {
        _defaultFont = previous;
    }

    bool metricsOk = UpdateFontMetrics();
    if(!metricsOk) {
        SelectObject(hdc, previous);
        DeleteObject(reinterpret_cast<HGDIOBJ>(font));
        return false;
    }

    if(_font) {
        DeleteObject(reinterpret_cast<HGDIOBJ>(_font));
    }

    _font = font;
    _glyphCache.clear();
    return true;
}

bool WindowsTrueTypeFont::UpdateFontMetrics()
{
    HDC hdc = reinterpret_cast<HDC>(_hdc);
    int baseline = 0;
    int descender = 0;

    const wchar_t samples[] = { L'A', L'g', L'中', L'j', L'y' };
    for(wchar_t ch : samples) {
        GLYPHMETRICS gm = {};
        DWORD size = GetGlyphOutlineW(hdc, static_cast<UINT>(ch), GGO_GRAY8_BITMAP, &gm, 0, nullptr, &IdentityMat);
        if(size == GDI_ERROR) {
            continue;
        }
        baseline = std::max(baseline, static_cast<int>(gm.gmptGlyphOrigin.y));
        int below = static_cast<int>(gm.gmBlackBoxY) - static_cast<int>(gm.gmptGlyphOrigin.y);
        descender = std::max(descender, below);
    }

    if(baseline <= 0) {
        baseline = _fontPixelHeight;
    }
    if(descender < 0) {
        descender = 0;
    }

    _baseline = baseline;
    _lineHeight = std::max(1, baseline + descender);

    GLYPHMETRICS spaceMetrics = {};
    DWORD spaceSize = GetGlyphOutlineW(hdc, static_cast<UINT>(L' '), GGO_GRAY8_BITMAP, &spaceMetrics, 0, nullptr, &IdentityMat);
    if(spaceSize == GDI_ERROR) {
        _spaceAdvance = std::max(1, _fontPixelHeight / 2);
    } else {
        _spaceAdvance = std::max(1, static_cast<int>(spaceMetrics.gmCellIncX));
    }

    return true;
}

const WindowsTrueTypeFont::GlyphBitmap& WindowsTrueTypeFont::GetGlyph(uint32_t codepoint)
{
    std::lock_guard<std::mutex> guard(_mutex);
    if(!_initialized) {
        return _missingGlyph;
    }

    auto it = _glyphCache.find(codepoint);
    if(it != _glyphCache.end()) {
        return it->second;
    }

    GlyphBitmap glyph = BuildGlyph(codepoint);
    auto result = _glyphCache.emplace(codepoint, std::move(glyph));
    return result.first->second;
}

WindowsTrueTypeFont::GlyphBitmap WindowsTrueTypeFont::BuildGlyph(uint32_t codepoint)
{
    GlyphBitmap glyph;
    if(!_hdc || !_font) {
        return _missingGlyph;
    }

    UINT charCode = (codepoint <= 0xFFFF) ? static_cast<UINT>(codepoint) : static_cast<UINT>('?');

    GLYPHMETRICS metrics = {};
    DWORD required = GetGlyphOutlineW(reinterpret_cast<HDC>(_hdc), charCode, GGO_GRAY8_BITMAP, &metrics, 0, nullptr, &IdentityMat);
    if(required == GDI_ERROR) {
        if(codepoint != static_cast<uint32_t>('?')) {
            return BuildGlyph(static_cast<uint32_t>('?'));
        }
        return _missingGlyph;
    }

    glyph.Advance = metrics.gmCellIncX;
    if(glyph.Advance == 0) {
        glyph.Advance = static_cast<int>(metrics.gmBlackBoxX);
    }
    if(glyph.Advance == 0) {
        glyph.Advance = _spaceAdvance;
    }

    glyph.OffsetX = metrics.gmptGlyphOrigin.x;
    glyph.OffsetY = -metrics.gmptGlyphOrigin.y;
    glyph.Width = static_cast<int>(metrics.gmBlackBoxX);
    glyph.Height = static_cast<int>(metrics.gmBlackBoxY);

    if(required > 0 && glyph.Width > 0 && glyph.Height > 0) {
        std::vector<uint8_t> buffer(required);
        DWORD written = GetGlyphOutlineW(reinterpret_cast<HDC>(_hdc), charCode, GGO_GRAY8_BITMAP, &metrics, required, buffer.data(), &IdentityMat);
        if(written != GDI_ERROR) {
            int pitch = (glyph.Width + 3) & ~3;
            glyph.Pixels.resize(static_cast<size_t>(glyph.Width) * static_cast<size_t>(glyph.Height));
            for(int y = 0; y < glyph.Height; y++) {
                uint8_t* src = buffer.data() + y * pitch;
                for(int x = 0; x < glyph.Width; x++) {
                    uint8_t coverage = src[x];
                    if(coverage > 64) {
                        coverage = 64;
                    }
                    glyph.Pixels[static_cast<size_t>(y) * glyph.Width + x] = static_cast<uint8_t>(std::min(255, coverage * 4));
                }
            }
        }
    }

    return glyph;
}

WindowsTrueTypeFont::GlyphBitmap WindowsTrueTypeFont::BuildFallbackGlyph() const
{
    GlyphBitmap glyph;
    glyph.Width = _fontPixelHeight / 2;
    glyph.Height = _fontPixelHeight;
    glyph.Advance = glyph.Width;
    glyph.OffsetX = 0;
    glyph.OffsetY = -glyph.Height;
    glyph.Pixels.resize(static_cast<size_t>(glyph.Width) * glyph.Height, 255);
    return glyph;
}

int WindowsTrueTypeFont::GetLineHeight() const
{
    return std::max(1, _lineHeight);
}

int WindowsTrueTypeFont::GetBaselineOffset() const
{
    return std::max(0, _baseline);
}

int WindowsTrueTypeFont::GetSpaceAdvance() const
{
    return std::max(1, _spaceAdvance);
}

#endif
