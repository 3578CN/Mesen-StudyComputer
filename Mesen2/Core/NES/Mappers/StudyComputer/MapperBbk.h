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
#include "NES/BaseMapper.h"
#include "NES/Mappers/StudyComputer/Bbk_Fd1.h"

class MapperBbk final : public BaseMapper
{
public:
	void Reset(bool softReset) override;

protected:
	uint16_t GetPrgPageSize() override { return 0x8000; }
	uint16_t GetChrPageSize() override { return 0x2000; }

	void InitMapper() override;

	bool AllowLowReadWrite() override { return true; }
	void WriteLow(uint16_t addr, uint8_t value) override;
	uint8_t ReadLow(uint16_t addr) override;

	bool AllowRegisterRead() override { return true; };
	void WriteRegister(uint16_t addr, uint8_t value) override;
	uint8_t ReadRegister(uint16_t addr) override;

	// 使能 VRAM 地址钩子，以便接收 PPU 地址变化（A12 事件）
	bool EnableVramAddressHook() override { return true; }

	// 每扫描线回调（由 PPU 调用），用于更新 nCurScanLine
	void HSync(int nScanline) override;

	// Holtek/Inno IRQ 检查：根据 nLineCount / bSplitMode / bEnableIRQ 等状态决定是否向 CPU 触发或清除外部 IRQ。
	// 返回：如果已触发 IRQ 则返回 true，否则返回 false。
	bool CheckIRQ();

	uint8_t EVRAM[32 * 1024];
	uint8_t EDRAM[512 * 1024];

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
};