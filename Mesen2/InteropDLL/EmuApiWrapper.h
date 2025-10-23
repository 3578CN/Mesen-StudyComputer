/*------------------------------------------------------------------------
名称：EmuApiWrapper.h
说明：声明 Interop 层对外提供的用于访问全局 FloppyDriveController 的辅助函数。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-17
备注：该文件仅声明访问器，实际实例在 EmuApiWrapper.cpp 中管理。
------------------------------------------------------------------------*/

#pragma once

class FloppyDriveController;

// 返回由 InteropDLL 管理的全局 FloppyDriveController 指针（可能为 nullptr）
FloppyDriveController* InteropGetFdc();
