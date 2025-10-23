/*------------------------------------------------------------------------
名称：SB2K 专用 UM6576 头文件
说明：
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-04
备注：
------------------------------------------------------------------------*/
#pragma once

#include "pch.h"
#include "NES/NesPpu.h"

class Sb2kPpu final : public NesPpu<Sb2kPpu>
{
public:
	explicit Sb2kPpu(NesConsole* console);
	void* OnBeforeSendFrame();
	void ProcessScanline();
};