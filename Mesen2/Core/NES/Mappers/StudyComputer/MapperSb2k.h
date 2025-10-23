/*------------------------------------------------------------------------
名称：SB2K BIOS Mapper 头文件
说明：
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-04
备注：
------------------------------------------------------------------------*/
#pragma once

#include "pch.h"
#include "NES/BaseMapper.h"

class MapperSb2k final : public BaseMapper
{
public:
	void	Reset(bool softReset) override;

protected:
	uint16_t GetPrgPageSize() override { return 0x1000; } // 4KB PRG 页面
	uint16_t GetChrPageSize() override { return 0x0400; } // 1KB CHR 页面
	void	InitMapper() override;
};