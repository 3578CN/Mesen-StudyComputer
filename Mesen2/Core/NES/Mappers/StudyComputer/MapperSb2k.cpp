/*------------------------------------------------------------------------
名称：SB2K BIOS Mapper 源文件
说明：Mapper 171 SubMapperID 2
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-04
备注：
------------------------------------------------------------------------*/
#include "pch.h"
#include "NES/Mappers/StudyComputer/MapperSb2k.h"
#include "NES/NesConsole.h"
#include "NES/BaseNesPpu.h"
#include "NES/NesCpu.h"
#include "NES/NesMemoryManager.h"

// 描述：初始化 Mapper 的寄存器和映射状态。
// 说明：在初始化期间将寄存器清零并更新 PRG/CHR 映射表以反映初始状态。
void MapperSb2k::InitMapper()
{
}

// 描述：重置 Mapper 状态并更新映射表。
// 参数：softReset 指示是否为软复位。true 表示软复位，false 表示第一次上电或者硬复位。
void MapperSb2k::Reset(bool softReset)
{
}