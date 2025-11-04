#include <stdio.h>
#include <string>

// 预声明 Emulator，用于发送通知
class Emulator;

class FloppyDriveController
{
public:
	/**
	 * 构造函数
	 * @param emu 指向宿主 Emulator，用于发送通知
	 */
	FloppyDriveController(Emulator* emu);
	~FloppyDriveController();

	// 加载磁盘
	int LoadDiskImage(const char* fname);
	// 弹出磁盘
	int Eject();
	// 保存磁盘
	int SaveDiskImage();
	/// <summary>
	/// 获取当前磁盘的目录树 JSON 串，用于 UI 渲染。
	/// </summary>
	int GetDirectoryTreeJson(std::string& outJson);

	int IsPresent() { return (pDiskFile != nullptr); }
	// 返回当前软驱是否处于读/写活动中（1: 活动中, 0: 空闲）
	int IsActive();

	/**
	 * 将主机内存中的文件数据写入当前加载的 FAT12 镜像（根目录下）。
	 * @param filename 要在镜像中创建/覆盖的目标文件名（仅文件名，不含路径），会被转换为短文件名 (8.3)
	 * @param data 指向文件内容的字节缓冲区
	 * @param length 缓冲区长度（字节）
	 * @return 返回 1 表示成功，0 表示失败
	 */
	int AddFileFromBuffer(const char* filename, const unsigned char* data, unsigned int length);

	/**
	 * 获取镜像中指定文件的大小（字节）。
	 * @param filename 文件名（支持与 JSON 中一致的长/短文件名匹配）。
	 * @return 返回文件大小（字节），未找到或出错返回 0。
	 */
	int GetFileSize(const char* filename);

	/**
	 * 从镜像中读取指定文件到提供的缓冲区。
	 * @param filename 要读取的文件名（支持与 GetFileSize 相同的匹配规则）。
	 * @param outBuffer 输出缓冲区指针（由调用方分配）。
	 * @param maxLength 输出缓冲区最大可写长度。
	 * @return 成功返回实际读取字节数；失败或未找到返回 0。
	 */
	int ReadFileToBuffer(const char* filename, unsigned char* outBuffer, uint32_t maxLength);

	/**
	 * 从镜像中删除指定文件（仅支持根目录，支持短/长名匹配）。
	 * 不递归删除子目录，也不额外处理长文件名条目的复杂清理（与现有写入行为保持一致）。
	 * @param filename 要删除的文件名（与 GetFileSize/ReadFileToBuffer 支持相同的匹配规则）
	 * @return 返回 1 表示成功，0 表示失败（例如镜像未加载、正在 I/O、文件不存在或写回失败）。
	 */
	int DeleteFileByName(const char* filename);

	// IO: nPort: 0-7
	unsigned char Read(unsigned char nPort);
	void Write(unsigned char nPort, unsigned nData);

	int SetWriteProtect(int bWP) { return 0; }
	int CheckIRQ() { return bFdcIrq; }
	void FdcHardReset(void);

	// Commands
	static void FdcNop(FloppyDriveController* thiz);
	static void FdcReadTrack(FloppyDriveController* thiz);
	static void FdcSpecify(FloppyDriveController* thiz);
	static void FdcSenseDriveStatus(FloppyDriveController* thiz);
	static void FdcWriteData(FloppyDriveController* thiz);
	static void FdcReadData(FloppyDriveController* thiz);
	static void FdcRecalibrate(FloppyDriveController* thiz);
	static void FdcSenseIntStatus(FloppyDriveController* thiz);
	static void FdcWriteDeletedData(FloppyDriveController* thiz);
	static void FdcReadID(FloppyDriveController* thiz);
	static void FdcReadDeletedData(FloppyDriveController* thiz);
	static void FdcFormatTrack(FloppyDriveController* thiz);
	static void FdcSeek(FloppyDriveController* thiz);
	static void FdcScanEqual(FloppyDriveController* thiz);
	static void FdcScanLowOrEqual(FloppyDriveController* thiz);
	static void FdcScanHighOrEqual(FloppyDriveController* thiz);

	typedef struct tagFDC_CMD_DESC
	{
		unsigned char bWLength;
		unsigned char bRLength;
		void (*pFun)(FloppyDriveController*);
	} FDC_CMD_DESC;

protected:
	void	FdcSoftReset(void);

	// for FDC
	int		bFdcIrq;
	int		bFdcHwReset;
	int		bFdcSoftReset;

	int		bFdcDataBytes;

	int		bFdcDmaInt;
	int		nFdcDrvSel;
	int		nFdcMotor;
	// FDC 接口速度设置（来自端口 0x3F7 的低位）
	int		nFdcSpeed;

#define FDC_MS_BUSYS0		0x01	// FDD0 in SEEK mode
#define FDC_MS_BUSYS1		0x02	// FDD1 in SEEK mode
#define FDC_MS_BUSYS2		0x04	// FDD2 in SEEK mode
#define FDC_MS_BUSYS3		0x08	// FDD3 in SEEK mode
#define FDC_MS_BUSYRW		0x10	// Read or Write in progress
#define FDC_MS_EXECUTION	0x20	// Execution Mode
#define FDC_MS_DATA_IN		0x40	// Data input or output
#define FDC_MS_RQM			0x80	// Request for Master, Ready
	unsigned char nFdcMainStatus;

#define FDC_S0_US0			0x01	// Unit Select 0
#define FDC_S0_US1			0x02	// Unit Select 1
#define FDC_S0_HD			0x04	// Head Address
#define FDC_S0_NR			0x08	// Not Ready
#define FDC_S0_EC			0x10	// Equipment Check
#define FDC_S0_SE			0x20	// Seek End
#define FDC_S0_IC0			0x40	// Interrupt Code
#define FDC_S0_IC1			0x80	// NT/AT/IC/XX

#define FDC_S1_MA			0x01	// Missing Address Mark
#define FDC_S1_NW			0x02	// Not Writable
#define FDC_S1_ND			0x04	// No Data
#define FDC_S1_OR			0x10	// Over Run
#define FDC_S1_DE			0x20	// Data Error
#define FDC_S1_EN			0x80	// End of Cylinder

#define FDC_S2_MD			0x01	// Missing Address Mark in Data Field
#define FDC_S2_BC			0x02	// Bad Cylinder
#define FDC_S2_SN			0x04	// Scan Not Satisfied
#define FDC_S2_SH			0x08	// Scan Equal Hit
#define FDC_S2_WC			0x10	// Wrong Cylinder
#define FDC_S2_DD			0x20	// Data Error in Data Field
#define FDC_S2_CM			0x40	// Control Mark

#define FDC_S3_US0			0x01	// Unit Select 0
#define FDC_S3_US1			0x02	// Unit Select 1
#define FDC_S3_HD			0x04	// Side Select
#define FDC_S3_TS			0x08	// Two Side
#define FDC_S3_T0			0x10	// Track 0
#define FDC_S3_RY			0x20	// Ready
#define FDC_S3_WP			0x40	// Write Protect
#define FDC_S3_FT			0x80	// Fault
	unsigned char nFDCStatus[4];

#define FDC_CC_MASK						0x1F
#define FDC_CF_MT						0x80
#define FDC_CF_MF						0x40
#define FDC_CF_SK						0x20

#define FDC_CC_READ_TRACK				0x02
#define FDC_CC_SPECIFY					0x03
#define FDC_CC_SENSE_DRIVE_STATUS		0x04
#define FDC_CC_WRITE_DATA				0x05
#define FDC_CC_READ_DATA				0x06
#define FDC_CC_RECALIBRATE				0x07
#define FDC_CC_SENSE_INTERRUPT_STATUS	0x08
#define FDC_CC_WRITE_DELETED_DATA		0x09
#define FDC_CC_READ_ID					0x0A
#define FDC_CC_READ_DELETED_DATA		0x0C
#define FDC_CC_FORMAT_TRACK				0x0D
#define FDC_CC_SEEK						0x0F
#define FDC_CC_SCAN_EQUAL				0x11
#define FDC_CC_SCAN_LOW_OR_EQUAL		0x19
#define FDC_CC_SCAN_HIGH_OR_EQUAL		0x1D
	unsigned char bFdcCycle;
	unsigned char bFdcLastCommand;
	unsigned char bFdcCommands[10];
	unsigned char bFdcResults[8];
	const FDC_CMD_DESC* pFdcCmd;

#define FDC_PH_IDLE						0
#define FDC_PH_COMMAND					1
#define FDC_PH_EXECUTION				2
#define FDC_PH_RESULT					3
	unsigned char bFdcPhase;

	unsigned char  nFdcCylinder;
	int               nFdcDataOffset;

	FILE* pDiskFile;

	int			   nCurrentLBA;

	// 使用文件句柄 pDiskFile 代替内存镜像缓冲区

	// 磁盘变更标志：插入或弹出时设置，读取端口 0x3F7 时报告并清除
	int            bDiskChanged;

	int            nDiskSize;
	char           szDiskName[2048];

private:
	// 指向宿主 Emulator，用于发送通知（可为空）
	Emulator* _emu;

	// 上一次记录的活动状态（0: 空闲, 1: 活动中），用于避免重复发送通知
	int bFdcActiveState;

	// 当 I/O 活动状态变化时调用，负责发送 FloppyIoStarted/FloppyIoStopped 通知
	void UpdateActiveState();
};