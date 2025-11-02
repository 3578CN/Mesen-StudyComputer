#pragma once

#include "pch.h"
#include "NES/INesMemoryHandler.h"
#include "NES/NesTypes.h"
#include "NES/RomData.h"
#include "Debugger/DebugTypes.h"
#include "Shared/Emulator.h"
#include "Shared/MemoryOperationType.h"
#include "Utilities/ISerializable.h"

class NesConsole;
class Epsm;
enum class MemoryType;
struct MapperStateEntry;

class BaseMapper : public INesMemoryHandler, public ISerializable
{
private:
	unique_ptr<Epsm> _epsm;

	MirroringType _mirroringType = {};
	string _batteryFilename;

	uint16_t InternalGetPrgPageSize();
	uint16_t InternalGetSaveRamPageSize();
	uint16_t InternalGetWorkRamPageSize();
	uint16_t InternalGetChrRomPageSize();
	uint16_t InternalGetChrRamPageSize();
	bool ValidateAddressRange(uint16_t startAddr, uint16_t endAddr);

	uint8_t* _nametableRam = nullptr;
	uint8_t _nametableCount = 2;
	uint32_t _ntRamSize = 0;

	uint32_t _internalRamMask = 0x7FF;

	bool _hasBusConflicts = false;
	bool _hasDefaultWorkRam = false;

	bool _hasCustomReadVram = false;
	bool _hasCpuClockHook = false;
	bool _hasVramAddressHook = false;

	bool _allowRegisterRead = false;
	bool _isReadRegisterAddr[0x10000] = {};
	bool _isWriteRegisterAddr[0x10000] = {};

	MemoryAccessType _prgMemoryAccess[0x100] = {};
	uint8_t* _prgPages[0x100] = {};

	MemoryAccessType _chrMemoryAccess[0x100] = {};
	uint8_t* _chrPages[0x100] = {};

	int32_t _prgMemoryOffset[0x100] = {};
	PrgMemoryType _prgMemoryType[0x100] = {};

	int32_t _chrMemoryOffset[0x100] = {};
	ChrMemoryType _chrMemoryType[0x100] = {};

	vector<uint8_t> _originalPrgRom;
	vector<uint8_t> _originalChrRom;

protected:
	// NES ROM 和 mapper 信息
	NesRomInfo _romInfo = {};

	// 指向控制台实例
	NesConsole* _console = nullptr;

	// 指向模拟器实例
	Emulator* _emu = nullptr;

	// 指向 PRG ROM 数据
	uint8_t* _prgRom = nullptr;

	// 指向 CHR ROM 数据
	uint8_t* _chrRom = nullptr;

	// 指向 CHR RAM 数据
	uint8_t* _chrRam = nullptr;

	// PRG ROM 大小（字节）
	uint32_t _prgSize = 0;

	// CHR ROM 大小（字节）
	uint32_t _chrRomSize = 0;

	// CHR RAM 大小（字节）
	uint32_t _chrRamSize = 0;

	// 指向电池保存的 SRAM
	uint8_t* _saveRam = nullptr;

	// 保存 RAM 大小（字节）
	uint32_t _saveRamSize = 0;

	// 工作 RAM 大小（字节）
	uint32_t _workRamSize = 0;

	// 指向工作 RAM
	uint8_t* _workRam = nullptr;

	// CHR 是否使用电池备份
	bool _hasChrBattery = false;

	// Mapper 私有 RAM 指针
	uint8_t* _mapperRam = nullptr;

	// Mapper RAM 大小（字节）
	uint32_t _mapperRamSize = 0;

public:
	// 必须由子类实现的初始化函数
	virtual void InitMapper() = 0;

protected:
	// 可接受 RomData 的初始化函数（可重写）
	virtual void InitMapper(RomData& romData);

	// 获取 PRG 页面大小（必须由子类实现）
	virtual uint16_t GetPrgPageSize() = 0;

	// 获取 CHR 页面大小（必须由子类实现）
	virtual uint16_t GetChrPageSize() = 0;

	// 检查是否为 NES 2.0 格式
	bool IsNes20();

	// 获取 CHR RAM 页面大小（默认等于 CHR 页面大小）
	virtual uint16_t GetChrRamPageSize() { return GetChrPageSize(); }

	// 获取保存 RAM 大小（默认）
	virtual uint32_t GetSaveRamSize() { return 0x2000; }

	// 获取保存 RAM 页面大小（默认）
	virtual uint32_t GetSaveRamPageSize() { return 0x2000; }

	// 是否强制为 CHR 启用电池（默认 false）
	virtual bool ForceChrBattery() { return false; }

	// 是否强制保存 RAM 大小（默认 false）
	virtual bool ForceSaveRamSize() { return false; }

	// 是否强制工作 RAM 大小（默认 false）
	virtual bool ForceWorkRamSize() { return false; }

	// 获取 CHR RAM 大小（默认 0）
	virtual uint32_t GetChrRamSize() { return 0x0000; }

	// 获取工作 RAM 大小（默认）
	virtual uint32_t GetWorkRamSize() { return 0x2000; }

	// 获取工作 RAM 页面大小（默认）
	virtual uint32_t GetWorkRamPageSize() { return 0x2000; }

	// 获取 Mapper RAM 大小（默认 0）
	virtual uint32_t GetMapperRamSize() { return 0; }

	// 寄存器地址范围起始（默认）
	virtual uint16_t RegisterStartAddress() { return 0x8000; }

	// 寄存器地址范围结束（默认）
	virtual uint16_t RegisterEndAddress() { return 0xFFFF; }

	// 是否允许从寄存器读取（默认 false）
	virtual bool AllowRegisterRead() { return false; }

	// 是否启用 CPU 时钟钩子（默认 false）
	virtual bool EnableCpuClockHook() { return false; }

	// 是否启用自定义 VRAM 读取（默认 false）
	virtual bool EnableCustomVramRead() { return false; }

	// 是否启用 VRAM 地址变更钩子（默认 false）
	virtual bool EnableVramAddressHook() { return false; }

	// 获取拨码开关数量（默认 0）
	virtual uint32_t GetDipSwitchCount() { return 0; }

	// 获取 nametable 数量（默认 0）
	virtual uint32_t GetNametableCount() { return 0; }

	// 是否存在总线冲突（默认 false）
	virtual bool HasBusConflicts() { return false; }

	// 从内部 RAM 读取一个字节（内部使用）
	/// @param addr 要读取的内部地址
	/// @return 读取到的字节值
	uint8_t InternalReadRam(uint16_t addr);

	// 写入寄存器（多数 mapper 会重写此方法）
	/// @param addr 寄存器地址
	/// @param value 写入的字节值
	/// @return 无返回值
	virtual void WriteRegister(uint16_t addr, uint8_t value);

	// 读取寄存器（多数 mapper 会重写此方法）
	/// @param addr 寄存器地址
	/// @return 寄存器读取的字节值
	virtual uint8_t ReadRegister(uint16_t addr);

	// 选择 4x PRG 页面映射
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	void SelectPrgPage4x(uint16_t slot, uint16_t page, PrgMemoryType memoryType = PrgMemoryType::PrgRom);

	// 选择 2x PRG 页面映射
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	void SelectPrgPage2x(uint16_t slot, uint16_t page, PrgMemoryType memoryType = PrgMemoryType::PrgRom);

	// 选择单个 PRG 页面（可重写）
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	virtual void SelectPrgPage(uint16_t slot, uint16_t page, PrgMemoryType memoryType = PrgMemoryType::PrgRom);

	// 设置 CPU 内存映射（使用页号）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param pageNumber 页面编号
	/// @param type 页面类型
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetCpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, int16_t pageNumber, PrgMemoryType type, int8_t accessType = -1);

	// 设置 CPU 内存映射（使用源偏移）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param type 页面类型
	/// @param sourceOffset 源偏移
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetCpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, PrgMemoryType type, uint32_t sourceOffset, int8_t accessType);

	// 设置 CPU 内存映射（直接使用源指针）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param source 源内存指针
	/// @param sourceOffset 源偏移
	/// @param sourceSize 源大小
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetCpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, uint8_t* source, uint32_t sourceOffset, uint32_t sourceSize, int8_t accessType = -1);

	// 移除 CPU 内存映射
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @return 无返回值
	void RemoveCpuMemoryMapping(uint16_t startAddr, uint16_t endAddr);

	// 选择 8x CHR 页面映射（可重写）
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	virtual void SelectChrPage8x(uint16_t slot, uint16_t page, ChrMemoryType memoryType = ChrMemoryType::Default);

	// 选择 4x CHR 页面映射（可重写）
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	virtual void SelectChrPage4x(uint16_t slot, uint16_t page, ChrMemoryType memoryType = ChrMemoryType::Default);

	// 选择 2x CHR 页面映射（可重写）
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	virtual void SelectChrPage2x(uint16_t slot, uint16_t page, ChrMemoryType memoryType = ChrMemoryType::Default);

	// 选择单个 CHR 页面（可重写）
	/// @param slot 映射槽索引
	/// @param page 要选择的页面号
	/// @param memoryType 页面类型
	/// @return 无返回值
	virtual void SelectChrPage(uint16_t slot, uint16_t page, ChrMemoryType memoryType = ChrMemoryType::Default);

	// 设置 PPU 内存映射（使用页号）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param pageNumber 页面编号
	/// @param type 页面类型
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetPpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, uint16_t pageNumber, ChrMemoryType type = ChrMemoryType::Default, int8_t accessType = -1);

	// 设置 PPU 内存映射（使用源偏移）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param type 页面类型
	/// @param sourceOffset 源偏移
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetPpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, ChrMemoryType type, uint32_t sourceOffset, int8_t accessType);

	// 设置 PPU 内存映射（使用源指针）
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param sourceMemory 源内存指针
	/// @param sourceOffset 源偏移
	/// @param sourceSize 源大小
	/// @param accessType 访问类型
	/// @return 无返回值
	void SetPpuMemoryMapping(uint16_t startAddr, uint16_t endAddr, uint8_t* sourceMemory, uint32_t sourceOffset, uint32_t sourceSize, int8_t accessType = -1);

	// 移除 PPU 内存映射
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @return 无返回值
	void RemovePpuMemoryMapping(uint16_t startAddr, uint16_t endAddr);

	// 是否含有电池存档
	/// @return 是否含有电池存档
	bool HasBattery();

	// 加载电池存档到内存（可重写）
	/// @return 无返回值
	virtual void LoadBattery();

	// 获取电池存档文件名
	/// @return 电池文件名
	string GetBatteryFilename();

	// 获取 PRG 页面数量
	/// @return PRG 页面数量
	uint32_t GetPrgPageCount();

	// 获取 CHR ROM 页面数量
	/// @return CHR ROM 页面数量
	uint32_t GetChrRomPageCount();

	// 获取上电时的默认字节值（用于未初始化的 RAM）
	/// @param defaultValue 默认字节值
	/// @return 上电缺省字节值
	uint8_t GetPowerOnByte(uint8_t defaultValue = 0);

	// 获取模拟器的拨码开关状态
	/// @return 拨码开关状态位掩码
	uint32_t GetDipSwitches();

	// 初始化默认工作 RAM（不持久化）
	/// @return 无返回值
	void SetupDefaultWorkRam();

	// 初始化 CHR RAM（可指定大小）
	/// @param chrRamSize 指定的 CHR RAM 大小，-1 表示使用默认
	/// @return 无返回值
	void InitializeChrRam(int32_t chrRamSize = -1);

	// 添加寄存器地址范围的监控
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param operation 需要监控的操作类型
	/// @return 无返回值
	void AddRegisterRange(uint16_t startAddr, uint16_t endAddr, MemoryOperation operation = MemoryOperation::Any);

	// 移除寄存器地址范围的监控
	/// @param startAddr 起始地址
	/// @param endAddr 结束地址
	/// @param operation 需要移除的操作类型
	/// @return 无返回值
	void RemoveRegisterRange(uint16_t startAddr, uint16_t endAddr, MemoryOperation operation = MemoryOperation::Any);

	// 序列化 mapper 状态
	/// @param s 序列化器实例
	/// @return 无返回值
	void Serialize(Serializer& s) override;

	// 恢复 PRG/CHR 的状态（在保存/恢复时使用）
	/// @return 无返回值
	void RestorePrgChrState();

	// 在 CPU 时钟处理时执行的基础操作（子类可扩展）
	/// @return 无返回值
	void BaseProcessCpuClock();

	// 获取指定索引的 Nametable 指针
	/// @param nametableIndex Nametable 索引
	/// @return 指向对应 nametable 的指针
	uint8_t* GetNametable(uint8_t nametableIndex);

	// 设置指定索引的 Nametable
	/// @param index 目标 nametable 索引
	/// @param nametableIndex 要映射的 nametable 索引
	/// @return 无返回值
	void SetNametable(uint8_t index, uint8_t nametableIndex);

	// 设置所有四个 Nametable 的索引
	/// @param nametable1Index 第一个 nametable 索引
	/// @param nametable2Index 第二个 nametable 索引
	/// @param nametable3Index 第三个 nametable 索引
	/// @param nametable4Index 第四个 nametable 索引
	/// @return 无返回值
	void SetNametables(uint8_t nametable1Index, uint8_t nametable2Index, uint8_t nametable3Index, uint8_t nametable4Index);

	// 设置镜像类型
	/// @param type 镜像类型
	/// @return 无返回值
	void SetMirroringType(MirroringType type);

	// 获取当前镜像类型
	/// @return 当前镜像类型
	MirroringType GetMirroringType();

	// 内部写 VRAM（会处理映射与访问权限）
	/// @param addr VRAM 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	void InternalWriteVram(uint16_t addr, uint8_t value);

	__forceinline uint8_t InternalReadVram(uint16_t addr)
	{
		if(_chrMemoryAccess[addr >> 8] & MemoryAccessType::Read) {
			return _chrPages[addr >> 8][(uint8_t)addr];
		}

		//Open bus - "When CHR is disabled, the pattern tables are open bus. Theoretically, this should return the LSB of the address read, but real-world behavior varies."
		return addr;
	}

	// 获取 mapper 状态条目（用于调试/序列化）
	/// @return mapper 状态条目列表
	virtual vector<MapperStateEntry> GetMapperStateEntries() { return {}; }

	// 应用 ROM 补丁并记录原始数据
	/// @param orgPrgRom 原始 PRG ROM 缓存引用
	/// @param orgChrRom 原始 CHR ROM 缓存引用（可空）
	/// @return 无返回值
	void LoadRomPatch(vector<uint8_t>& orgPrgRom, vector<uint8_t>* orgChrRom = nullptr);

	// 将 ROM 保存到文件或导出
	/// @param orgPrgRom 原始 PRG ROM 缓存引用
	/// @param orgChrRom 原始 CHR ROM 缓存引用（可空）
	/// @return 无返回值
	void SaveRom(vector<uint8_t>& orgPrgRom, vector<uint8_t>* orgChrRom = nullptr);

	// 序列化 ROM 差异用于状态回放
	/// @param s 序列化器实例
	/// @param orgPrgRom 原始 PRG ROM 缓存引用
	/// @param orgChrRom 原始 CHR ROM 缓存引用（可空）
	/// @return 无返回值
	void SerializeRomDiff(Serializer& s, vector<uint8_t>& orgPrgRom, vector<uint8_t>* orgChrRom = nullptr);

public:
	static constexpr uint32_t NametableSize = 0x400; // Nametable 大小（字节）

	// 初始化 mapper，绑定控制台与 ROM 数据
	/// @param console 指向 NesConsole 实例的指针
	/// @param romData ROM 数据
	/// @return 无返回值
	void Initialize(NesConsole* console, RomData& romData);

	// 为特定 mapper 做进一步初始化
	/// @param romData ROM 数据
	/// @return 无返回值
	void InitSpecificMapper(RomData& romData);

	// 构造函数
	BaseMapper();

	// 析构函数
	virtual ~BaseMapper();

	// 重置 mapper（softReset 指示是否为软重置）
	/// @param softReset 是否为软重置
	/// @return 无返回值
	virtual void Reset(bool softReset);

	// 重置后上电时的回调（可重写）
	virtual void OnAfterResetPowerOn() {}

	// 获取游戏系统类型
	/// @return 游戏系统类型
	GameSystem GetGameSystem();

	// 获取 PPU 型号
	/// @return PPU 型号
	PpuModel GetPpuModel();

	// 获取 Epsm 实例指针
	/// @return Epsm 实例指针
	Epsm* GetEpsm() { return _epsm.get(); }

	// 是否具有默认工作 RAM
	/// @return 是否具有默认工作 RAM
	bool HasDefaultWorkRam();

	// 设置控制台地区（NTSC/PAL）
	/// @param region 控制台地区
	/// @return 无返回值
	void SetRegion(ConsoleRegion region);

	// 是否启用 CPU 时钟钩子
	__forceinline bool HasCpuClockHook() { return _hasCpuClockHook; }

	// 处理 CPU 时钟事件（可重写）
	/// @return 无返回值
	virtual void ProcessCpuClock();

	// 是否启用 VRAM 地址变更钩子
	__forceinline bool HasVramAddressHook() { return _hasVramAddressHook; }

	// 通知 VRAM 地址发生变化（可重写）
	/// @param addr 发生变化的 VRAM 地址
	/// @return 无返回值
	virtual void NotifyVramAddressChange(uint16_t addr);

	// 填充内存范围信息（实现 INesMemoryHandler）
	/// @param ranges 输出的内存范围集合
	/// @return 无返回值
	virtual void GetMemoryRanges(MemoryRanges& ranges) override;

	// 获取内部 RAM 大小（默认 0x800）
	/// @return 内部 RAM 大小（字节）
	virtual uint32_t GetInternalRamSize() { return 0x800; }

	// 保存电池数据到磁盘（可重写）
	/// @return 无返回值
	virtual void SaveBattery();

	// 获取 ROM 信息副本
	/// @return ROM 信息副本
	NesRomInfo GetRomInfo();

	// 获取 mapper 的拨码开关数量
	/// @return 拨码开关数量
	uint32_t GetMapperDipSwitchCount();

	// 从 CPU 地址空间读取一个字节
	/// @param addr CPU 地址
	/// @return 读取到的字节值
	uint8_t ReadRam(uint16_t addr) override;

	// 从 CPU 地址空间读取但不触发副作用
	/// @param addr CPU 地址
	/// @return 读取到的字节值（不触发副作用）
	uint8_t PeekRam(uint16_t addr) override;

	// 调试用读取 RAM（可禁用副作用）
	/// @param addr CPU 地址
	/// @return 读取到的字节值
	uint8_t DebugReadRam(uint16_t addr);

	// 向 CPU 地址空间写入一个字节
	/// @param addr CPU 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	void WriteRam(uint16_t addr, uint8_t value) override;

	// 调试用写 RAM（可禁用副作用）
	/// @param addr CPU 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	void DebugWriteRam(uint16_t addr, uint8_t value);

	// 写入 PRG RAM（通常为保存数据）
	/// @param addr PRG RAM 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	void WritePrgRam(uint16_t addr, uint8_t value);

	// Mapper 自定义 VRAM 读取实现（可重写）
	/// @param addr VRAM 地址
	/// @param operationType 读取类型
	/// @return 读取到的字节值
	virtual uint8_t MapperReadVram(uint16_t addr, MemoryOperationType operationType);

	// Mapper 自定义 VRAM 写入实现（可重写）
	/// @param addr VRAM 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	virtual void MapperWriteVram(uint16_t addr, uint8_t value);

	// 读取 VRAM 并触发 PPU 的读处理
	__forceinline uint8_t ReadVram(uint16_t addr, MemoryOperationType type = MemoryOperationType::PpuRenderingRead)
	{
		uint8_t value;
		if(!_hasCustomReadVram) {
			value = InternalReadVram(addr);
		} else {
			value = MapperReadVram(addr, type);
		}
		_emu->ProcessPpuRead<CpuType::Nes>(addr, value, MemoryType::NesPpuMemory, type);
		return value;
	}

	// 调试写 VRAM（可选择禁用副作用）
	/// @param addr VRAM 地址
	/// @param value 写入的字节值
	/// @param disableSideEffects 是否禁用副作用
	/// @return 无返回值
	void DebugWriteVram(uint16_t addr, uint8_t value, bool disableSideEffects = true);

	// 写 VRAM（正常流程）
	/// @param addr VRAM 地址
	/// @param value 写入的字节值
	/// @return 无返回值
	void WriteVram(uint16_t addr, uint8_t value);

	// 调试读 VRAM（可选择禁用副作用）
	/// @param addr VRAM 地址
	/// @param disableSideEffects 是否禁用副作用
	/// @return 读取到的字节值
	uint8_t DebugReadVram(uint16_t addr, bool disableSideEffects = true);

	// 复制 CHR 图块到目标缓冲
	/// @param address CHR 图块地址
	/// @param dest 目标缓冲指针
	/// @return 无返回值
	void CopyChrTile(uint32_t address, uint8_t* dest);

	//Debugger Helper Functions
	// 是否存在 CHR RAM
	/// @return 是否存在 CHR RAM
	bool HasChrRam();

	// 是否存在 CHR ROM
	/// @return 是否存在 CHR ROM
	bool HasChrRom();

	// 获取 CHR ROM 大小
	/// @return CHR ROM 大小（字节）
	uint32_t GetChrRomSize() { return _chrRomSize; }

	// 获取卡带状态（用于调试/序列化）
	/// @return 卡带状态
	CartridgeState GetState();

	// 将相对地址转换为绝对地址信息
	/// @param relativeAddr 相对地址
	/// @return 绝对地址信息
	AddressInfo GetAbsoluteAddress(uint16_t relativeAddr);

	// 获取 PPU 的绝对地址并写入 info
	/// @param relativeAddr 相对地址
	/// @param info 输出的地址信息
	/// @return 无返回值
	void GetPpuAbsoluteAddress(uint16_t relativeAddr, AddressInfo& info);

	// 获取 PPU 的绝对地址
	/// @param relativeAddr 相对地址
	/// @return 绝对地址信息
	AddressInfo GetPpuAbsoluteAddress(uint32_t relativeAddr);

	// 将绝对地址转换为相对地址信息
	/// @param addr 绝对地址信息
	/// @return 相对地址信息
	AddressInfo GetRelativeAddress(AddressInfo& addr);

	// 获取 PPU 相对地址偏移
	/// @param addr 地址信息
	/// @return PPU 相对地址偏移
	int32_t GetPpuRelativeAddress(AddressInfo& addr);

	// 检查地址是否为写寄存器
	/// @param addr 要检查的地址
	/// @return 是否为写寄存器
	bool IsWriteRegister(uint16_t addr);

	// 检查地址是否为读寄存器
	/// @param addr 要检查的地址
	/// @return 是否为读寄存器
	bool IsReadRegister(uint16_t addr);

	// 导出 ROM 文件数据（可选择导出为 IPS）
	/// @param out 输出缓冲
	/// @param asIpsFile 是否导出为 IPS
	/// @param header 可选头数据指针
	/// @return 无返回值
	void GetRomFileData(vector<uint8_t>& out, bool asIpsFile, uint8_t* header);

	// 获取 PRG+CHR 的副本
	/// @return PRG+CHR 的副本数据
	vector<uint8_t> GetPrgChrCopy();

	// 恢复 PRG/CHR 的备份数据
	/// @param backupData 备份数据引用
	/// @return 无返回值
	void RestorePrgChrBackup(vector<uint8_t>& backupData);

	// 撤销对 PRG/CHR 的修改
	/// @return 无返回值
	void RevertPrgChrChanges();

	// 检查是否存在对 PRG/CHR 的修改
	/// @return 是否存在更改
	bool HasPrgChrChanges();

	// 将 PRG/CHR ROM 复制到另一个 mapper
	/// @param mapper 目标 mapper 指针
	/// @return 无返回值
	void CopyPrgChrRom(BaseMapper* mapper);

	// 切换内存访问权限（主/子）
	/// @param sub 子 mapper 指针
	/// @param mainHasAccess 主 mapper 是否保留访问权限
	/// @return 无返回值
	void SwapMemoryAccess(BaseMapper* sub, bool mainHasAccess);
};