#include "pch.h"
#include "Shared/Video/DebugStats.h"
#include "Shared/Video/DebugHud.h"
#include "Shared/Video/VideoRenderer.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/Interfaces/IAudioDevice.h"
#include "Shared/Emulator.h"
#include "Shared/RewindManager.h"
#include "Shared/EmuSettings.h"

void DebugStats::DisplayStats(Emulator *emu, double lastFrameTime)
{
	AudioStatistics stats = emu->GetSoundMixer()->GetStatistics();
	AudioConfig audioCfg = emu->GetSettings()->GetAudioConfig();
	DebugHud* rendererHud = nullptr;
	if(emu->GetVideoRenderer()) {
		rendererHud = emu->GetVideoRenderer()->GetRendererHud();
	}
	DebugHud* hud = rendererHud ? rendererHud : emu->GetDebugHud();
	if(!hud) {
		return;
	}

	uint32_t lineHeight = hud->MeasureString(utf8::utf8::encode(L"测")).Y;
	if(lineHeight == 0) {
		lineHeight = 10;
	}
	int lineSpacing = (int)lineHeight + 2;
	const std::string colonText = utf8::utf8::encode(L"：");
	uint32_t colonWidth = hud->MeasureString(colonText).X;
	if(colonWidth == 0) {
		colonWidth = 2;
	}


	struct StatLine {
		std::string Label;
		std::string Value;
		int ValueColor;
	};

	const int audioBoxLeft = 8;
	const int audioBoxTop = 8;
	const int audioBoxWidth = 122;
	const int boxGap = 8;
	const int innerPadding = 3;
	const int audioDataLines = 4;
	int audioBoxHeight = std::max<int>(64, 2 * innerPadding + (int)lineHeight + audioDataLines * lineSpacing);

	const int videoBoxLeft = audioBoxLeft + audioBoxWidth + boxGap;
	const int videoBoxTop = audioBoxTop;
	const int videoBoxWidth = audioBoxWidth;
	const int videoDataLines = 4;
	int videoBoxHeight = std::max<int>(64, 2 * innerPadding + (int)lineHeight + videoDataLines * lineSpacing);

	int topRowHeight = std::max(audioBoxHeight, videoBoxHeight);

	_frameDurations[_frameDurationIndex] = lastFrameTime;
	_frameDurationIndex = (_frameDurationIndex + 1) % 60;

	int startFrame = (hud == rendererHud) ? -1 : emu->GetFrameCount();

	hud->DrawRectangle(audioBoxLeft, audioBoxTop, audioBoxWidth, audioBoxHeight, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(audioBoxLeft, audioBoxTop, audioBoxWidth, audioBoxHeight, 0xFFFFFF, false, 1, startFrame);

	int audioTextY = audioBoxTop + innerPadding;
	int audioTextX = audioBoxLeft + innerPadding;
	hud->DrawString(audioTextX, audioTextY, utf8::utf8::encode(L"音频统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);
	audioTextY += lineSpacing;

	std::stringstream ss;
	int latencyColor = (stats.AverageLatency > 0 && std::abs(stats.AverageLatency - audioCfg.AudioLatency) > 3) ? 0xFF0000 : 0xFFFFFF;
	ss << std::fixed << std::setprecision(2) << stats.AverageLatency << " ms";
	std::string latencyValue = ss.str();
	StatLine audioLines[] = {
		{ utf8::utf8::encode(L"延迟"), latencyValue, latencyColor },
		{ utf8::utf8::encode(L"下溢次数"), std::to_string(stats.BufferUnderrunEventCount), 0xFFFFFF },
		{ utf8::utf8::encode(L"缓冲区大小"), std::to_string(stats.BufferSize / 1024) + "kb", 0xFFFFFF },
		{ utf8::utf8::encode(L"采样率"), std::to_string((uint32_t)(audioCfg.SampleRate * emu->GetSoundMixer()->GetRateAdjustment())) + "Hz", 0xFFFFFF }
	};

	uint32_t audioLabelWidth = 0;
	for(const StatLine& line : audioLines) {
		audioLabelWidth = std::max<uint32_t>(audioLabelWidth, hud->MeasureString(line.Label).X);
	}
	int audioLabelBaseX = audioTextX;
	int audioColonX = audioLabelBaseX + (int)audioLabelWidth;
	int audioValueX = audioColonX + (int)colonWidth;

	int audioLineY = audioTextY;
	for(const StatLine& line : audioLines) {
		uint32_t labelWidth = hud->MeasureString(line.Label).X;
		int labelX = audioColonX - (int)labelWidth;
		hud->DrawString(labelX, audioLineY, line.Label, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(audioColonX, audioLineY, colonText, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(audioValueX, audioLineY, line.Value, line.ValueColor, 0xFF000000, 1, startFrame);
		audioLineY += lineSpacing;
	}

	hud->DrawRectangle(videoBoxLeft, videoBoxTop, videoBoxWidth, videoBoxHeight, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(videoBoxLeft, videoBoxTop, videoBoxWidth, videoBoxHeight, 0xFFFFFF, false, 1, startFrame);

	int videoTextY = videoBoxTop + innerPadding;
	int videoTextX = videoBoxLeft + innerPadding;
	hud->DrawString(videoTextX, videoTextY, utf8::utf8::encode(L"视频统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);
	videoTextY += lineSpacing;

	double totalDuration = 0;
	for(int i = 0; i < 60; i++) {
		totalDuration += _frameDurations[i];
	}

	if(emu->GetFrameCount() > 60) {
		_lastFrameMin = std::min(lastFrameTime, _lastFrameMin);
		_lastFrameMax = std::max(lastFrameTime, _lastFrameMax);
	} else {
		_lastFrameMin = 9999;
		_lastFrameMax = 0;
	}

	ss = std::stringstream();
	ss << std::fixed << std::setprecision(4) << (1000 / (totalDuration / 60));
	std::string frameRateValue = ss.str();
	ss = std::stringstream();
	ss << std::fixed << std::setprecision(2) << lastFrameTime << " ms";
	std::string lastFrameValue = ss.str();
	ss = std::stringstream();
	ss << std::fixed << std::setprecision(2) << ((_lastFrameMin < 9999) ? _lastFrameMin : 0.0) << " ms";
	std::string minLatencyValue = ss.str();
	ss = std::stringstream();
	ss << std::fixed << std::setprecision(2) << _lastFrameMax << " ms";
	std::string maxLatencyValue = ss.str();

	StatLine videoLines[] = {
		{ utf8::utf8::encode(L"帧率"), frameRateValue, 0xFFFFFF },
		{ utf8::utf8::encode(L"上一帧"), lastFrameValue, 0xFFFFFF },
		{ utf8::utf8::encode(L"最小延迟"), minLatencyValue, 0xFFFFFF },
		{ utf8::utf8::encode(L"最大延迟"), maxLatencyValue, 0xFFFFFF }
	};

	uint32_t videoLabelWidth = 0;
	for(const StatLine& line : videoLines) {
		videoLabelWidth = std::max<uint32_t>(videoLabelWidth, hud->MeasureString(line.Label).X);
	}
	int videoLabelBaseX = videoTextX;
	int videoColonX = videoLabelBaseX + (int)videoLabelWidth;
	int videoValueX = videoColonX + (int)colonWidth;

	int videoLineY = videoTextY;
	for(const StatLine& line : videoLines) {
		uint32_t labelWidth = hud->MeasureString(line.Label).X;
		int labelX = videoColonX - (int)labelWidth;
		hud->DrawString(labelX, videoLineY, line.Label, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(videoColonX, videoLineY, colonText, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(videoValueX, videoLineY, line.Value, line.ValueColor, 0xFF000000, 1, startFrame);
		videoLineY += lineSpacing;
	}

	int secondRowTop = videoBoxTop + topRowHeight + 6;
	hud->DrawRectangle(videoBoxLeft, secondRowTop - 1, videoBoxWidth, 34, 0xFFFFFF, false, 1, startFrame);
	hud->DrawRectangle(videoBoxLeft + 1, secondRowTop, videoBoxWidth - 2, 32, 0x000000, true, 1, startFrame);

	double expectedFrameDelay = 1000 / emu->GetFps();

	for(int i = 0; i < 59; i++) {
		double duration = _frameDurations[(_frameDurationIndex + i) % 60];
		double nextDuration = _frameDurations[(_frameDurationIndex + i + 1) % 60];

		duration = std::min(25.0, std::max(10.0, duration));
		nextDuration = std::min(25.0, std::max(10.0, nextDuration));

		int lineColor = 0x00FF00;
		if(std::abs(duration - expectedFrameDelay) > 2) {
			lineColor = 0xFF0000;
		} else if(std::abs(duration - expectedFrameDelay) > 1) {
			lineColor = 0xFFA500;
		}
		int graphBaseX = videoBoxLeft + 1 + i * 2;
		int chartBaseY = secondRowTop + 50;
		hud->DrawLine(graphBaseX, chartBaseY - (int)std::llround(duration * 2), graphBaseX + 2, chartBaseY - (int)std::llround(nextDuration * 2), lineColor, 1, startFrame);
	}

	const int miscBoxLeft = audioBoxLeft;
	const int miscBoxTop = secondRowTop;
	const int miscBoxWidth = audioBoxWidth;
	const int miscDataLines = 2;
	int miscBoxHeight = std::max<int>(40, 2 * innerPadding + (int)lineHeight + miscDataLines * lineSpacing);

	hud->DrawRectangle(miscBoxLeft, miscBoxTop, miscBoxWidth, miscBoxHeight, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(miscBoxLeft, miscBoxTop, miscBoxWidth, miscBoxHeight, 0xFFFFFF, false, 1, startFrame);

	int miscTextY = miscBoxTop + innerPadding;
	int miscTextX = miscBoxLeft + innerPadding;
	hud->DrawString(miscTextX, miscTextY, utf8::utf8::encode(L"杂项统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);
	miscTextY += lineSpacing;

	RewindStats rewindStats = emu->GetRewindManager()->GetStats();
	double memUsage = (double)rewindStats.MemoryUsage / (1024 * 1024);
	ss = std::stringstream();
	ss << std::fixed << std::setprecision(2) << memUsage << " MB";
	StatLine miscLines[2] = {
		{ utf8::utf8::encode(L"回溯内存"), ss.str(), 0xFFFFFF },
		{ utf8::utf8::encode(L"每分钟"), "", 0xFFFFFF }
	};
	int miscLineCount = 1;
	if(rewindStats.HistoryDuration > 0) {
		ss = std::stringstream();
		ss << std::fixed << std::setprecision(2) << (memUsage * 60 * 60 / rewindStats.HistoryDuration) << " MB";
		miscLines[1].Value = ss.str();
		miscLineCount = 2;
	}

	uint32_t miscLabelWidth = 0;
	for(int i = 0; i < miscLineCount; i++) {
		miscLabelWidth = std::max<uint32_t>(miscLabelWidth, hud->MeasureString(miscLines[i].Label).X);
	}
	int miscLabelBaseX = miscTextX;
	int miscColonX = miscLabelBaseX + (int)miscLabelWidth;
	int miscValueX = miscColonX + (int)colonWidth;

	int miscLineY = miscTextY;
	for(int i = 0; i < miscLineCount; i++) {
		uint32_t labelWidth = hud->MeasureString(miscLines[i].Label).X;
		int labelX = miscColonX - (int)labelWidth;
		hud->DrawString(labelX, miscLineY, miscLines[i].Label, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(miscColonX, miscLineY, colonText, 0xFFFFFF, 0xFF000000, 1, startFrame);
		hud->DrawString(miscValueX, miscLineY, miscLines[i].Value, miscLines[i].ValueColor, 0xFF000000, 1, startFrame);
		miscLineY += lineSpacing;
	}
}