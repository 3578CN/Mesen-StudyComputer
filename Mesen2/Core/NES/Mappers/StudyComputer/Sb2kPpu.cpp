/*------------------------------------------------------------------------
名称：SB2K 专用 UM6576 PPU 源文件
说明：
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-04
备注：
------------------------------------------------------------------------*/
#include "pch.h"
#include "NES/Mappers/StudyComputer/Sb2kPpu.h"
#include "NES/Mappers/StudyComputer/MapperSb2k.h"
#include "NES/NesConsole.h"
#include "NES/NesMemoryManager.h"
#include <NES/NesConstants.h>
#include <algorithm>

// 描述：构造 SB2K 专用 UM6576 PPU。
Sb2kPpu::Sb2kPpu(NesConsole* console) : NesPpu<Sb2kPpu>(console)
{
	OutputDebug("初始化ppu");
}

void* Sb2kPpu::OnBeforeSendFrame()
{
	return nullptr;
}

void Sb2kPpu::ProcessScanline()
{

}