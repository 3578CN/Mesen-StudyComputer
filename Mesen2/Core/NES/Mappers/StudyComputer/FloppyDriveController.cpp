#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER)
#include <share.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "FloppyDriveController.h"

// https://www.cpcwiki.eu/index.php/765_FDC

static const FloppyDriveController::FDC_CMD_DESC FdcCmdTable[32] =
{
	/* 0x00 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x01 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x02 */ {9, 7, FloppyDriveController::FdcReadTrack},
	/* 0x03 */ {3, 0, FloppyDriveController::FdcSpecify},
	/* 0x04 */ {2, 1, FloppyDriveController::FdcSenseDriveStatus},
	/* 0x05 */ {9, 7, FloppyDriveController::FdcWriteData},
	/* 0x06 */ {9, 7, FloppyDriveController::FdcReadData},
	/* 0x07 */ {2, 0, FloppyDriveController::FdcRecalibrate},
	/* 0x08 */ {1, 2, FloppyDriveController::FdcSenseIntStatus},
	/* 0x09 */ {9, 7, FloppyDriveController::FdcWriteDeletedData},
	/* 0x0A */ {2, 7, FloppyDriveController::FdcReadID},
	/* 0x0B */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x0C */ {9, 7, FloppyDriveController::FdcReadDeletedData},
	/* 0x0D */ {6, 7, FloppyDriveController::FdcFormatTrack},
	/* 0x0E */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x0F */ {3, 0, FloppyDriveController::FdcSeek},
	/* 0x10 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x11 */ {9, 7, FloppyDriveController::FdcScanEqual},
	/* 0x12 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x13 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x14 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x15 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x16 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x17 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x18 */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x19 */ {9, 7, FloppyDriveController::FdcScanLowOrEqual},
	/* 0x1A */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x1B */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x1C */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x1D */ {9, 7, FloppyDriveController::FdcScanHighOrEqual},
	/* 0x1E */ {1, 1, FloppyDriveController::FdcNop},
	/* 0x1F */ {1, 1, FloppyDriveController::FdcNop},
};

/*
 * 感知中断状态 (SENSE INTERRUPT STATUS)
 * FDC 会在下列任一情况产生中断信号：
 * 1. 在进入 Result Phase 时，涉及下列命令：
 *    a. Read Data Command
 *    b. Read a Track Command
 *    c. Read ID Command
 *    d. Read Deleted Data Command
 *    e. Write Data Command
 *    f. Format a Cylinder Command
 *    g. Write Deleted Data Command
 *    h. Scan Commands
 * 2. FDD 的 Ready 线状态发生变化
 * 3. Seek 或 Recalibrate 命令结束
 * 4. 在 NON-DMA 模式的 Execution Phase 中

 * 上述原因 1 和 4 引起的中断发生在正常的命令操作期间，处理器可以很容易地区分这些中断。
 * 在 NON-OMA Mode 的 Execution Phase 中，Main Status Register 的 DB5 位为 [high]。
 * 一旦进入 Result Phase，该位会被清除。原因 1 和 4 不需要发送 Sense Interrupt
 * Status 命令。该中断可通过对 FDC 的读/写数据操作来清除。上述原因 2 和 3 引起的中断
 * 可借助 Sense Interrupt Status Command 唯一识别。发出该命令时，会复位中断信号，
 * 并通过 Status Register 0 的第 5、6、7 位来识别中断原因。
 */

FloppyDriveController::FloppyDriveController()
{
	bFdcIrq = 0;
	bFdcHwReset = 0;
	bFdcSoftReset = 0;

	bFdcDmaInt = 0;
	nFdcDrvSel = 0;
	nFdcMotor = 0;
	nFdcMainStatus = FDC_MS_RQM;

	nFDCStatus[0] = 0;
	nFDCStatus[1] = 0;
	nFDCStatus[2] = 0;
	nFDCStatus[3] = 0;

	bFdcCycle = 0;
	bFdcPhase = FDC_PH_IDLE;

	nFdcCylinder = 0;

	nFdcDataOffset = 0;

	nDiskSize = 0;
	pDiskFile = nullptr;

	nCurrentLBA = 0;

	// 磁盘变更标志初始化
	bDiskChanged = 0;

	// speed 默认 0
	nFdcSpeed = 0;
}

FloppyDriveController::~FloppyDriveController()
{
	if(pDiskFile) fclose(pDiskFile);
}

int FloppyDriveController::LoadDiskImage(const char* filePath)
{
	FILE* fp;

	// try to open disk image for read/write (do not truncate)
#if defined(_MSC_VER)
	// 使用 _sopen_s 并传入 _SH_DENYNO，避免独占打开（允许共享读写）
	int fd;
	errno_t err = _sopen_s(&fd, filePath, _O_RDWR | _O_BINARY, _SH_DENYNO, _S_IREAD | _S_IWRITE);
	if(err != 0 || fd == -1) {
		return 0;
	}
	if(!(fp = _fdopen(fd, "rb+"))) {
		_close(fd);
		return 0;
	}
#else
	if(!(fp = ::fopen(filePath, "rb+"))) {
		return 0;
	}
#endif

	::fseek(fp, 0, SEEK_END);
	nDiskSize = ::ftell(fp);
	::fseek(fp, 0, SEEK_SET);

	// close previous file if any
	if(pDiskFile) fclose(pDiskFile);
	pDiskFile = fp;
#if defined(_MSC_VER)
	strcpy_s(szDiskName, sizeof(szDiskName), filePath);
#else
	strncpy(szDiskName, filePath, sizeof(szDiskName) - 1);
	szDiskName[sizeof(szDiskName) - 1] = '\0';
#endif

	// 标记磁盘已改变（插入）
	bDiskChanged = 1;

	// reset data transfer offset
	nFdcDataOffset = 0;

	return 1;
}

// 描述 弹出当前加载的磁盘镜像，如果有未保存的数据会尝试保存。
// 返回 0 表示无磁盘，1 表示成功。
int FloppyDriveController::Eject()
{
	// 没有加载磁盘
	if(!pDiskFile) {
		// 没有可弹出的磁盘
		return 0;
	}

	// 关闭文件句柄
	fclose(pDiskFile);
	pDiskFile = nullptr;
	nDiskSize = 0;
	szDiskName[0] = '\0';

	// 重置状态
	nCurrentLBA = 0;
	nFdcDataOffset = 0;
	bFdcDataBytes = 0;
	bFdcCycle = 0;
	bFdcPhase = FDC_PH_IDLE;
	bFdcIrq = 0;
	bFdcDmaInt = 0;
	nFdcDrvSel = 0;
	nFdcMotor = 0;
	nFdcMainStatus = FDC_MS_RQM;
	nFDCStatus[0] = nFDCStatus[1] = nFDCStatus[2] = nFDCStatus[3] = 0;


	// 标记磁盘已改变（弹出）
	bDiskChanged = 1;

	return 1;
}

int FloppyDriveController::SaveDiskImage()
{
	// 直接对打开的镜像文件执行刷新
	if(!pDiskFile) return 0;

	if(::fflush(pDiskFile) != 0) {
		return 0;
	}

	return 1;
}

unsigned char FloppyDriveController::Read(unsigned char nPort)
{
	unsigned char nData;

	switch(nPort) {
		case 0: // 3F0: FDCDMADackIO
		case 1: // 3F1: FDCDMATcIO
			if(pDiskFile) {
				unsigned char tmp = 0;
				::fseek(pDiskFile, nFdcDataOffset, SEEK_SET);
				if(::fread(&tmp, 1, 1, pDiskFile) == 1) {
					nData = tmp;
				} else {
					nData = 0;
				}
				nFdcDataOffset++;
				bFdcDataBytes--;
				if(0 == bFdcDataBytes)
					bFdcPhase = FDC_PH_RESULT;
			} else {
				bDiskChanged = 1;
				nData = 0;
			}
			break;
		case 2: // 3F2: FDCDRQPortI/FDCCtrlPortO
			// I: D6 : FDC DRQ
			nData = 0x40;
			break;
		case 3: // 3F3: FDCIRQPortI/FDCDMADackIO
			// I: D6 : IRQ
			if(bFdcIrq)
				nData = 0x40;
			else
				nData = 0;
			break;

		case 4: // 3F4: FDCResetPortO/FDCStatPortI
			// I: D7 : FDC ready
			// I: D6 : FDC dir
			// I: D5 : FDC busy
			nData = nFdcMainStatus;
			break;
		case 5: // 3F5: FDCDataPortIO
			if(FDC_PH_EXECUTION == bFdcPhase) {
				// Non-DMA mode
				switch(bFdcLastCommand) {
					case 0x02: // ReadTrack
					case 0x06: // ReadData
					{
						unsigned char tmp = 0;
						if(pDiskFile) {
							::fseek(pDiskFile, nFdcDataOffset, SEEK_SET);
							if(::fread(&tmp, 1, 1, pDiskFile) != 1) tmp = 0;
							nFdcDataOffset++;
						} else {
							bDiskChanged = 1;
							tmp = 0;
						}
						nData = tmp;

						bFdcDataBytes--;
						if(0 == bFdcDataBytes) {
							bFdcPhase = FDC_PH_RESULT;
						}
					}
					break;
					default:
						// who call me?
						break;
				}
			} else if(FDC_PH_RESULT == bFdcPhase) {
				nData = bFdcResults[bFdcCycle];
				bFdcCycle++;
				if(bFdcCycle == pFdcCmd->bRLength) {
					// prepare for next command
					bFdcCycle = 0;
					bFdcPhase = FDC_PH_IDLE;
					nFdcMainStatus &= ~FDC_MS_DATA_IN; // host to fdc

					bFdcIrq = 0;

				}
			} else {
				nData = 0;
			}
			break;
		case 7: // 3F7: FDCChangePortI/FDCSpeedPortO
			// I: D7 : Disk changed
			// 读取时：若磁盘发生变化则返回 D7=1 并清除标志
			if(bDiskChanged) {
				nData = 0x80;
				bDiskChanged = 0;
			} else {
				nData = 0;
			}
			break;
		default:
			nData = 0;
			break;
	}

	return nData;
}

void FloppyDriveController::Write(unsigned char nPort, unsigned nData)
{
	switch(nPort) {
		case 0: // 3F0: FDCDMADackIO
		case 1: // 3F1: FDCDMATcIO
			if(pDiskFile) {
				unsigned char tmp = (unsigned char)nData;
				::fseek(pDiskFile, nFdcDataOffset, SEEK_SET);
				::fwrite(&tmp, 1, 1, pDiskFile);
				nFdcDataOffset++;
			} else {
				bDiskChanged = 1;
			}
			bFdcDataBytes--;
			if(0 == bFdcDataBytes) {
				// 扇区写入完成，刷新 stdio 缓冲以便其它进程能看到变化
				if(pDiskFile) ::fflush(pDiskFile);
				bFdcCycle = 0;
				bFdcPhase = FDC_PH_RESULT;
				nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
			}
			break;
		case 2: // 3F2: FDCDRQPortI/FDCCtrlPortO
			// O: D5 : Drv B motor
			// O: D4 : Drv A motor
			// O: D3 : Enable INT and DMA
			// O: D2 : not FDC Reset
			// O: D[1:0] : Drv sel

			bFdcDmaInt = (nData & 8) ? 1 : 0;
			nFdcDrvSel = nData & 3;
			nFdcMotor = nData >> 4;

			if(nData & 4) {
				if(bFdcSoftReset) {
					FdcSoftReset();

					bFdcSoftReset = 0;

					// IRQ after soft reset
					if(0 == nFdcDrvSel) {
						// Driver A Only
						bFdcIrq = pDiskFile ? 1 : 0;
					} else {
						bFdcIrq = 0;
					}
				}
			} else {
				if(!bFdcSoftReset) {
					bFdcSoftReset = 1;
					bFdcIrq = 0;
				}
			}

			break;
		case 3: // 3F3: FDCIRQPortI/FDCDMADackIO
			// I: D6 : IRQ
			nData = nData;
			break;
		case 4: // 3F4: FDCResetPortO/FDCStatPortI
			// O: D6 : FDC pin reset
			if(nData & 0x40) {
				if(!bFdcHwReset) {
					bFdcHwReset = 1;
					bFdcIrq = 0;
				}
			} else {
				if(bFdcHwReset) {
					FdcHardReset();
					bFdcHwReset = 0;
				}
			}
			break;
		case 5: // 3F5: FDCDataPortIO
			switch(bFdcPhase) {
				case FDC_PH_EXECUTION:
					// Non-DMA mode
					switch(bFdcLastCommand) {
						case 0x05: // WriteData
							if(pDiskFile) {
								unsigned char tmp = (unsigned char)nData;
								::fseek(pDiskFile, nFdcDataOffset, SEEK_SET);
								::fwrite(&tmp, 1, 1, pDiskFile);
								nFdcDataOffset++;
							} else {
								bDiskChanged = 1;
							}
							bFdcDataBytes--;
							if(0 == bFdcDataBytes) {
								// 扇区写入完成，刷新缓冲，通知 host
								if(pDiskFile) ::fflush(pDiskFile);
								bFdcCycle = 0;
								bFdcPhase = FDC_PH_RESULT;
								nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host

								bFdcIrq = 0;
							}
							break;
						case 0x0D: // FormatTrack
							bFdcDataBytes--;
							if(0 == bFdcDataBytes) {
								bFdcCycle = 0;
								bFdcPhase = FDC_PH_RESULT;
								nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host

								bFdcIrq = 0;
							}
							break;
						default:
							// ERROR
							break;
					}
					break;
				case FDC_PH_RESULT:
					// ERROR
					break;
				case FDC_PH_IDLE:
				default:
					bFdcCycle = 0;
					bFdcPhase = FDC_PH_COMMAND;
					pFdcCmd = &FdcCmdTable[nData & FDC_CC_MASK];
					// fall through
				case FDC_PH_COMMAND:
					bFdcCommands[bFdcCycle] = nData;
					bFdcCycle++;
					if(bFdcCycle == pFdcCmd->bWLength) {
						int cmd = bFdcCommands[0] & FDC_CC_MASK;

						pFdcCmd->pFun(this);

						bFdcCycle = 0;
						bFdcLastCommand = cmd;
					}
					break;
			}
			break;
		case 7: // 3F7: FDCChangePortI/FDCSpeedPortO
			// I: D7 : Disk changed
			// O: D[1:0] : 00 500kbps(1.2M, 1.44M)
			//             01 300kbps(360K)
			//             10 250kbps(720K)
			// 写入时：保存速度设置到 nFdcSpeed 的低 2 位，其余位保留
			nFdcSpeed = nData & 0x03;
			break;
		default:
			break;
	}
}

void FloppyDriveController::FdcHardReset(void)
{
	bFdcDmaInt = 0;
	nFdcDrvSel = 0;
	nFdcMotor = 0;
	nFdcMainStatus = FDC_MS_RQM;

	nFDCStatus[0] = 0;
	nFDCStatus[1] = 0;
	nFDCStatus[2] = 0;
	nFDCStatus[3] = 0;

	bFdcIrq = 0;
	bFdcCycle = 0;
	bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcSoftReset(void)
{
	nFdcDrvSel = 0;
	nFdcMotor = 0;
	nFdcMainStatus = FDC_MS_RQM;

	nFDCStatus[0] = 0;
	nFDCStatus[1] = 0;
	nFDCStatus[2] = 0;
	nFDCStatus[3] = 0;

	bFdcCycle = 0;
	bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcNop(FloppyDriveController* thiz)
{
	thiz->nFDCStatus[0] = FDC_S0_IC1;
	thiz->bFdcResults[0] = thiz->nFDCStatus[0];
	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcReadTrack(FloppyDriveController* thiz)
{
	thiz = thiz;

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;

	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcSpecify(FloppyDriveController* thiz)
{
	// 03, DF, 03
	// [1] Step Rate Time, Head Unload Time
	// [2] HLT/ND
	int ND = thiz->bFdcCommands[2] & 1;

	if(ND)
		thiz->nFdcMainStatus |= FDC_MS_EXECUTION;
	else
		thiz->nFdcMainStatus &= ~FDC_MS_EXECUTION;

	thiz->bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcSenseDriveStatus(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcWriteData(FloppyDriveController* thiz)
{
	unsigned char C = thiz->bFdcCommands[2];
	unsigned char H = thiz->bFdcCommands[3];
	unsigned char R = thiz->bFdcCommands[4];
	unsigned char N = thiz->bFdcCommands[5];

	int LBA;

	LBA = C * 36 + H * 18 + (R - 1);

	thiz->nCurrentLBA = LBA;

	thiz->nFdcDataOffset = LBA * 512;

	thiz->bFdcDataBytes = 512;

	R++;
	if(19 == R) {
		R = 1;
		H++;
		if(2 == H) {
			C++;
			if(80 == C)
				C = 0;
		}
	}

	thiz->nFDCStatus[0] = 0;

	thiz->bFdcResults[0] = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ? FDC_S0_IC0 : 0; // ST0
	thiz->bFdcResults[1] = FDC_S1_EN; // ST1
	thiz->bFdcResults[2] = 0; // ST2
	thiz->bFdcResults[3] = C;
	thiz->bFdcResults[4] = H;
	thiz->bFdcResults[5] = R;
	thiz->bFdcResults[6] = N;

	thiz->bFdcIrq = 1;

	// host to fdc
	thiz->nFdcMainStatus &= ~FDC_MS_DATA_IN;

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcReadData(FloppyDriveController* thiz)
{
	unsigned char C = thiz->bFdcCommands[2];
	unsigned char H = thiz->bFdcCommands[3];
	unsigned char R = thiz->bFdcCommands[4];
	unsigned char N = thiz->bFdcCommands[5];

	int LBA;

	LBA = C * 36 + H * 18 + (R - 1);
	if(LBA > 2879) LBA = 2879;

	thiz->nCurrentLBA = LBA;

	thiz->nFdcDataOffset = LBA * 512;

	thiz->bFdcDataBytes = 512;

	R++;
	if(19 == R) {
		R = 1;
		H++;
		if(2 == H) {
			C++;
			if(80 == C)
				C = 0;
		}
	}

	thiz->nFDCStatus[0] = 0;

	thiz->bFdcResults[0] = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ? FDC_S0_IC0 : 0; // ST0
	thiz->bFdcResults[1] = FDC_S1_EN; // ST1
	thiz->bFdcResults[2] = 0; // ST2
	thiz->bFdcResults[3] = C;
	thiz->bFdcResults[4] = H;
	thiz->bFdcResults[5] = R;
	thiz->bFdcResults[6] = N;

	thiz->bFdcIrq = 1;

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;

	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcRecalibrate(FloppyDriveController* thiz)
{
	unsigned char US;

	US = thiz->bFdcCommands[1] & 3;

	thiz->nFDCStatus[0] = US ? (FDC_S0_SE | FDC_S0_IC0) : FDC_S0_SE;

	thiz->bFdcIrq = 1;
	thiz->bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcSenseIntStatus(FloppyDriveController* thiz)
{
	switch(thiz->bFdcLastCommand) {
		case 0x07: // FdcRecalibrate
			thiz->bFdcResults[0] = thiz->nFDCStatus[0];
			thiz->bFdcResults[1] = 0;	// PCN
			break;
		case 0x0F: // FdcSeek
			thiz->bFdcResults[0] = FDC_S0_SE;
			thiz->bFdcResults[1] = thiz->nFdcCylinder;	// PCN
			break;
		default:
			if(0 == thiz->nFdcDrvSel)	// Drv A Only
				thiz->nFDCStatus[0] = FDC_S0_IC0 | FDC_S0_IC1;
			else
				thiz->nFDCStatus[0] = FDC_S0_IC0 | FDC_S0_SE;

			thiz->bFdcResults[0] = thiz->nFDCStatus[0];
			thiz->bFdcResults[1] = thiz->nFdcCylinder;	// PCN
			break;
	}

	thiz->bFdcIrq = 0;
	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcWriteDeletedData(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcReadID(FloppyDriveController* thiz)
{
	if(!thiz->pDiskFile) {
		// no response while disk empty
		thiz->bFdcPhase = FDC_PH_IDLE;
		return;
	}

	thiz->nFDCStatus[0] = 0;

	thiz->bFdcResults[0] = 0;
	thiz->bFdcResults[1] = thiz->pDiskFile ? 0 : (FDC_S1_ND | FDC_S1_MA); // ST1
	thiz->bFdcResults[2] = 0; // ST2
	thiz->bFdcResults[3] = 0;
	thiz->bFdcResults[4] = 0;
	thiz->bFdcResults[5] = 0;
	thiz->bFdcResults[6] = 0;

	thiz->bFdcIrq = 1;
	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcReadDeletedData(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcFormatTrack(FloppyDriveController* thiz)
{
	unsigned char HD_US = thiz->bFdcCommands[1];
	unsigned char N = thiz->bFdcCommands[2]; // N: bytes per sector
	unsigned char SC = thiz->bFdcCommands[3]; // SC: sectors per track
	unsigned char GPL = thiz->bFdcCommands[4]; // GPL: gap 3 length
	unsigned char D = thiz->bFdcCommands[5]; // D: filler pattern to write in each byte

	// 直接将填充字节写入磁盘镜像文件（扇区 512 字节）
	if(thiz->pDiskFile) {
		unsigned char buf[512];
		memset(buf, D, 512);
		::fseek(thiz->pDiskFile, thiz->nCurrentLBA * 512, SEEK_SET);
		::fwrite(buf, 512, 1, thiz->pDiskFile);
		// 格式化写入后立即刷新以便其它程序能看到变化
		::fflush(thiz->pDiskFile);
	} else {
		// 没有磁盘，忽略
	}

	thiz->nFDCStatus[0] = 0;

	thiz->bFdcResults[0] = HD_US & 7;// FDC_S0_IC0; // ST0
	thiz->bFdcResults[1] = 0;// FDC_S1_EN; // ST1
	thiz->bFdcResults[2] = 0; // ST2
	thiz->bFdcResults[3] = 0;
	thiz->bFdcResults[4] = 0;
	thiz->bFdcResults[5] = 0;
	thiz->bFdcResults[6] = N; // bytes per sector

	thiz->bFdcIrq = 1;

	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcSeek(FloppyDriveController* thiz)
{
	// new cylinder number
	unsigned char NCN;
	unsigned char US;
	unsigned char HD;

	HD = (thiz->bFdcCommands[1] >> 2) & 1;
	US = thiz->bFdcCommands[1] & 3;
	NCN = thiz->bFdcCommands[2];

	thiz->nFdcCylinder = NCN;

	//	thiz->nCurrentLBA = NCN * 36 + HD * 18; for format_track command

	thiz->nFDCStatus[0] = FDC_S0_SE;

	thiz->bFdcIrq = 1;
	thiz->bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcScanEqual(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcScanLowOrEqual(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcScanHighOrEqual(FloppyDriveController* thiz)
{
	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}