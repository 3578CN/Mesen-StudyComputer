#include "pch.h"
#include "Shared/Video/DebugStats.h"
#include "Shared/Video/DebugHud.h"
#include "Shared/Audio/SoundMixer.h"
#include "Shared/Interfaces/IAudioDevice.h"
#include "Shared/Emulator.h"
#include "Shared/RewindManager.h"
#include "Shared/EmuSettings.h"

void DebugStats::DisplayStats(Emulator *emu, double lastFrameTime)
{
	AudioStatistics stats = emu->GetSoundMixer()->GetStatistics();
	AudioConfig audioCfg = emu->GetSettings()->GetAudioConfig();
	DebugHud* hud = emu->GetDebugHud();

	_frameDurations[_frameDurationIndex] = lastFrameTime;
	_frameDurationIndex = (_frameDurationIndex + 1) % 60;

	int startFrame = emu->GetFrameCount();

	hud->DrawRectangle(8, 8, 115, 49, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(8, 8, 115, 49, 0xFFFFFF, false, 1, startFrame);

	hud->DrawString(10, 10, utf8::utf8::encode(L"音频统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);
	hud->DrawString(10, 21, utf8::utf8::encode(L"延迟："), 0xFFFFFF, 0xFF000000, 1, startFrame);

	int color = (stats.AverageLatency > 0 && std::abs(stats.AverageLatency - audioCfg.AudioLatency) > 3) ? 0xFF0000 : 0xFFFFFF;
	std::stringstream ss;
	ss << std::fixed << std::setprecision(2) << stats.AverageLatency << " ms";
	hud->DrawString(54, 21, ss.str(), color, 0xFF000000, 1, startFrame);

	hud->DrawString(10, 30, utf8::utf8::encode(L"下溢次数：") + std::to_string(stats.BufferUnderrunEventCount), 0xFFFFFF, 0xFF000000, 1, startFrame);
	hud->DrawString(10, 39, utf8::utf8::encode(L"缓冲区大小：") + std::to_string(stats.BufferSize / 1024) + "kb", 0xFFFFFF, 0xFF000000, 1, startFrame);
	hud->DrawString(10, 48, utf8::utf8::encode(L"采样率：") + std::to_string((uint32_t)(audioCfg.SampleRate * emu->GetSoundMixer()->GetRateAdjustment())) + "Hz", 0xFFFFFF, 0xFF000000, 1, startFrame);

	hud->DrawRectangle(132, 8, 115, 49, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(132, 8, 115, 49, 0xFFFFFF, false, 1, startFrame);
	hud->DrawString(134, 10, utf8::utf8::encode(L"视频统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);

	double totalDuration = 0;
	for(int i = 0; i < 60; i++) {
		totalDuration += _frameDurations[i];
	}

	ss = std::stringstream();
	ss << utf8::utf8::encode(L"帧率：") << std::fixed << std::setprecision(4) << (1000 / (totalDuration / 60));
	hud->DrawString(134, 21, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);

	ss = std::stringstream();
	ss << utf8::utf8::encode(L"上一帧：") << std::fixed << std::setprecision(2) << lastFrameTime << " ms";
	hud->DrawString(134, 30, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);

	if(emu->GetFrameCount() > 60) {
		_lastFrameMin = std::min(lastFrameTime, _lastFrameMin);
		_lastFrameMax = std::max(lastFrameTime, _lastFrameMax);
	} else {
		_lastFrameMin = 9999;
		_lastFrameMax = 0;
	}

	ss = std::stringstream();
	ss << utf8::utf8::encode(L"最小延迟：") << std::fixed << std::setprecision(2) << ((_lastFrameMin < 9999) ? _lastFrameMin : 0.0) << " ms";
	hud->DrawString(134, 39, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);

	ss = std::stringstream();
	ss << utf8::utf8::encode(L"最大延迟：") << std::fixed << std::setprecision(2) << _lastFrameMax << " ms";
	hud->DrawString(134, 48, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);

	hud->DrawRectangle(129, 59, 122, 32, 0xFFFFFF, false, 1, startFrame);
	hud->DrawRectangle(130, 60, 120, 30, 0x000000, true, 1, startFrame);

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
		hud->DrawLine(130 + i*2, 60 + 50 - duration*2, 130 + i*2 + 2, 60 + 50 - nextDuration*2, lineColor, 1, startFrame);
	}

	hud->DrawRectangle(8, 60, 115, 34, 0x40000000, true, 1, startFrame);
	hud->DrawRectangle(8, 60, 115, 34, 0xFFFFFF, false, 1, startFrame);

	hud->DrawString(10, 62, utf8::utf8::encode(L"杂项统计"), 0xFFFFFF, 0xFF000000, 1, startFrame);

	RewindStats rewindStats = emu->GetRewindManager()->GetStats();
	double memUsage = (double)rewindStats.MemoryUsage / (1024 * 1024);
	ss = std::stringstream();
	ss << utf8::utf8::encode(L"回溯内存：") << std::fixed << std::setprecision(2) << memUsage << " MB";
	hud->DrawString(10, 73, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);

	if(rewindStats.HistoryDuration > 0) {
	ss = std::stringstream();
	ss << utf8::utf8::encode(L"   每分钟：") << std::fixed << std::setprecision(2) << (memUsage * 60 * 60 / rewindStats.HistoryDuration) << " MB";
	hud->DrawString(9, 82, ss.str(), 0xFFFFFF, 0xFF000000, 1, startFrame);
	}
}