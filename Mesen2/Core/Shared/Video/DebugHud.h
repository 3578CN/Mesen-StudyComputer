#pragma once
#include "pch.h"
#include <cmath>
#include "Utilities/SimpleLock.h"
#include "Shared/SettingTypes.h"
#include "Shared/Video/DrawCommand.h"

class DebugHud
{
private:
	static constexpr size_t MaxCommandCount = 500000;
	vector<unique_ptr<DrawCommand>> _commands;
	atomic<uint32_t> _commandCount;
	SimpleLock _commandLock;
	unordered_map<uint32_t, uint32_t> _drawPixels;
	double _virtualWidth = 0;
	double _virtualHeight = 0;
	double _actualWidth = 0;
	double _actualHeight = 0;
	double _scaleX = 1.0;
	double _scaleY = 1.0;
	double _fontScale = 1.0;

	__forceinline int ScaleCoord(double scale, int value) const
	{
		if(scale == 1.0 || scale == 0.0) {
			return value;
		}
		double scaled = value * scale;
		if(scaled < 0) {
			return (int)std::llround(scaled);
		}
		return (int)std::llround(scaled);
	}

	__forceinline int ScaleLength(double scale, int value) const
	{
		if(scale == 1.0 || scale == 0.0 || value == 0) {
			return value;
		}
		int scaled = (int)std::llround(value * scale);
		if(value > 0 && scaled <= 0) {
			scaled = 1;
		} else if(value < 0 && scaled >= 0) {
			scaled = -1;
		}
		return scaled;
	}

	__forceinline int ScaleMaxWidth(int value) const
	{
		if(value <= 0) {
			return value;
		}
		if(_scaleX == 0.0) {
			return value;
		}
		int scaled = (int)std::llround(value * _scaleX);
		return scaled <= 0 ? 1 : scaled;
	}

public:
	DebugHud();
	~DebugHud();

	bool HasCommands() { return _commandCount > 0; }

	bool Draw(uint32_t* argbBuffer, FrameInfo frameInfo, OverscanDimensions overscan, uint32_t frameNumber, HudScaleFactors scaleFactors, bool clearAndUpdate = false);
	void ClearScreen();
	void SetVirtualResolution(uint32_t virtualWidth, uint32_t virtualHeight, uint32_t actualWidth, uint32_t actualHeight);
	TextSize MeasureString(const string& text, uint32_t maxWidth = 0) const;
	__forceinline double GetScaleX() const { return _scaleX; }
	__forceinline double GetScaleY() const { return _scaleY; }
	__forceinline double GetFontScale() const { return _fontScale; }

	void DrawPixel(int x, int y, int color, int frameCount, int startFrame = -1);
	void DrawLine(int x, int y, int x2, int y2, int color, int frameCount, int startFrame = -1);
	void DrawRectangle(int x, int y, int width, int height, int color, bool fill, int frameCount, int startFrame = -1);
	void DrawString(int x, int y, string text, int color, int backColor, int frameCount, int startFrame = -1, int maxWidth = 0, bool overwritePixels = false);

	__forceinline void AddCommand(unique_ptr<DrawCommand> cmd)
	{
		auto lock = _commandLock.AcquireSafe();
		if(_commands.size() < DebugHud::MaxCommandCount) {
			_commands.push_back(std::move(cmd));
			_commandCount++;
		}
	}
};
