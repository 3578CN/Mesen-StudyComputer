/*------------------------------------------------------------------------
名称：BBK BIOS Mapper 头文件
说明：
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-12
备注：
------------------------------------------------------------------------*/
#pragma once

#include "pch.h"
#include <array>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include "NES/BaseMapper.h"
#include "NES/Mappers/StudyComputer/Bbk_Fd1.h"
// A12 事件监视器，用于检测 PPU VRAM 地址的 A12 上升沿
#include "NES/Mappers/A12Watcher.h"

class MapperBbk final : public BaseMapper
{
public:
	void Reset(bool softReset) override;
	~MapperBbk() override;

protected:
	uint16_t GetPrgPageSize() override { return 0x8000; }
	uint16_t GetChrPageSize() override { return 0x2000; }

	// 使用 BaseMapper 提供的 mapper RAM（包含 EDRAM + EVRAM），由 BaseMapper 在初始化时分配并注册
	// 这里返回大小：EDRAM 512KB + EVRAM 32KB
	uint32_t GetMapperRamSize() override { return (512 + 32) * 1024; }

	void InitMapper() override;
	bool EnableCpuClockHook() override { return true; }

	bool AllowLowReadWrite() override { return true; }
	void WriteLow(uint16_t addr, uint8_t value) override;
	uint8_t ReadLow(uint16_t addr) override;

	bool AllowRegisterRead() override { return true; };
	void WriteRegister(uint16_t addr, uint8_t value) override;
	uint8_t ReadRegister(uint16_t addr) override;
	void ProcessCpuClock() override;

	// 使能 VRAM 地址钩子，以便接收 PPU 地址变化（A12 事件）
	bool EnableVramAddressHook() override { return true; }

	// 接收 PPU VRAM 地址变化通知（用于 A12 上升沿检测）
	void NotifyVramAddressChange(uint16_t addr) override;

	// A12 监视器：用于基于 VRAM 地址的 A12 上升沿检测
	A12Watcher _a12Watcher;

	// Holtek/Inno IRQ 检查：根据 nLineCount / bSplitMode / bEnableIRQ 等状态决定是否向 CPU 触发或清除外部 IRQ。
	// 返回：如果已触发 IRQ 则返回 true，否则返回 false。
	bool CheckIRQ();

	// ---- BBK 内部寄存器/状态 ----
	// 描述：控制 ROM/DRAM 映射与 IO 行为的寄存器和标志。
	bool    bMapRam = false;    // FF01 D3
	bool    bFF01_D4 = false;   // FF01 D4
	uint8_t bRomSel_89AB = 0;
	uint8_t nRegFF14 = 0; // 7 bit
	uint8_t nRegFF1C = 0; // 6 bit
	uint8_t nRegFF24 = 0; // 6 bit
	uint8_t nRegFF2C = 0; // 6 bit
	uint8_t nRegSPInt = 0; // 1bit

	// holtek asic 状态
	int     bSplitMode = 0;
	bool    bEnableIRQ = false;
	int     nLineCount = 0;

	int     nNrOfSR = 0;
	int     nNrOfVR = 0;
	uint8_t QueueSR[32] = { 0 };
	uint8_t QueueVR[32] = { 0 };

	int     nQIndex = 0;
	int     nCurScanLine = 0;
	bool    bDiskAccess = false;

	// MapAddr 返回类型常量
	enum InnoCsType
	{
		INNO_CS_NONE = 0,
		INNO_CS_ROM = 1,
		INNO_CS_DRAM = 2,
		INNO_CS_HOLTEK = 3
	};

	// 将 CPU 地址映射到物理 PRG/EDRAM/IO 地址并返回类型。
	// 参数：addr - CPU 地址；cs_type - 输出芯片选择类型；is_write - 是否写访问。
	uint32_t MapAddr(uint16_t addr, int* cs_type, bool is_write);

	// Mapper 所属的 BBK FD1 输入设备（作为 Mapper 附属的输入设备）
	std::shared_ptr<BbkFd1> _bbkInput;

	// LPC 解码线程处理
	static int LpcFeed(void* host, unsigned char* food);
	void LpcThreadRoutine();
	void InitializeLpcAudio();
	void ShutdownLpcAudio();
	void ResetLpcAudioState();
	/// 描述：仅请求重置 LPC 解码器，不清空 PCM 队列与输入缓冲，避免语音被截断。
	/// 说明：对应 VirtuaNES 在 0xFF10 上升沿仅复位解码器的行为，保持已生成样本继续播放。
	void RequestLpcDecoderReset();
	void UpdateLpcSampleStep();
	int16_t PopLpcSample();
	void EnqueueLpcByte(uint8_t value);

	static constexpr uint32_t LpcSampleRate = 10000;
	static constexpr size_t LpcDataBufferSize = 16;
	static constexpr size_t LpcPcmMaxSize = 4096;

	std::array<uint8_t, LpcDataBufferSize> _lpcDataBuffer = {};
	size_t _lpcDataReadPos = 0;
	size_t _lpcDataWritePos = 0;
	size_t _lpcDataCount = 0;

	std::mutex _lpcMutex;
	std::condition_variable _lpcCond;
	std::condition_variable _lpcAckCond;
	bool _lpcThreadRunning = false;
	bool _lpcStopRequested = false;
	bool _lpcResetRequested = false;
	bool _lpcResetAck = false;

	void* _lpcSynth = nullptr;
	std::thread _lpcThread;

	std::mutex _pcmMutex;
	std::condition_variable _pcmCond;
	std::deque<int16_t> _lpcPcmQueue;
	int16_t _lpcLastSample = 0;
	int16_t _lpcLastMixedSample = 0;
	double _lpcCycleAccumulator = 0.0;
	double _lpcCyclesPerSample = 1.0;
	ConsoleRegion _lpcCachedRegion = ConsoleRegion::Auto;
};