#include "pch.h"
#include <algorithm>
#include <cmath>
#include "Shared/Video/DebugHud.h"
#include "Shared/Video/DrawCommand.h"
#include "Shared/Video/DrawLineCommand.h"
#include "Shared/Video/DrawPixelCommand.h"
#include "Shared/Video/DrawRectangleCommand.h"
#include "Shared/Video/DrawStringCommand.h"
#include "Shared/Video/DrawScreenBufferCommand.h"

DebugHud::DebugHud()
{
	_commandCount = 0;
	_scaleX = 1.0;
	_scaleY = 1.0;
	_fontScale = 1.0;
}

DebugHud::~DebugHud()
{
	_commandLock.Acquire();
	_commandLock.Release();
}

void DebugHud::ClearScreen()
{
	auto lock = _commandLock.AcquireSafe();
	_commands.clear();
	_drawPixels.clear();
}

void DebugHud::SetVirtualResolution(uint32_t virtualWidth, uint32_t virtualHeight, uint32_t actualWidth, uint32_t actualHeight)
{
	_virtualWidth = virtualWidth;
	_virtualHeight = virtualHeight;
	_actualWidth = actualWidth;
	_actualHeight = actualHeight;

	if(_virtualWidth == 0 || _actualWidth == 0) {
		_scaleX = 1.0;
	} else {
		_scaleX = (double)_actualWidth / (double)_virtualWidth;
	}

	if(_virtualHeight == 0 || _actualHeight == 0) {
		_scaleY = 1.0;
	} else {
		_scaleY = (double)_actualHeight / (double)_virtualHeight;
	}

	_fontScale = _scaleY > 0.0 ? _scaleY : 1.0;
}

TextSize DebugHud::MeasureString(const string& text, uint32_t maxWidth) const
{
	string tempText = text;
	uint32_t scaledWidth = 0;
	if(maxWidth > 0) {
		double converted = maxWidth * _scaleX;
		if(converted < 1.0) {
			converted = 1.0;
		}
		scaledWidth = (uint32_t)std::llround(converted);
	}

	TextSize size = DrawStringCommand::MeasureString(tempText, scaledWidth, _fontScale);
	if(_scaleX != 0) {
		size.X = (uint32_t)std::llround(size.X / _scaleX);
	}
	if(_scaleY != 0) {
		size.Y = (uint32_t)std::llround(size.Y / _scaleY);
	}
	return size;
}

bool DebugHud::Draw(uint32_t* argbBuffer, FrameInfo frameInfo, OverscanDimensions overscan, uint32_t frameNumber, HudScaleFactors scaleFactors, bool clearAndUpdate)
{
	auto lock = _commandLock.AcquireSafe();

	bool isDirty = false;
	if(clearAndUpdate) {
		unordered_map<uint32_t, uint32_t> drawPixels;
		drawPixels.reserve(1000);
		for(unique_ptr<DrawCommand>& command : _commands) {
			command->Draw(&drawPixels, argbBuffer, frameInfo, overscan, frameNumber, scaleFactors);
		}

		isDirty = drawPixels.size() != _drawPixels.size();
		if(!isDirty) {
			for(auto keyValue : drawPixels) {
				auto match = _drawPixels.find(keyValue.first);
				if(match != _drawPixels.end()) {
					if(keyValue.second != match->second) {
						isDirty = true;
						break;
					}
				} else {
					isDirty = true;
					break;
				}
			}
		}

		if(isDirty) {
			memset(argbBuffer, 0, frameInfo.Height * frameInfo.Width * sizeof(uint32_t));
			for(auto keyValue : drawPixels) {
				argbBuffer[keyValue.first] = keyValue.second;
			}
			_drawPixels = drawPixels;
		}
	} else {
		isDirty = true;
		for(unique_ptr<DrawCommand>& command : _commands) {
			command->Draw(nullptr, argbBuffer, frameInfo, overscan, frameNumber, scaleFactors);
		}
	}

	_commands.erase(std::remove_if(_commands.begin(), _commands.end(), [](const unique_ptr<DrawCommand>& c) { return c->Expired(); }), _commands.end());
	_commandCount = (uint32_t)_commands.size();

	return isDirty;
}

void DebugHud::DrawPixel(int x, int y, int color, int frameCount, int startFrame)
{
	int scaledX = ScaleCoord(_scaleX, x);
	int scaledY = ScaleCoord(_scaleY, y);
	AddCommand(unique_ptr<DrawCommand>(new DrawPixelCommand(scaledX, scaledY, color, frameCount, startFrame)));
}

void DebugHud::DrawLine(int x, int y, int x2, int y2, int color, int frameCount, int startFrame)
{
	int scaledX1 = ScaleCoord(_scaleX, x);
	int scaledY1 = ScaleCoord(_scaleY, y);
	int scaledX2 = ScaleCoord(_scaleX, x2);
	int scaledY2 = ScaleCoord(_scaleY, y2);
	AddCommand(unique_ptr<DrawCommand>(new DrawLineCommand(scaledX1, scaledY1, scaledX2, scaledY2, color, frameCount, startFrame)));
}

void DebugHud::DrawRectangle(int x, int y, int width, int height, int color, bool fill, int frameCount, int startFrame)
{
	int adjX = x;
	int adjY = y;
	int adjWidth = width;
	int adjHeight = height;

	if(adjWidth < 0) {
		adjX += adjWidth + 1;
		adjWidth = -adjWidth;
	}
	if(adjHeight < 0) {
		adjY += adjHeight + 1;
		adjHeight = -adjHeight;
	}

	int scaledX = ScaleCoord(_scaleX, adjX);
	int scaledY = ScaleCoord(_scaleY, adjY);
	int scaledWidth = ScaleLength(_scaleX, adjWidth);
	int scaledHeight = ScaleLength(_scaleY, adjHeight);
	AddCommand(unique_ptr<DrawCommand>(new DrawRectangleCommand(scaledX, scaledY, scaledWidth, scaledHeight, color, fill, frameCount, startFrame)));
}

void DebugHud::DrawString(int x, int y, string text, int color, int backColor, int frameCount, int startFrame, int maxWidth, bool overwritePixels)
{
	int scaledX = ScaleCoord(_scaleX, x);
	int scaledY = ScaleCoord(_scaleY, y);
	int scaledMaxWidth = ScaleMaxWidth(maxWidth);
	AddCommand(unique_ptr<DrawCommand>(new DrawStringCommand(scaledX, scaledY, text, color, backColor, frameCount, startFrame, scaledMaxWidth, overwritePixels, _fontScale)));
}
