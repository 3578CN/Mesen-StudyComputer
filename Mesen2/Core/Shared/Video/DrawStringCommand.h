#pragma once
#include "pch.h"
#include "Shared/Video/DrawCommand.h"
#ifdef _WIN32
#include "Shared/Video/WindowsTrueTypeFont.h"
#include <limits>
#endif

class DrawStringCommand : public DrawCommand
{
private:
	int _x, _y, _color, _backColor;
	int _maxWidth = 0;
	string _text;

	//Taken from FCEUX's LUA code
	static constexpr int _tabSpace = 4;

	static unordered_map<int, char const*> _jpFont;

	static constexpr uint8_t _font[792] = {
		6,  0,  0,  0,  0,  0,  0,  0,	// 0x20 - Spacebar
		2,128,128,128,128,128,  0,128,
		5, 80, 80, 80,  0,  0,  0,  0,
		6, 80, 80,248, 80,248, 80, 80,
		6, 32,120,160,112, 40,240, 32,
		6, 64,168, 80, 32, 80,168, 16,
		6, 96,144,160, 64,168,144,104,
		3, 64, 64,  0,  0,  0,  0,  0,
		4, 32, 64, 64, 64, 64, 64, 32,
		4,128, 64, 64, 64, 64, 64,128,
		6,  0, 80, 32,248, 32, 80,  0,
		6,  0, 32, 32,248, 32, 32,  0,
		3,  0,  0,  0,  0,  0, 64,128,
		5,  0,  0,  0,240,  0,  0,  0,
		3,  0,  0,  0,  0,  0,  0, 64,
		5, 16, 16, 32, 32, 32, 64, 64,
		6,112,136,136,136,136,136,112,	// 0x30 - 0
		6, 32, 96, 32, 32, 32, 32, 32,
		6,112,136,  8, 48, 64,128,248,
		6,112,136,  8, 48,  8,136,112,
		6, 16, 48, 80,144,248, 16, 16,
		6,248,128,128,240,  8,  8,240,
		6, 48, 64,128,240,136,136,112,
		6,248,  8, 16, 16, 32, 32, 32,
		6,112,136,136,112,136,136,112,
		6,112,136,136,120,  8, 16, 96,
		3,  0,  0, 64,  0,  0, 64,  0,
		3,  0,  0, 64,  0,  0, 64,128,
		4,  0, 32, 64,128, 64, 32,  0,
		5,  0,  0,240,  0,240,  0,  0,
		4,  0,128, 64, 32, 64,128,  0,
		6,112,136,  8, 16, 32,  0, 32,	// 0x3F - ?
		6,112,136,136,184,176,128,112,	// 0x40 - @
		6,112,136,136,248,136,136,136,	// 0x41 - A
		6,240,136,136,240,136,136,240,
		6,112,136,128,128,128,136,112,
		6,224,144,136,136,136,144,224,
		6,248,128,128,240,128,128,248,
		6,248,128,128,240,128,128,128,
		6,112,136,128,184,136,136,120,
		6,136,136,136,248,136,136,136,
		4,224, 64, 64, 64, 64, 64,224,
		6,  8,  8,  8,  8,  8,136,112,
		6,136,144,160,192,160,144,136,
		6,128,128,128,128,128,128,248,
		6,136,216,168,168,136,136,136,
		6,136,136,200,168,152,136,136,
		7, 48, 72,132,132,132, 72, 48,
		6,240,136,136,240,128,128,128,
		6,112,136,136,136,168,144,104,
		6,240,136,136,240,144,136,136,
		6,112,136,128,112,  8,136,112,
		6,248, 32, 32, 32, 32, 32, 32,
		6,136,136,136,136,136,136,112,
		6,136,136,136, 80, 80, 32, 32,
		6,136,136,136,136,168,168, 80,
		6,136,136, 80, 32, 80,136,136,
		6,136,136, 80, 32, 32, 32, 32,
		6,248,  8, 16, 32, 64,128,248,
		3,192,128,128,128,128,128,192,
		5, 64, 64, 32, 32, 32, 16, 16,
		3,192, 64, 64, 64, 64, 64,192,
		4, 64,160,  0,  0,  0,  0,  0,
		6,  0,  0,  0,  0,  0,  0,248,
		3,128, 64,  0,  0,  0,  0,  0,
		5,  0,  0, 96, 16,112,144,112,	// 0x61 - a
		5,128,128,224,144,144,144,224,
		5,  0,  0,112,128,128,128,112,
		5, 16, 16,112,144,144,144,112,
		5,  0,  0, 96,144,240,128,112,
		5, 48, 64,224, 64, 64, 64, 64,
		5,  0,112,144,144,112, 16,224,
		5,128,128,224,144,144,144,144,
		2,128,  0,128,128,128,128,128,
		4, 32,  0, 32, 32, 32, 32,192,
		5,128,128,144,160,192,160,144,
		2,128,128,128,128,128,128,128,
		6,  0,  0,208,168,168,168,168,
		5,  0,  0,224,144,144,144,144,
		5,  0,  0, 96,144,144,144, 96,
		5,  0,224,144,144,224,128,128,
		5,  0,112,144,144,112, 16, 16,
		5,  0,  0,176,192,128,128,128,
		5,  0,  0,112,128, 96, 16,224,
		4, 64, 64,224, 64, 64, 64, 32,
		5,  0,  0,144,144,144,144,112,
		5,  0,  0,144,144,144,160,192,
		6,  0,  0,136,136,168,168, 80,
		5,  0,  0,144,144, 96,144,144,
		5,  0,144,144,144,112, 16, 96,
		5,  0,  0,240, 32, 64,128,240,
		4, 32, 64, 64,128, 64, 64, 32,
		3, 64, 64, 64, 64, 64, 64, 64,
		4,128, 64, 64, 32, 64, 64,128,
		6,  0,104,176,  0,  0,  0,  0,
		5,  0,  0,113, 80,113,  0,  0,
	};

	static int GetCharNumber(char ch)
	{
		if(ch < 32) {
			return 0;
		}

		ch -= 32;
		return (ch < 0 || ch > 94) ? 95 : ch;
	}

	static int GetCharWidth(char ch)
	{
		return _font[GetCharNumber(ch) * 8];
	}

protected:
	void InternalDraw()
	{
#ifdef _WIN32
		int baseScale = (int)std::floor(_xScale);
		if(baseScale == 0) {
			baseScale = 1;
		}
		int startX = (int)(_x * _xScale / baseScale);
		int currentX = startX;
		int currentY = _y;

		WindowsTrueTypeFont& font = WindowsTrueTypeFont::GetInstance();
		font.EnsureReady();

		const int lineHeight = std::max(1, font.GetLineHeight());
		const int baselineOffset = font.GetBaselineOffset();
		int baseline = currentY + baselineOffset;
		int lineTop = baseline - baselineOffset;

		const int wrapWidth = _maxWidth > 0 ? _maxWidth : std::numeric_limits<int>::max();
		const int spaceAdvance = std::max(1, font.GetSpaceAdvance());
		const int tabAdvance = std::max(1, spaceAdvance * _tabSpace);

		const uint32_t fgRGB = _color & 0x00FFFFFF;
		const uint32_t fgAlpha = (_color >> 24) & 0xFF;
		const uint32_t bgAlpha = (_backColor >> 24) & 0xFF;
		const bool drawBackground = bgAlpha > 0;

		size_t index = 0;
		auto readCodepoint = [&](uint32_t& cp) -> bool {
			if(index >= _text.size()) {
				return false;
			}
			uint8_t lead = (uint8_t)_text[index++];
			if(lead < 0x80) {
				cp = lead;
				return true;
			}

			if((lead & 0xE0) == 0xC0 && index < _text.size()) {
				uint8_t b1 = (uint8_t)_text[index++];
				if((b1 & 0xC0) != 0x80) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x1F) << 6) | (b1 & 0x3F);
				return true;
			}

			if((lead & 0xF0) == 0xE0 && index + 1 < _text.size()) {
				uint8_t b1 = (uint8_t)_text[index++];
				uint8_t b2 = (uint8_t)_text[index++];
				if(((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80)) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
				return true;
			}

			if((lead & 0xF8) == 0xF0 && index + 2 < _text.size()) {
				uint8_t b1 = (uint8_t)_text[index++];
				uint8_t b2 = (uint8_t)_text[index++];
				uint8_t b3 = (uint8_t)_text[index++];
				if(((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80)) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
				return true;
			}

			cp = '?';
			return true;
		};

		auto newLine = [&]() {
			currentX = startX;
			currentY += lineHeight;
			baseline = currentY + baselineOffset;
			lineTop = baseline - baselineOffset;
		};

		uint32_t codepoint = 0;
		while(readCodepoint(codepoint)) {
			if(codepoint == '\r') {
				continue;
			}
			if(codepoint == '\n') {
				newLine();
				continue;
			}
			if(codepoint == '\t') {
				int relative = currentX - startX;
				int advance = tabAdvance;
				if(relative > 0) {
					int remainder = relative % tabAdvance;
					advance = remainder == 0 ? tabAdvance : tabAdvance - remainder;
				}
				if(relative > 0 && relative + advance > wrapWidth) {
					newLine();
					relative = 0;
					advance = tabAdvance;
				}
				if(drawBackground) {
					for(int row = 0; row < lineHeight; row++) {
						for(int column = 0; column < advance; column++) {
							DrawPixel(currentX + column, lineTop + row, _backColor);
						}
					}
				}
				currentX += advance;
				continue;
			}

			if(codepoint == ' ' && _maxWidth > 0 && currentX == startX) {
				continue;
			}

			const WindowsTrueTypeFont::GlyphBitmap& glyph = font.GetGlyph(codepoint);
			int relative = currentX - startX;
			if(relative > 0 && relative + glyph.Advance > wrapWidth) {
				newLine();
			}

			int drawX = currentX + glyph.OffsetX;
			int drawY = baseline + glyph.OffsetY;

			if(drawBackground) {
				if(glyph.Width > 0 && glyph.Height > 0) {
					for(int row = 0; row < glyph.Height; row++) {
						for(int column = 0; column < glyph.Width; column++) {
							DrawPixel(drawX + column, drawY + row, _backColor);
						}
					}
				} else {
					int bgWidth = std::max(spaceAdvance, glyph.Advance);
					for(int row = 0; row < lineHeight; row++) {
						for(int column = 0; column < bgWidth; column++) {
							DrawPixel(currentX + column, lineTop + row, _backColor);
						}
					}
				}
			}

			if(glyph.Width > 0 && glyph.Height > 0 && !glyph.Pixels.empty()) {
				for(int row = 0; row < glyph.Height; row++) {
					for(int column = 0; column < glyph.Width; column++) {
						uint8_t coverage = glyph.Pixels[static_cast<size_t>(row) * glyph.Width + column];
						if(coverage == 0) {
							continue;
						}
						uint32_t alpha = (fgAlpha * coverage + 127) / 255;
						if(alpha == 0) {
							continue;
						}
						uint32_t pixelColor = fgRGB | (alpha << 24);
						DrawPixel(drawX + column, drawY + row, pixelColor);
					}
				}
			}

			currentX += glyph.Advance;
		}
#else
		int startX = (int)(_x * _xScale / std::floor(_xScale));
		int lineWidth = 0;
		int x = startX;
		int y = _y;
		int lineHeight = 9;
		
		auto newLine = [&lineWidth, &x, &y, &lineHeight, startX]() {
			lineWidth = 0;
			x = startX;
			y += lineHeight;
			lineHeight = 9;
		};

		for(int i = 0; i < _text.size(); i++) {
			unsigned char c = _text[i];
			if(c == '\n') {
				newLine();
			} else if(c == '\t') {
				int tabWidth = (_tabSpace - (((x - startX) / 8) % _tabSpace)) * 8;
				x += tabWidth;
				lineWidth += tabWidth;
				if(_maxWidth > 0 && lineWidth > _maxWidth) {
					newLine();
				}
			} else if(c == 0x20) {
				//Space (ignore spaces at the start of a new line, when text wrapping is enabled)
				if(lineWidth > 0 || _maxWidth == 0) {
					if(_backColor & 0xFF000000) {
						//Draw bg color for spaces (when bg color is set)
						for(int row = 0; row < lineHeight; row++) {
							for(int column = 0; column < 6; column++) {
								DrawPixel(x + column, y + row - 1, _backColor);
							}
						}
					}

					lineWidth += 6;
					x += 6;
				}
			} else if(c >= 0x80) {
				//8x12 UTF-8 font for Japanese
				int code = (uint8_t)c;
				if(i + 2 < _text.size()) {
					code |= ((uint8_t)_text[i + 1]) << 8;
					code |= ((uint8_t)_text[i + 2]) << 16;

					auto res = _jpFont.find(code);
					if(res != _jpFont.end()) {
						lineWidth += 8;
						if(_maxWidth > 0 && lineWidth > _maxWidth) {
							newLine();
							lineWidth += 8;
						}

						uint8_t* charDef = (uint8_t*)res->second;

						for(int row = 0; row < 12; row++) {
							uint8_t rowData = charDef[row];
							for(int column = 0; column < 8; column++) {
								int drawFg = (rowData >> (7 - column)) & 0x01;
								DrawPixel(x + column, y + row - 2, drawFg ? _color : _backColor);
							}
						}
						i += 2;
						x += 8;
						lineHeight = 12;
					}
				}
			} else {
				//Variable size font for standard ASCII
				int ch = GetCharNumber(c);
				int width = GetCharWidth(c);
				
				lineWidth += width;
				if(_maxWidth > 0 && lineWidth > _maxWidth) {
					newLine();
					lineWidth += width;
				}

				int rowOffset = (c == 'y' || c == 'g' || c == 'p' || c == 'q') ? 1 : 0;
				for(int row = 0; row < 8; row++) {
					uint8_t rowData = ((row == 7 && rowOffset == 0) || (row == 0 && rowOffset == 1)) ? 0 : _font[ch * 8 + 1 + row - rowOffset];
					for(int col = 0; col < width; col++) {
						int drawFg = (rowData >> (7 - col)) & 0x01;
						DrawPixel(x + col, y + row, drawFg ? _color : _backColor);
					}
				}
				for(int col = 0; col < width; col++) {
					DrawPixel(x + col, y - 1, _backColor);
				}
				x += width;
			}
		}
#endif
	}

public:
	DrawStringCommand(int x, int y, string text, int color, int backColor, int frameCount, int startFrame, int maxWidth = 0, bool overwritePixels = false) :
		DrawCommand(startFrame, frameCount, true), _x(x), _y(y), _color(color), _backColor(backColor), _maxWidth(maxWidth), _text(text)
	{
		//Invert alpha byte - 0 = opaque, 255 = transparent (this way, no need to specifiy alpha channel all the time)
		_overwritePixels = overwritePixels;
		_color = (~color & 0xFF000000) | (color & 0xFFFFFF);
		_backColor = (~backColor & 0xFF000000) | (backColor & 0xFFFFFF);
	}

	static TextSize MeasureString(string& text, uint32_t maxWidth = 0)
	{
#ifdef _WIN32
		WindowsTrueTypeFont& font = WindowsTrueTypeFont::GetInstance();
		font.EnsureReady();

		const int lineHeight = std::max(1, font.GetLineHeight());
		const int spaceAdvance = std::max(1, font.GetSpaceAdvance());
		const int tabAdvance = std::max(1, spaceAdvance * _tabSpace);
		const bool hasWrap = maxWidth > 0;
		const int wrapWidth = hasWrap ? (int)maxWidth : std::numeric_limits<int>::max();

		int x = 0;
		int maxX = 0;
		int y = 0;

		size_t index = 0;
		auto readCodepoint = [&](uint32_t& cp) -> bool {
			if(index >= text.size()) {
				return false;
			}
			uint8_t lead = (uint8_t)text[index++];
			if(lead < 0x80) {
				cp = lead;
				return true;
			}

			if((lead & 0xE0) == 0xC0 && index < text.size()) {
				uint8_t b1 = (uint8_t)text[index++];
				if((b1 & 0xC0) != 0x80) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x1F) << 6) | (b1 & 0x3F);
				return true;
			}

			if((lead & 0xF0) == 0xE0 && index + 1 < text.size()) {
				uint8_t b1 = (uint8_t)text[index++];
				uint8_t b2 = (uint8_t)text[index++];
				if(((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80)) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
				return true;
			}

			if((lead & 0xF8) == 0xF0 && index + 2 < text.size()) {
				uint8_t b1 = (uint8_t)text[index++];
				uint8_t b2 = (uint8_t)text[index++];
				uint8_t b3 = (uint8_t)text[index++];
				if(((b1 & 0xC0) != 0x80) || ((b2 & 0xC0) != 0x80) || ((b3 & 0xC0) != 0x80)) {
					cp = '?';
					return true;
				}
				cp = ((lead & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
				return true;
			}

			cp = '?';
			return true;
		};

		auto newLine = [&]() {
			maxX = std::max(maxX, x);
			x = 0;
			y += lineHeight;
		};

		uint32_t codepoint = 0;
		while(readCodepoint(codepoint)) {
			if(codepoint == '\r') {
				continue;
			}
			if(codepoint == '\n') {
				newLine();
				continue;
			}
			if(codepoint == '\t') {
				int advance = tabAdvance;
				if(x > 0) {
					int remainder = x % tabAdvance;
					advance = remainder == 0 ? tabAdvance : tabAdvance - remainder;
				}
				if(hasWrap && x > 0 && x + advance > wrapWidth) {
					newLine();
					advance = tabAdvance;
				}
				x += advance;
				maxX = std::max(maxX, x);
				continue;
			}
			if(codepoint == ' ' && hasWrap && x == 0) {
				continue;
			}

			const WindowsTrueTypeFont::GlyphBitmap& glyph = font.GetGlyph(codepoint);
			if(hasWrap && x > 0 && x + glyph.Advance > wrapWidth) {
				newLine();
			}

			int glyphBounds = glyph.OffsetX + glyph.Width;
			glyphBounds = std::max(glyphBounds, glyph.Advance);
			glyphBounds = std::max(glyphBounds, 0);
			maxX = std::max(maxX, x + glyphBounds);

			x += glyph.Advance;
		}

		maxX = std::max(maxX, x);

		TextSize size;
		size.X = (uint32_t)maxX;
		size.Y = (uint32_t)(y + lineHeight);
		return size;
#else
		uint32_t maxX = 0;
		uint32_t x = 0;
		uint32_t y = 0;
		uint32_t lineHeight = 9;

		auto newLine = [&]() {
			maxX = std::max(x, maxX);
			x = 0;
			y += lineHeight;
			lineHeight = 9;
		};

		for(int i = 0; i < text.size(); i++) {
			unsigned char c = text[i];
			if(c == '\n') {
				maxX = std::max(x, maxX);
				x = 0;
				y += 9;
			} else if(c == '\t') {
				x += _tabSpace - (((x / 8) % _tabSpace)) * 8;
			} else if(c == 0x20) {
				//Space (ignore spaces at the start of a new line, when text wrapping is enabled)
				if(x > 0 || maxWidth == 0) {
					if(maxWidth > 0 && x + 6 > maxWidth) {
						newLine();
					}
					x += 6;
				}
			} else if(c >= 0x80) {
				//8x12 UTF-8 font for Japanese
				int code = (uint8_t)c;
				if(i + 2 < text.size()) {
					code |= ((uint8_t)text[i + 1]) << 8;
					code |= ((uint8_t)text[i + 2]) << 16;
					auto res = _jpFont.find(code);
					if(res != _jpFont.end()) {
						if(maxWidth > 0 && x + 8 > maxWidth) {
							newLine();
						}

						i += 2;
						x += 8;
						lineHeight = 12;
					}
				}
			} else {
				//Variable size font for standard ASCII
				int charWidth = GetCharWidth(c);
				if(maxWidth > 0 && x + charWidth > maxWidth) {
					newLine();
				}
				x += charWidth;
			}
		}

		TextSize size;
		size.X = std::max(x, maxX);
		size.Y = y + lineHeight;
		return size;
#endif
	}
};
