/*------------------------------------------------------------------------
名称：BBK BIOS Mapper 源文件
说明：Mapper 171 SubMapperID 1
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-12
备注：
------------------------------------------------------------------------*/
#include "pch.h"
#include "NES/Mappers/StudyComputer/MapperBbk.h"
#include "NES/NesConsole.h"
#include "NES/BaseNesPpu.h"
#include "NES/NesCpu.h"
#include "NES/NesMemoryManager.h"
#include "NES/Mappers/StudyComputer/FloppyDriveController.h"
#include "NES/Mappers/StudyComputer/Bbk_Fd1.h"
#include "Shared/BaseControlManager.h"
#include "NES/Mappers/A12Watcher.h"

// 将 EVRAM（PPU Nametable）整合到 BaseMapper 提供的 _mapperRam 的高位段中。
// EDRAM 原始大小为 512KB，EVRAM 为 32KB。EVRAM 放在 _mapperRam 的偏移位置：
// EVRAM_BASE = 512KB
// 这样 PPU/CPU 指针比较会识别这些内存为 NesMapperRam，便于调试器查看。
static constexpr uint32_t BBK_EDRAM_SIZE = 512 * 1024; // EDRAM 大小
static constexpr uint32_t BBK_EVRAM_SIZE = 32 * 1024;  // EVRAM 大小
static constexpr uint32_t BBK_EVRAM_BASE = BBK_EDRAM_SIZE; // EVRAM 在 _mapperRam 中的起始偏移

void MapperBbk::InitMapper()
{
	// 初始化并注册 BBK 专用的输入设备（键盘/鼠标 FD1）
	if(!_bbkInput) {
		_bbkInput = std::make_shared<BbkFd1>(_console->GetEmulator());
		// 作为系统控制设备注册，使其能接收输入并在 MapperInputPort 上工作
		_console->GetControlManager()->AddSystemControlDevice(_bbkInput);
		// 立即从设置注入当前的 MapperInput 按键映射，确保设备在运行时可用
		auto mappings = _console->GetEmulator()->GetSettings()->GetNesConfig().MapperInput.Keys.GetKeyMappingArray();
		// 如果用户没有通过 UI 绑定任何 MapperInput 按键，构造一个合理的默认映射
		if(mappings.empty()) {
			KeyMapping def;
			// 显式为每个 CustomKeys 槽赋值，使用 KeyManager::GetKeyCode("Name") 风格，按 Bbk_Fd1::Buttons 顺序
			def.CustomKeys[0] = KeyManager::GetKeyCode("4");
			def.CustomKeys[1] = KeyManager::GetKeyCode("G");
			def.CustomKeys[2] = KeyManager::GetKeyCode("F");
			def.CustomKeys[3] = KeyManager::GetKeyCode("C");
			def.CustomKeys[4] = KeyManager::GetKeyCode("F2");
			def.CustomKeys[5] = KeyManager::GetKeyCode("E");
			def.CustomKeys[6] = KeyManager::GetKeyCode("5");
			def.CustomKeys[7] = KeyManager::GetKeyCode("V");

			def.CustomKeys[8] = KeyManager::GetKeyCode("2");
			def.CustomKeys[9] = KeyManager::GetKeyCode("D");
			def.CustomKeys[10] = KeyManager::GetKeyCode("S");
			def.CustomKeys[11] = KeyManager::GetKeyCode("End");
			def.CustomKeys[12] = KeyManager::GetKeyCode("F1");
			def.CustomKeys[13] = KeyManager::GetKeyCode("W");
			def.CustomKeys[14] = KeyManager::GetKeyCode("3");
			def.CustomKeys[15] = KeyManager::GetKeyCode("X");

			def.CustomKeys[16] = KeyManager::GetKeyCode("Insert");
			def.CustomKeys[17] = KeyManager::GetKeyCode("Backspace");
			def.CustomKeys[18] = KeyManager::GetKeyCode("Page Down");
			def.CustomKeys[19] = KeyManager::GetKeyCode("Right Arrow");
			def.CustomKeys[20] = KeyManager::GetKeyCode("F8");
			def.CustomKeys[21] = KeyManager::GetKeyCode("Page Up");
			def.CustomKeys[22] = KeyManager::GetKeyCode("Delete");
			def.CustomKeys[23] = KeyManager::GetKeyCode("Home");

			def.CustomKeys[24] = KeyManager::GetKeyCode("9");
			def.CustomKeys[25] = KeyManager::GetKeyCode("I");
			def.CustomKeys[26] = KeyManager::GetKeyCode("L");
			def.CustomKeys[27] = KeyManager::GetKeyCode(",");
			def.CustomKeys[28] = KeyManager::GetKeyCode("F5");
			def.CustomKeys[29] = KeyManager::GetKeyCode("O");
			def.CustomKeys[30] = KeyManager::GetKeyCode("0");
			def.CustomKeys[31] = KeyManager::GetKeyCode(".");

			def.CustomKeys[32] = KeyManager::GetKeyCode("]");
			def.CustomKeys[33] = KeyManager::GetKeyCode("Enter");
			def.CustomKeys[34] = KeyManager::GetKeyCode("Up Arrow");
			def.CustomKeys[35] = KeyManager::GetKeyCode("Left Arrow");
			def.CustomKeys[36] = KeyManager::GetKeyCode("F7");
			def.CustomKeys[37] = KeyManager::GetKeyCode("[");
			def.CustomKeys[38] = KeyManager::GetKeyCode("\\");
			def.CustomKeys[39] = KeyManager::GetKeyCode("Down Arrow");

			def.CustomKeys[40] = KeyManager::GetKeyCode("Q");
			def.CustomKeys[41] = KeyManager::GetKeyCode("Caps Lock");
			def.CustomKeys[42] = KeyManager::GetKeyCode("Z");
			def.CustomKeys[43] = KeyManager::GetKeyCode("Tab");
			def.CustomKeys[44] = KeyManager::GetKeyCode("Esc");
			def.CustomKeys[45] = KeyManager::GetKeyCode("A");
			def.CustomKeys[46] = KeyManager::GetKeyCode("1");
			def.CustomKeys[47] = KeyManager::GetKeyCode("Left Ctrl");

			def.CustomKeys[48] = KeyManager::GetKeyCode("7");
			def.CustomKeys[49] = KeyManager::GetKeyCode("Y");
			def.CustomKeys[50] = KeyManager::GetKeyCode("K");
			def.CustomKeys[51] = KeyManager::GetKeyCode("M");
			def.CustomKeys[52] = KeyManager::GetKeyCode("F4");
			def.CustomKeys[53] = KeyManager::GetKeyCode("U");
			def.CustomKeys[54] = KeyManager::GetKeyCode("8");
			def.CustomKeys[55] = KeyManager::GetKeyCode("J");

			def.CustomKeys[56] = KeyManager::GetKeyCode("-");
			def.CustomKeys[57] = KeyManager::GetKeyCode(";");
			def.CustomKeys[58] = KeyManager::GetKeyCode("'");
			def.CustomKeys[59] = KeyManager::GetKeyCode("/");
			def.CustomKeys[60] = KeyManager::GetKeyCode("F6");
			def.CustomKeys[61] = KeyManager::GetKeyCode("P");
			def.CustomKeys[62] = KeyManager::GetKeyCode("=");
			def.CustomKeys[63] = KeyManager::GetKeyCode("Left Shift");
			def.CustomKeys[64] = KeyManager::GetKeyCode("Right Shift");

			def.CustomKeys[65] = KeyManager::GetKeyCode("T");
			def.CustomKeys[66] = KeyManager::GetKeyCode("H");
			def.CustomKeys[67] = KeyManager::GetKeyCode("N");
			def.CustomKeys[68] = KeyManager::GetKeyCode("Space");
			def.CustomKeys[69] = KeyManager::GetKeyCode("F3");
			def.CustomKeys[70] = KeyManager::GetKeyCode("R");
			def.CustomKeys[71] = KeyManager::GetKeyCode("6");
			def.CustomKeys[72] = KeyManager::GetKeyCode("B");

			def.CustomKeys[73] = KeyManager::GetKeyCode("F11");
			def.CustomKeys[74] = KeyManager::GetKeyCode("F12");
			def.CustomKeys[75] = KeyManager::GetKeyCode("Numpad -");
			def.CustomKeys[76] = KeyManager::GetKeyCode("Numpad +");
			def.CustomKeys[77] = KeyManager::GetKeyCode("Numpad *");
			def.CustomKeys[78] = KeyManager::GetKeyCode("F10");
			def.CustomKeys[79] = KeyManager::GetKeyCode("Numpad /");
			def.CustomKeys[80] = KeyManager::GetKeyCode("Num Lock");

			def.CustomKeys[81] = KeyManager::GetKeyCode("`");
			def.CustomKeys[82] = KeyManager::GetKeyCode("Left Alt");
			def.CustomKeys[83] = KeyManager::GetKeyCode("F9");
			def.CustomKeys[84] = KeyManager::GetKeyCode("Numpad .");
			mappings.push_back(def);
		}
		_bbkInput->SetKeyMappings(mappings);
	}
}

// 描述：重置 Mapper 状态并更新映射表。
// 参数：softReset 指示是否为软复位。true 表示软复位，false 表示第一次上电或者硬复位。
void MapperBbk::Reset(bool softReset)
{
	if(!softReset) {
		// 使用 BaseMapper 分配并注册的 mapper RAM（包含 EDRAM + EVRAM）
		memset(_mapperRam, 0, _mapperRamSize);
		// 清零 EVRAM 段（位于 _mapperRam 的高位）
		memset(_mapperRam + BBK_EVRAM_BASE, 0, BBK_EVRAM_SIZE);

		// ---- CPU 映射（PRG / EDRAM / BIOS） ----
		// 4xxx - 5xxx  -> mapper RAM + 0x78000  (8KB)
		SetCpuMemoryMapping(0x4000, 0x5FFF, _mapperRam + 0x78000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
		// 6xxx - 7xxx  -> mapper RAM + 0x7A000  (8KB)
		SetCpuMemoryMapping(0x6000, 0x7FFF, _mapperRam + 0x7A000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
		// 8xxx - 9xxx  -> mapper RAM + 0x0000  (8KB)
		SetCpuMemoryMapping(0x8000, 0x9FFF, _mapperRam + 0x0000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
		// Axxx - Bxxx  -> mapper RAM + 0x2000  (8KB)
		SetCpuMemoryMapping(0xA000, 0xBFFF, _mapperRam + 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);

		// 将 CPU 地址 0xC000-0xDFFF 映射到 PRG ROM 的偏移 0x1C000 处（8KB，作为 ROM 读取）
		SetCpuMemoryMapping(0xC000, 0xDFFF, _prgRom + 0x1C000, 0, _prgSize, MemoryAccessType::Read);
		// 将 CPU 地址 0xE000-0xFFFF 映射到 PRG ROM 的偏移 0x1E000 处（8KB，作为 ROM 读取）
		SetCpuMemoryMapping(0xE000, 0xFFFF, _prgRom + 0x1E000, 0, _prgSize, MemoryAccessType::Read);

		// Nametables（使用整合到 _mapperRam 的 EVRAM 段）
		SetPpuMemoryMapping(0x2000, 0x23FF, _mapperRam, BBK_EVRAM_BASE + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
		SetPpuMemoryMapping(0x2400, 0x27FF, _mapperRam, BBK_EVRAM_BASE + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);
		SetPpuMemoryMapping(0x2800, 0x2BFF, _mapperRam, BBK_EVRAM_BASE + 0x0800, _mapperRamSize, MemoryAccessType::ReadWrite);
		SetPpuMemoryMapping(0x2C00, 0x2FFF, _mapperRam, BBK_EVRAM_BASE + 0x0C00, _mapperRamSize, MemoryAccessType::ReadWrite);

		// 初始化 Mapper 内部寄存器/状态（参考 VirtuaNES 的 MapperBBK::Reset）
		// 保证上电/硬复位时各寄存器与队列处于已知状态
		bMapRam = false;
		nRegFF14 = 0;
		nRegFF1C = 0;
		nRegFF24 = 0;
		nRegFF2C = 0;
		nRegSPInt = 0;

		bRomSel_89AB = 0;

		bSplitMode = 0;
		bEnableIRQ = false;

		nLineCount = 0;

		nNrOfSR = 0;
		nNrOfVR = 0;
		memset(QueueSR, 0, sizeof(QueueSR));
		memset(QueueVR, 0, sizeof(QueueVR));

		nQIndex = 0;
		nCurScanLine = 0;
		bDiskAccess = false;

	}
}

// 描述：将 CPU 地址映射到物理 EDRAM/BIOS/IO 地址。
// 参数：addr - CPU 地址；cs_type - 输出芯片选择；is_write - 是否写操作。
// 返回：物理地址（uint32_t），并在 cs_type 中填入 INNO_CS_* 值。
uint32_t MapperBbk::MapAddr(uint16_t addr, int* cs_type, bool is_write)
{
	uint32_t phy_addr = 0;
	int type = INNO_CS_NONE;

	switch(addr >> 13) {
		case 1: // 0x2000-0x3FFF (handled as 0x4000-0x5FFF / 0x6000-0x7FFF equivalents in original)
			if(!(addr & 2) && bMapRam && !bFF01_D4 && is_write) {
				type = INNO_CS_DRAM;
				phy_addr = 0x7A000 + (addr & 0x1FFF);
			}
			break;
		case 2: // 0x4000-0x5FFF -> handled as 0x8000-0x9FFF / 0xA000-BFFF in original
		case 3: // 0x6000-0x7FFF
			type = INNO_CS_DRAM;
			phy_addr = 0x78000 + (addr & 0x3FFF);
			break;
		case 4: // 0x8000-0x9FFF
			if(nRegFF14 & 0x40) {
				type = INNO_CS_DRAM;
				phy_addr = (nRegFF14 & 0x3F) * 0x2000 + (addr & 0x1FFF);
			} else if(!is_write) {
				type = INNO_CS_ROM;
				phy_addr = (nRegFF14 & 0xF) * 0x2000 + (addr & 0x1FFF);
			}
			break;
		case 5: // 0xA000-0xBFFF
			if(nRegFF14 & 0x40) {
				type = INNO_CS_DRAM;
				phy_addr = (nRegFF1C & 0x3F) * 0x2000 + (addr & 0x1FFF);
			} else if(!is_write) {
				type = INNO_CS_ROM;
				phy_addr = (nRegFF1C & 0xF) * 0x2000 + (addr & 0x1FFF);
			}
			break;
		case 6: // 0xC000-0xDFFF
			if(bMapRam) {
				type = INNO_CS_DRAM;
				phy_addr = (nRegFF24 & 0x3F) * 0x2000 + (addr & 0x1FFF);
			} else if(!is_write) {
				type = INNO_CS_ROM;
				phy_addr = 0x1C000 + (addr & 0x1FFF);
			}
			break;
		case 7: // 0xE000-0xFFFF
			if(addr < 0xFF00) {
				if(bMapRam) {
					type = INNO_CS_DRAM;
					phy_addr = (nRegFF2C & 0x3F) * 0x2000 + (addr & 0x1FFF);
				} else if(!is_write) {
					type = INNO_CS_ROM;
					phy_addr = 0x1E000 + (addr & 0x1FFF);
				}
			} else if(is_write) {
				// IO 写入：Holtek
				type = INNO_CS_HOLTEK;
				phy_addr = addr & 0xFF;
			} else if(bMapRam) {
				type = INNO_CS_DRAM;
				phy_addr = (nRegFF2C & 0x3F) * 0x2000 + (addr & 0x1FFF);
			} else if((addr & 7) == 0) {
				// Read IO
				type = INNO_CS_HOLTEK;
				phy_addr = addr & 0xFF;
			} else {
				type = INNO_CS_ROM;
				phy_addr = 0x1E000 + (addr & 0x1FFF);
			}
			break;
		default:
			type = INNO_CS_NONE;
			break;
	}

	if(cs_type)
		*cs_type = type;

	return phy_addr;
}

void MapperBbk::WriteLow(uint16_t addr, uint8_t value)
{
	// 写低: 0x2000-0x3FFF, 0x4100-0x7FFF
	if(addr < 0x4000) {
		if((addr & 2) == 0 && bMapRam && !bFF01_D4) {
			int offset = addr & 0x1FFF;
			// 6xxx-7xxx 映射到 EDRAM + 0x7A000
			_mapperRam[0x7A000 + offset] = value;
		}
	} else if(addr >= 0x4400) {
		int bank = addr >> 13;      // 2 -> 0x4000-0x5FFF, 3 -> 0x6000-0x7FFF
		int offset = addr & 0x1FFF; // 8K 偏移

		if(bank == 2) {
			_mapperRam[0x78000 + offset] = value;
		} else if(bank == 3) {
			_mapperRam[0x7A000 + offset] = value;
		}
	}
}

uint8_t MapperBbk::ReadLow(uint16_t addr)
{
	// 读低: 0x4100 - 0x7FFF
	if(addr < 0x4400)
		return (addr >> 8); // empty

	int bank = addr >> 13;       // 0..7, 在这里应为 2 或 3
	int offset = addr & 0x1FFF;  // 8K 偏移

	// 4xxx-5xxx (bank 2) 映射到 EDRAM + 0x78000
	// 6xxx-7xxx (bank 3) 映射到 EDRAM + 0x7A000
	if(bank == 2)
		return _mapperRam[0x78000 + offset];
	else // bank == 3
		return _mapperRam[0x7A000 + offset];
}

void MapperBbk::WriteRegister(uint16_t addr, uint8_t value)
{
	// 写高: 0x8000-0xFFFF
	int cs_type;
	uint32_t phy = MapAddr(addr, &cs_type, true);

	// 若映射到 DRAM，直接写入
	if(cs_type == INNO_CS_DRAM) {
		if(phy == 0x7FFFE && value == 0x9B)
			value = 0x96;

		_mapperRam[phy] = value;
		return;
	}

	// FDC 写入区间 0xFF80 - 0xFFB8
	if(addr >= 0xFF80 && addr <= 0xFFB8) {
		unsigned char nPort = (addr >> 3) & 7;

		FloppyDriveController* fdc = _console->GetFdc();
		if(fdc) {
			bDiskAccess = true;
			fdc->Write(nPort, value);
		}

		return;
	}

	bDiskAccess = false;

	// 其余 IO/寄存器
	switch(addr) {
		case 0xFF00: // Keyboard LED
			// D2: !CapsLock, D1: !NumLock - 不在此 mapper 层处理
			break;

		case 0xFF01: // VideoCtrlPort
			// value 的低 2 位决定镜像模式：0=垂直, 1=水平, 2=单屏 4L, 3=单屏 4H
			switch(value & 3) {
				case 0: SetMirroringType(MirroringType::Vertical); break;
				case 1: SetMirroringType(MirroringType::Horizontal); break;
				case 2: SetMirroringType(MirroringType::ScreenAOnly); break;
				case 3: SetMirroringType(MirroringType::ScreenBOnly); break;
			}

			bSplitMode = (value & 0x40) ? 1 : 0;
			bEnableIRQ = (value & 4) ? true : false;

			// 检查 IRQ 状态
			CheckIRQ();

			// 更新 FF01 D4 标志
			bFF01_D4 = !!(value & 0x10);

			// C000-FFFF 到 EDRAM/ROM 的映射
			if(value & 8) {
				// map C000-FFFF 到 mapper RAM
				SetCpuMemoryMapping(0xC000, 0xDFFF, _mapperRam + nRegFF24 * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetCpuMemoryMapping(0xE000, 0xFFFF, _mapperRam + nRegFF2C * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
				bMapRam = true;
			} else {
				// map C000-FFFF 到 ROM
				SetCpuMemoryMapping(0xC000, 0xDFFF, _prgRom + 0x1C000, 0, _prgSize, MemoryAccessType::Read);
				SetCpuMemoryMapping(0xE000, 0xFFFF, _prgRom + 0x1E000, 0, _prgSize, MemoryAccessType::Read);
				bMapRam = false;
			}
			break;

		case 0xFF02: // IntCountPortL
			nLineCount = value;
			break;

		case 0xFF06: // IntCountPortH
			nLineCount &= 0x0F;
			nLineCount |= (value & 0x0F) << 4;
			break;

		case 0xFF04: // DRAMPagePort
			nRegFF14 = (value << 1) & 0x7F;
			nRegFF1C = ((value << 1) & 0x3F) | 1;

			bRomSel_89AB = !(value & 0x20);

			if(bRomSel_89AB) {
				// ROM
				value &= 0x07;	//  128K
				SetCpuMemoryMapping(0x8000, 0x9FFF, _prgRom + value * 0x4000 + 0x0000, 0, _prgSize, MemoryAccessType::Read);
				SetCpuMemoryMapping(0xA000, 0xBFFF, _prgRom + value * 0x4000 + 0x2000, 0, _prgSize, MemoryAccessType::Read);
			} else {
				// DRAM
				value &= 0x1F; // 512K
				SetCpuMemoryMapping(0x8000, 0x9FFF, _mapperRam + value * 0x4000 + 0x0000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetCpuMemoryMapping(0xA000, 0xBFFF, _mapperRam + value * 0x4000 + 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
			}
			break;

		case 0xFF14:
			value &= 0x3F;
			nRegFF14 = value | 0x40;
			bRomSel_89AB = 0;
			SetCpuMemoryMapping(0x8000, 0x9FFF, _mapperRam + value * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF1C:
			nRegFF1C = value & 0x3F;
			bRomSel_89AB = 0;
			SetCpuMemoryMapping(0xA000, 0xBFFF, _mapperRam + nRegFF1C * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF24:
			nRegFF24 = value & 0x3F;
			if(bMapRam)
				SetCpuMemoryMapping(0xC000, 0xDFFF, _mapperRam + nRegFF24 * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF2C:
			nRegFF2C = value & 0x3F;
			if(bMapRam)
				SetCpuMemoryMapping(0xE000, 0xFFFF, _mapperRam + nRegFF2C * 0x2000, 0, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF12: // Init
			if(!bEnableIRQ) {
				nNrOfSR = 0;
				nNrOfVR = 0;

				if(bSplitMode)
					nQIndex = 0;
			}
			CheckIRQ();
			break;

		case 0xFF0A: // Scanline
			QueueSR[nNrOfSR] = value;
			if(bEnableIRQ)
				nNrOfSR = 0;
			else
				nNrOfSR++;
			CheckIRQ();
			break;

		case 0xFF1A: // VideoBank
			QueueVR[nNrOfVR] = value;
			if(bEnableIRQ)
				nNrOfVR = 0;
			else
				nNrOfVR++;
			CheckIRQ();
			break;

		case 0xFF22: // Start
			if(bSplitMode) {
				uint8_t page;

				if(!bEnableIRQ) {
					nQIndex = 0;
					nLineCount = 0;
				}

				nLineCount = QueueSR[nQIndex];

				page = QueueVR[nQIndex] & 15;
				// PPU 0x0000-0x0FFF 区域映射到 _mapperRam 的 EVRAM 段
				SetPpuMemoryMapping(0x0000, 0x03FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x0400, 0x07FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

				page = (QueueVR[nQIndex] >> 4) & 15;
				SetPpuMemoryMapping(0x0800, 0x0BFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x0C00, 0x0FFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

				nQIndex++;
			}
			CheckIRQ();
			break;

		case 0xFF03: // VideoDataPort0
			value &= 0x0F;
			SetPpuMemoryMapping(0x0000, 0x03FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x0400, 0x07FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF0B: // VideoDataPort1
			value &= 0x0F;
			SetPpuMemoryMapping(0x0800, 0x0BFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x0C00, 0x0FFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF13: // VideoDataPort2 (2K)
			value &= 0x0F; // 只取低 4 位作为 2K 页面索引
			SetPpuMemoryMapping(0x1000, 0x13FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x1400, 0x17FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

		case 0xFF1B: // VideoDataPort3 (2K)
			value &= 0x0F; // 只取低 4 位作为 2K 页面索引
			SetPpuMemoryMapping(0x1800, 0x1BFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x1C00, 0x1FFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);
			break;

			// FF23/2B/33/... 映射 1K 页面
		case 0xFF23: value &= 0x1F; SetPpuMemoryMapping(0x0000, 0x03FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF2B: value &= 0x1F; SetPpuMemoryMapping(0x0400, 0x07FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF33: value &= 0x1F; SetPpuMemoryMapping(0x0800, 0x0BFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF3B: value &= 0x1F; SetPpuMemoryMapping(0x0C00, 0x0FFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF43: value &= 0x1F; SetPpuMemoryMapping(0x1000, 0x13FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF4B: value &= 0x1F; SetPpuMemoryMapping(0x1400, 0x17FF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF53: value &= 0x1F; SetPpuMemoryMapping(0x1800, 0x1BFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;
		case 0xFF5B: value &= 0x1F; SetPpuMemoryMapping(0x1C00, 0x1FFF, _mapperRam, BBK_EVRAM_BASE + value * 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite); break;

		case 0xFF10: // SoundPort0/SpeakInitPort
			if(0 == nRegSPInt && (value & 1)) {
			}
			nRegSPInt = value & 1;
			break;

		case 0xFF18: // SoundPort1/SpeakDataPort - no LPC queue in this port, 简化实现
			// 如果实现了扬声器缓冲可在此写入。这里忽略。
			break;

		case 0xFF40: // PCDaCtPortO/PCCDataPort
			// 发送到 PC 打印机端口（若存在）
			// Mesen2 没有统一 lpt 接口在此处，保留空实现
			break;

		case 0xFF48:
			// PC 状态端口读取/写入不在此处处理
			value = value;
			break;

		case 0xFF50:
			// PC 控制端口，处理行脉冲等
			break;

		default:
			break;
	}

}

uint8_t MapperBbk::ReadRegister(uint16_t addr)
{
	// 读高: 0x8000 - 0xFFFF
	uint8_t data = 0;
	int cs_type;
	uint32_t phy_addr;

	phy_addr = MapAddr(addr, &cs_type, false);

	if(INNO_CS_ROM == cs_type) {
		return _prgRom[phy_addr];
	} else if(INNO_CS_DRAM == cs_type) {
		return _mapperRam[phy_addr];
	}

	// FDC 读取区间 0xFF80 - 0xFFB8
	if(addr >= 0xFF80 && addr <= 0xFFB8) {
		unsigned char nPort = (addr >> 3) & 7;

		FloppyDriveController* fdc = _console->GetFdc();
		if(fdc) {
			data = fdc->Read(nPort);
			bDiskAccess = true;
		} else {
			data = 0;
		}

		return data;
	}

	bDiskAccess = 0;

	switch(addr) {
		case 0xFF40:
			return 0;
		case 0xFF48:
		{
			unsigned status = 0;
			// Mesen2 中未实现 lpt 接口的细节，尽量返回 0
			data = (uint8_t)status;
			return data;
		}
		case 0xFF50:
			return 0; // PC Card
		case 0xFF18:
			return 0;
		default:
			break;
	}
	return 0;
}

// 描述：检查并触发/清除 Mapper IRQ。
// 返回：如果触发 IRQ 返回 true，否则返回 false。
bool MapperBbk::CheckIRQ()
{
	// 如果计数达到 254 并处于分帧模式，若已到最后队列并允许 IRQ，则触发
	if(254 == nLineCount && bSplitMode) {
		if(nQIndex == nNrOfSR) {
			if(bEnableIRQ) {
				_console->GetCpu()->SetIrqSource(IRQSource::External);
				return true;
			}
		}
	}

	// 非分帧模式下，当计数为 254 且允许 IRQ 时触发
	if(254 == nLineCount && !bSplitMode) {
		if(bEnableIRQ) {
			_console->GetCpu()->SetIrqSource(IRQSource::External);
			return true;
		}
	}

	// 否则清除外部 IRQ 源
	_console->GetCpu()->ClearIrqSource(IRQSource::External);

	return false;
}

// 描述：基于 PPU A12 上升沿处理 BBK 的行计数与分帧映射切换。
// 在 PPU VRAM 地址变化时由 BaseMapper 调用（需 EnableVramAddressHook 返回 true）。
void MapperBbk::NotifyVramAddressChange(uint16_t addr)
{
	uint32_t frameCycle = _console->GetPpu()->GetFrameCycle();
	if(_a12Watcher.UpdateVramAddress(addr, _console->GetPpu()->GetFrameCycle()) == A12StateChange::Rise) {
		int curScanline = int(frameCycle / 341) - 1;
		if(0 == curScanline && bSplitMode) {
			uint8_t page;

			nQIndex = 0;
			nLineCount = QueueSR[nQIndex];

			page = QueueVR[nQIndex] & 15; 	// 2K
			SetPpuMemoryMapping(0x0000, 0x03FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x0400, 0x07FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

			page = (QueueVR[nQIndex] >> 4) & 15;
			SetPpuMemoryMapping(0x0800, 0x0BFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
			SetPpuMemoryMapping(0x0C00, 0x0FFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

			nQIndex++;
		}

		if(curScanline >= 240)
			return;

		CheckIRQ();

		if(255 == nLineCount) {
			if(nQIndex != nNrOfSR) {
				uint8_t page;

				nLineCount = QueueSR[nQIndex];

				page = QueueVR[nQIndex] & 15; 	// 2K
				SetPpuMemoryMapping(0x0000, 0x03FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x0400, 0x07FF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

				page = (QueueVR[nQIndex] >> 4) & 15;
				SetPpuMemoryMapping(0x0800, 0x0BFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0000, _mapperRamSize, MemoryAccessType::ReadWrite);
				SetPpuMemoryMapping(0x0C00, 0x0FFF, _mapperRam, BBK_EVRAM_BASE + page * 0x0800 + 0x0400, _mapperRamSize, MemoryAccessType::ReadWrite);

				nQIndex++;
			}
		} else if(bSplitMode || bEnableIRQ) {
			nLineCount++;
		}
	}
}