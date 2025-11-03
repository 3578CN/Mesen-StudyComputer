#pragma once
#include "pch.h"

enum class EmulatorShortcut;

enum class ConsoleNotificationType
{
	GameLoaded,
	StateLoaded,
	GameReset,
	GamePaused,
	GameResumed,
	CodeBreak,
	DebuggerResumed,
	PpuFrameDone,
	ResolutionChanged,
	ConfigChanged,
	ExecuteShortcut,
	ReleaseShortcut,
	EmulationStopped,
	BeforeEmulationStop,
	ViewerRefresh,
	EventViewerRefresh,
	MissingFirmware,
	SufamiTurboFilePrompt,
	BeforeGameUnload,
	BeforeGameLoad,
	GameLoadFailed,
	CheatsChanged,
	RequestConfigChange,
	RefreshSoftwareRenderer,
	// 软驱 I/O 开始/结束通知
	FloppyIoStarted,
	FloppyIoStopped,
	// 软盘镜像加载/弹出通知（加载镜像或弹出镜像时发送）
	FloppyLoaded,
	FloppyEjected,
};

struct GameLoadedEventParams
{
	bool IsPaused;
	bool IsPowerCycle;
};

class INotificationListener
{
public:
	virtual void ProcessNotification(ConsoleNotificationType type, void* parameter) = 0;
};

struct ExecuteShortcutParams
{
	EmulatorShortcut Shortcut;
	uint32_t Param;
	void* ParamPtr;
};