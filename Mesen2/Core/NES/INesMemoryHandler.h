#pragma once

#include "pch.h"
#include "NES/NesTypes.h"

class MemoryRanges
{
private:
	vector<uint16_t> _ramReadAddresses;
	vector<uint16_t> _ramWriteAddresses;
	bool _allowOverride = false;

public:
	vector<uint16_t>* GetRAMReadAddresses() { return &_ramReadAddresses; }
	vector<uint16_t>* GetRAMWriteAddresses() { return &_ramWriteAddresses; }

	bool GetAllowOverride()
	{
		return _allowOverride;
	}

	void SetAllowOverride()
	{
		_allowOverride = true;
	}

	void AddHandler(MemoryOperation operation, uint16_t start, uint16_t end = 0)
	{
		if(end == 0) {
			end = start;
		}

		if(operation == MemoryOperation::Read || operation == MemoryOperation::Any) {
			for(uint32_t i = start; i <= end; i++) {
				_ramReadAddresses.push_back((uint16_t)i);
			}
		}

		if(operation == MemoryOperation::Write || operation == MemoryOperation::Any) {
			for(uint32_t i = start; i <= end; i++) {
				_ramWriteAddresses.push_back((uint16_t)i);
			}
		}
	}
};

class INesMemoryHandler
{
public:
	virtual void GetMemoryRanges(MemoryRanges& ranges) = 0;
	virtual uint8_t ReadRam(uint16_t addr) = 0;
	virtual void WriteRam(uint16_t addr, uint8_t value) = 0;
	virtual uint8_t PeekRam(uint16_t addr) { return 0; }

	// 钩子：允许低地址读写（默认 false）
	// 如果返回 true，处理器将转发低地址的读写交给 WriteLow/ReadLow 处理。
	virtual bool AllowLowReadWrite() { return false; }

	// 当 AllowLowReadWrite() 为 true 时，可重写。
	virtual void WriteLow(uint16_t addr, uint8_t value) {}
	virtual uint8_t ReadLow(uint16_t addr) { return 0; }

	virtual ~INesMemoryHandler() {}
};