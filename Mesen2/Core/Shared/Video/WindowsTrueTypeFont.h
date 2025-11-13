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
#include <unordered_map>
#include <vector>

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
	const GlyphBitmap& GetGlyph(uint32_t codepoint, int pixelHeight);

	int GetLineHeight() const;
	int GetBaselineOffset() const;
	int GetSpaceAdvance() const;
	int GetBasePixelHeight() const { return _fontPixelHeight; }

private:
	WindowsTrueTypeFont();
	~WindowsTrueTypeFont();

	WindowsTrueTypeFont(const WindowsTrueTypeFont&) = delete;
	WindowsTrueTypeFont& operator=(const WindowsTrueTypeFont&) = delete;

	bool Initialize();
	bool CreateFontForHeight(const wchar_t* fontName, int pixelHeight);
	bool UpdateFontMetrics();
	GlyphBitmap BuildGlyph(uint32_t codepoint, int pixelHeight);
	void* GetFontHandle(int pixelHeight);
	GlyphBitmap BuildFallbackGlyph() const;

	void* _hdc = nullptr;
	void* _defaultFont = nullptr;
	std::unordered_map<int, void*> _fonts;

	int _lineHeight = 0;
	int _baseline = 0;
	int _spaceAdvance = 0;
	bool _initialized = false;

	std::unordered_map<uint64_t, GlyphBitmap> _glyphCache;
	GlyphBitmap _missingGlyph;
	std::mutex _mutex;
	// 字体像素高度。
	static constexpr int _fontPixelHeight = 10;
};

#endif
