#include "pch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <array>
#if defined(_MSC_VER)
#include <share.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "FloppyDriveController.h"
// 需要访问 Emulator 以发送通知
#include "Core/Shared/Emulator.h"
#include "Core/Shared/Interfaces/INotificationListener.h"
#include "Core/Shared/NotificationManager.h"
#include "Utilities/StringUtilities.h"

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

namespace
{
struct FilePositionGuard
{
	FILE* handle;
	long position;
	FilePositionGuard(FILE* fp) : handle(fp), position(fp ? ::ftell(fp) : -1) { }
	~FilePositionGuard()
	{
		if(handle && position >= 0) {
			::fseek(handle, position, SEEK_SET);
		}
	}
};

struct Fat12Context
{
	uint32_t bytesPerSector = 0;
	uint32_t sectorsPerCluster = 0;
	uint32_t reservedSectors = 0;
	uint32_t fatCount = 0;
	uint32_t rootEntryCount = 0;
	uint32_t sectorsPerFat = 0;
	uint32_t rootDirSectors = 0;
	uint32_t fatOffset = 0;
	uint32_t rootDirOffset = 0;
	uint32_t dataOffset = 0;
	uint32_t bytesPerCluster = 0;
	uint32_t totalSectors = 0;
	uint32_t totalClusters = 0;
};

enum class FdcNodeType
{
	Disk,
	Directory,
	File
};

struct FdcFileNode
{
	FdcNodeType type = FdcNodeType::File;
	string name;
	uint32_t size = 0;
	vector<FdcFileNode> children;
	// 首簇编号（用于读取文件内容），仅对文件/目录项有意义
	uint16_t firstCluster = 0;
	// 总容量（数据区可用字节数），仅对磁盘类型有意义
	uint32_t capacity = 0;
	// 可用空间（字节）
	uint32_t freeBytes = 0;
	// 修改时间的字符串表示，如果可用格式为 "YYYY-MM-DD HH:MM:SS"
	string modified;
};

static bool ReadBytes(FILE* file, long offset, void* buffer, size_t length)
{
	if(!file || !buffer || length == 0) {
		return false;
	}
	if(::fseek(file, offset, SEEK_SET) != 0) {
		return false;
	}
	return ::fread(buffer, 1, length, file) == length;
}

static bool LoadFat12Context(FILE* file, int diskSize, Fat12Context& ctx)
{
	std::array<uint8_t, 512> sector{};
	if(!ReadBytes(file, 0, sector.data(), sector.size())) {
		return false;
	}

	uint16_t bytesPerSector = (uint16_t)(sector[11] | (sector[12] << 8));
	uint8_t sectorsPerCluster = sector[13];
	uint16_t reservedSectors = (uint16_t)(sector[14] | (sector[15] << 8));
	uint8_t fatCount = sector[16];
	uint16_t rootEntryCount = (uint16_t)(sector[17] | (sector[18] << 8));
	uint16_t totalSectors16 = (uint16_t)(sector[19] | (sector[20] << 8));
	uint16_t sectorsPerFat = (uint16_t)(sector[22] | (sector[23] << 8));
	uint32_t totalSectors32 = (uint32_t)(sector[32] | (sector[33] << 8) | (sector[34] << 16) | (sector[35] << 24));

	uint32_t totalSectors = totalSectors16 != 0 ? totalSectors16 : totalSectors32;
	if(totalSectors == 0 && bytesPerSector != 0) {
		totalSectors = diskSize > 0 ? (uint32_t)(diskSize / bytesPerSector) : 0;
	}

	if(bytesPerSector == 0 || sectorsPerCluster == 0 || fatCount == 0 || sectorsPerFat == 0) {
		return false;
	}

	ctx.bytesPerSector = bytesPerSector;
	ctx.sectorsPerCluster = sectorsPerCluster;
	ctx.reservedSectors = reservedSectors;
	ctx.fatCount = fatCount;
	ctx.rootEntryCount = rootEntryCount;
	ctx.sectorsPerFat = sectorsPerFat;
	ctx.rootDirSectors = (uint32_t)((rootEntryCount * 32 + bytesPerSector - 1) / bytesPerSector);
	ctx.fatOffset = ctx.reservedSectors * ctx.bytesPerSector;
	ctx.rootDirOffset = (ctx.reservedSectors + ctx.fatCount * ctx.sectorsPerFat) * ctx.bytesPerSector;
	ctx.dataOffset = (ctx.reservedSectors + ctx.fatCount * ctx.sectorsPerFat + ctx.rootDirSectors) * ctx.bytesPerSector;
	ctx.bytesPerCluster = ctx.bytesPerSector * ctx.sectorsPerCluster;
	ctx.totalSectors = totalSectors;
	uint32_t dataSectors = (totalSectors > (ctx.reservedSectors + ctx.fatCount * ctx.sectorsPerFat + ctx.rootDirSectors)) ?
		(totalSectors - (ctx.reservedSectors + ctx.fatCount * ctx.sectorsPerFat + ctx.rootDirSectors)) : 0;
	ctx.totalClusters = (ctx.sectorsPerCluster > 0) ? dataSectors / ctx.sectorsPerCluster : 0;

	return ctx.bytesPerSector > 0 && ctx.bytesPerCluster > 0;
}

static bool LoadFatTable(FILE* file, const Fat12Context& ctx, vector<uint8_t>& fatData)
{
	uint32_t fatSize = ctx.sectorsPerFat * ctx.bytesPerSector;
	if(fatSize == 0) {
		return false;
	}
	fatData.resize(fatSize);
	return ReadBytes(file, (long)ctx.fatOffset, fatData.data(), fatData.size());
}

static string TrimTrailingSpaces(const string& value)
{
	size_t endPos = value.find_last_not_of(' ');
	if(endPos == string::npos) {
		return "";
	}
	return value.substr(0, endPos + 1);
}

static string BuildShortName(const uint8_t* namePart, const uint8_t* extPart)
{
	string baseName(reinterpret_cast<const char*>(namePart), 8);
	if(!baseName.empty() && (uint8_t)baseName[0] == 0x05) {
		baseName[0] = (char)0xE5;
	}
	baseName = TrimTrailingSpaces(baseName);
	string extension(reinterpret_cast<const char*>(extPart), 3);
	extension = TrimTrailingSpaces(extension);
	if(extension.empty()) {
		return baseName;
	}
	if(baseName.empty()) {
		return extension;
	}
	return baseName + "." + extension;
}

static std::u16string ExtractLongNameSegment(const uint8_t* entry)
{
	std::u16string segment;
	auto appendRange = [&](int offset, int count) {
		for(int i = 0; i < count; i++) {
			char16_t ch = (char16_t)(entry[offset + i * 2] | (entry[offset + i * 2 + 1] << 8));
			if(ch == 0x0000 || ch == (char16_t)0xFFFF) {
				return false;
			}
			segment.push_back(ch);
		}
		return true;
	};

	bool cont = appendRange(1, 5);
	if(cont) cont = appendRange(14, 6);
	if(cont) appendRange(28, 2);
	return segment;
}

static string BuildLongName(const vector<std::u16string>& segments)
{
	if(segments.empty()) {
		return "";
	}
	std::u16string combined;
	for(const auto& part : segments) {
		combined += part;
	}
	return utf8::utf8::encode(combined);
}

static uint16_t ReadFatValue(const vector<uint8_t>& fat, uint16_t cluster)
{
	uint32_t index = cluster + cluster / 2;
	if(index + 1 >= fat.size()) {
		return 0xFFF;
	}
	uint16_t value = (uint16_t)(fat[index] | (fat[index + 1] << 8));
	if(cluster & 1) {
		value >>= 4;
	} else {
		value &= 0x0FFF;
	}
	return value;
}

static bool WriteFatValue(vector<uint8_t>& fat, uint16_t cluster, uint16_t value)
{
	uint32_t index = cluster + cluster / 2;
	if(index + 1 >= fat.size()) {
		return false;
	}
	uint16_t orig = (uint16_t)(fat[index] | (fat[index + 1] << 8));
	if(cluster & 1) {
		// odd cluster: store high 12 bits
		// keep low 4 bits
		orig &= 0x000F;
		orig |= (uint16_t)((value & 0x0FFF) << 4);
	} else {
		// even cluster: store low 12 bits
		orig &= 0xF000;
		orig |= (uint16_t)(value & 0x0FFF);
	}
	fat[index] = (uint8_t)(orig & 0xFF);
	fat[index + 1] = (uint8_t)((orig >> 8) & 0xFF);
	return true;
}

static bool ReadClusterChain(FILE* file, const Fat12Context& ctx, const vector<uint8_t>& fat, uint16_t startCluster, vector<uint8_t>& outData)
{
	outData.clear();
	if(startCluster < 2 || ctx.bytesPerCluster == 0) {
		return true;
	}

	vector<uint8_t> buffer(ctx.bytesPerCluster);
	uint32_t maxIterations = ctx.totalClusters + 2;
	uint32_t iteration = 0;
	uint16_t cluster = startCluster;
	while(cluster >= 2 && cluster < 0xFF0) {
		if(iteration++ > maxIterations) {
			break;
		}
		if((cluster - 2) >= ctx.totalClusters) {
			break;
		}
		long offset = (long)(ctx.dataOffset + (uint32_t)(cluster - 2) * ctx.bytesPerCluster);
		if(!ReadBytes(file, offset, buffer.data(), buffer.size())) {
			return false;
		}
		outData.insert(outData.end(), buffer.begin(), buffer.end());
		uint16_t nextCluster = ReadFatValue(fat, cluster);
		if(nextCluster >= 0xFF8) {
			break;
		}
		if(nextCluster == 0) {
			break;
		}
		cluster = nextCluster;
	}
	return true;
}

static void ParseDirectoryData(FILE* file, const Fat12Context& ctx, const vector<uint8_t>& fat, const uint8_t* data, size_t byteCount, vector<FdcFileNode>& output)
{
	output.clear();
	if(!data || byteCount == 0) {
		return;
	}

	vector<std::u16string> longNameParts;
	size_t entryCount = byteCount / 32;
	for(size_t i = 0; i < entryCount; i++) {
		const uint8_t* entry = data + i * 32;
		uint8_t firstByte = entry[0];
		if(firstByte == 0x00) {
			break;
		}
		uint8_t attr = entry[11];
		if(attr == 0x0F) {
			uint8_t sequence = entry[0] & 0x1F;
			if((entry[0] & 0x40) != 0) {
				longNameParts.clear();
				longNameParts.resize(sequence);
			}
			if(sequence >= 1 && sequence <= longNameParts.size()) {
				longNameParts[sequence - 1] = ExtractLongNameSegment(entry);
			}
			continue;
		}
		if(firstByte == 0xE5) {
			longNameParts.clear();
			continue;
		}
		if(attr & 0x08) {
			longNameParts.clear();
			continue;
		}

		string name;
		if(!longNameParts.empty()) {
			name = BuildLongName(longNameParts);
		}
		if(name.empty()) {
			name = BuildShortName(entry, entry + 8);
		}
		longNameParts.clear();
		if(name.empty()) {
			continue;
		}

		bool isDirectory = (attr & 0x10) != 0;
		if(isDirectory && (name == "." || name == "..")) {
			continue;
		}

		uint16_t firstClusterLow = (uint16_t)(entry[26] | (entry[27] << 8));
		uint16_t firstClusterHigh = (uint16_t)(entry[20] | (entry[21] << 8));
		uint32_t firstClusterCombined = (uint32_t)firstClusterLow | ((uint32_t)firstClusterHigh << 16);
		uint16_t firstCluster = (uint16_t)firstClusterCombined;
		uint32_t fileSize = (uint32_t)(entry[28] | (entry[29] << 8) | (entry[30] << 16) | (entry[31] << 24));

		FdcFileNode node;
		node.type = isDirectory ? FdcNodeType::Directory : FdcNodeType::File;
		node.name = name;
		node.size = fileSize;
			// 解析目录项中的修改时间（写入时间）。FAT 目录项的写入时间位于偏移 22-25。
			// 时间：entry[22..23]，日期：entry[24..25]
			{
				uint16_t writeTime = (uint16_t)(entry[22] | (entry[23] << 8));
				uint16_t writeDate = (uint16_t)(entry[24] | (entry[25] << 8));
				if(writeDate != 0 || writeTime != 0) {
					int hour = (writeTime >> 11) & 0x1F;
					int minute = (writeTime >> 5) & 0x3F;
					int second = (writeTime & 0x1F) * 2;
					int day = writeDate & 0x1F;
					int month = (writeDate >> 5) & 0x0F;
					int year = ((writeDate >> 9) & 0x7F) + 1980;
					char buf[32];
					snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", year, month, day, hour, minute, second);
					node.modified = buf;
				}
			}
		// 记录首簇，供后续读取文件数据使用
		node.firstCluster = firstCluster;

		if(isDirectory && firstCluster >= 2) {
			vector<uint8_t> dirData;
			if(ReadClusterChain(file, ctx, fat, firstCluster, dirData)) {
				ParseDirectoryData(file, ctx, fat, dirData.data(), dirData.size(), node.children);
			}
		}

		output.push_back(std::move(node));
	}
}

// 在节点树中查找指定名称的文件，若找到返回 true 并填充首簇与大小
static bool FindFileNodeByName(const vector<FdcFileNode>& nodes, const string& filename, uint16_t& outFirstCluster, uint32_t& outSize)
{
	for(const auto& n : nodes) {
		if(n.type == FdcNodeType::File && n.name == filename) {
			outFirstCluster = n.firstCluster;
			outSize = n.size;
			return true;
		}
		if(!n.children.empty()) {
			if(FindFileNodeByName(n.children, filename, outFirstCluster, outSize)) return true;
		}
	}
	return false;
}

static void JsonEscape(const string& text, string& builder)
{
	for(char ch : text) {
		switch(ch) {
			case '\\': builder += "\\\\"; break;
			case '\"': builder += "\\\""; break;
			case '\b': builder += "\\b"; break;
			case '\f': builder += "\\f"; break;
			case '\n': builder += "\\n"; break;
			case '\r': builder += "\\r"; break;
			case '\t': builder += "\\t"; break;
			default:
				if(static_cast<unsigned char>(ch) < 0x20) {
					char buffer[7];
					snprintf(buffer, sizeof(buffer), "\\u%04X", (unsigned)(unsigned char)ch);
					builder += buffer;
				} else {
					builder += ch;
				}
				break;
		}
	}
}

static void AppendJson(const FdcFileNode& node, string& builder)
{
	builder += "{";
	builder += "\"name\":\"";
	JsonEscape(node.name, builder);
	builder += "\"";
	builder += ",\"type\":\"";
	switch(node.type) {
		case FdcNodeType::Disk: builder += "disk"; break;
		case FdcNodeType::Directory: builder += "dir"; break;
		case FdcNodeType::File: builder += "file"; break;
	}
	builder += "\"";
	builder += ",\"size\":";
	builder += std::to_string(node.size);
	// 如果存在修改时间，则以字符串形式输出（YYYY-MM-DD HH:MM:SS）
	if(!node.modified.empty()) {
		builder += ",\"modified\":\"";
		JsonEscape(node.modified, builder);
		builder += "\"";
	}
	if(node.type == FdcNodeType::Disk) {
		builder += ",\"capacity\":";
		builder += std::to_string(node.capacity);
		builder += ",\"free\":";
		builder += std::to_string(node.freeBytes);
	}
	if(node.type != FdcNodeType::File) {
		builder += ",\"children\":[";
		for(size_t i = 0; i < node.children.size(); i++) {
			AppendJson(node.children[i], builder);
			if(i + 1 < node.children.size()) {
				builder += ",";
			}
		}
		builder += "]";
	}
	builder += "}";
}
}

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

FloppyDriveController::FloppyDriveController(Emulator* emu)
{
	_emu = emu;
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

	// 初始化待传输字节计数，防止未初始化内存导致误判为活动中
	bFdcDataBytes = 0;

	// 初始活动状态为空闲
	bFdcActiveState = 0;

	nFdcCylinder = 0;

	nFdcDataOffset = 0;

	nDiskSize = 0;
	pDiskFile = nullptr;

	nCurrentLBA = 0;

	// 磁盘变更标志初始化
	bDiskChanged = 0;

	// speed 默认 0
	nFdcSpeed = 0;

	// 保证通知状态与当前内部状态一致
	UpdateActiveState();
}

FloppyDriveController::~FloppyDriveController()
{
	if(pDiskFile) fclose(pDiskFile);
}

int FloppyDriveController::LoadDiskImage(const char* filePath)
{
	// 更新活动状态（可能从无盘->有盘）
	UpdateActiveState();

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

	// 发送 "磁盘已加载" 通知，供托管层刷新 UI
	if(_emu) {
		_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::FloppyLoaded);
	}

	// reset data transfer offset
	nFdcDataOffset = 0;

	// 确保读写字节计数与相位在加载镜像后为空闲状态，避免残留状态导致误判
	bFdcDataBytes = 0;
	bFdcPhase = FDC_PH_IDLE;

	return 1;
}

// 描述 弹出当前加载的磁盘镜像，如果有未保存的数据会尝试保存。
// 返回 0 表示无磁盘，1 表示成功。
int FloppyDriveController::Eject()
{
	// 弹出时保证通知为停止状态
	UpdateActiveState();

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

	// 发送 "磁盘已弹出" 通知，供托管层刷新 UI
	if(_emu) {
		_emu->GetNotificationManager()->SendNotification(ConsoleNotificationType::FloppyEjected);
	}

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

/// <summary>
/// 解析 FAT12 镜像目录并返回 JSON。
/// </summary>
int FloppyDriveController::GetDirectoryTreeJson(string& outJson)
{
	outJson = "[]";
	if(!pDiskFile) {
		return 0;
	}

	FilePositionGuard guard(pDiskFile);

	Fat12Context ctx;
	if(!LoadFat12Context(pDiskFile, nDiskSize, ctx)) {
		return 0;
	}

	vector<uint8_t> fatData;
	if(!LoadFatTable(pDiskFile, ctx, fatData)) {
		return 0;
	}

	vector<uint8_t> rootBuffer;
	if(ctx.rootEntryCount > 0) {
		rootBuffer.resize((size_t)ctx.rootEntryCount * 32);
		if(!ReadBytes(pDiskFile, (long)ctx.rootDirOffset, rootBuffer.data(), rootBuffer.size())) {
			return 0;
		}
	}

	vector<FdcFileNode> children;
	if(!rootBuffer.empty()) {
		ParseDirectoryData(pDiskFile, ctx, fatData, rootBuffer.data(), rootBuffer.size(), children);
	}

	FdcFileNode rootNode;
	rootNode.type = FdcNodeType::Disk;
	rootNode.size = (uint32_t)(nDiskSize > 0 ? nDiskSize : 0);
	rootNode.children = std::move(children);

	// 计算并填充容量与可用空间信息（以字节为单位）
	if(ctx.bytesPerCluster > 0 && ctx.totalClusters > 0) {
		uint32_t capacity = ctx.totalClusters * ctx.bytesPerCluster;
		uint32_t freeClusters = 0;
		for(uint16_t c = 2; c < (uint16_t)(ctx.totalClusters + 2); ++c) {
			uint16_t val = ReadFatValue(fatData, c);
			if(val == 0) freeClusters++;
		}
		uint32_t freeBytes = freeClusters * ctx.bytesPerCluster;
		rootNode.capacity = capacity;
		rootNode.freeBytes = freeBytes;
	} else {
		rootNode.capacity = 0;
		rootNode.freeBytes = 0;
	}

	string diskName = szDiskName;
	if(diskName.empty()) {
		diskName = "Floppy";
	}
	size_t pos = diskName.find_last_of("/\\");
	if(pos != string::npos) {
		diskName = diskName.substr(pos + 1);
	}
	rootNode.name = diskName;

	string json;
	json.reserve(4096);
	AppendJson(rootNode, json);
	outJson = std::move(json);
	return 1;
}

unsigned char FloppyDriveController::Read(unsigned char nPort)
{
	UpdateActiveState();

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
	UpdateActiveState();

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
	UpdateActiveState();

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
	UpdateActiveState();

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
	// 更新活动状态（可能进入/离开 Result）
	thiz->UpdateActiveState();

	thiz->nFDCStatus[0] = FDC_S0_IC1;
	thiz->bFdcResults[0] = thiz->nFDCStatus[0];
	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcReadTrack(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;

	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcSpecify(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

	thiz->bFdcPhase = FDC_PH_RESULT;
	thiz->nFdcMainStatus |= FDC_MS_DATA_IN; // fdc to host
}

void FloppyDriveController::FdcWriteData(FloppyDriveController* thiz)
{
	// 进入读/写执行阶段，更新活动状态
	thiz->UpdateActiveState();

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
	// 进入读/写执行阶段，更新活动状态
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

	unsigned char US;

	US = thiz->bFdcCommands[1] & 3;

	thiz->nFDCStatus[0] = US ? (FDC_S0_SE | FDC_S0_IC0) : FDC_S0_SE;

	thiz->bFdcIrq = 1;
	thiz->bFdcPhase = FDC_PH_IDLE;
}

void FloppyDriveController::FdcSenseIntStatus(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcReadID(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcFormatTrack(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

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
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcScanLowOrEqual(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

void FloppyDriveController::FdcScanHighOrEqual(FloppyDriveController* thiz)
{
	thiz->UpdateActiveState();

	thiz->bFdcPhase = (thiz->nFdcMainStatus & FDC_MS_EXECUTION) ?
		FDC_PH_EXECUTION : FDC_PH_RESULT;
}

// 更新并发送软驱 I/O 活动状态通知（仅在状态变化时发送，避免重复）
void FloppyDriveController::UpdateActiveState()
{
	int active = (pDiskFile && (bFdcPhase != FDC_PH_IDLE || bFdcDataBytes > 0)) ? 1 : 0;
	if(active != bFdcActiveState) {
		bFdcActiveState = active;
		if(_emu) {
			_emu->GetNotificationManager()->SendNotification(active ? ConsoleNotificationType::FloppyIoStarted : ConsoleNotificationType::FloppyIoStopped);
		}
	}
}

// 返回是否正在进行 I/O（读或写）操作。此函数用于外部查询软驱活动状态。
int FloppyDriveController::IsActive()
{
	// pDiskFile 不为空且处于非空闲阶段或有待传输数据则视为活动中
	if(pDiskFile && (bFdcPhase != FDC_PH_IDLE || bFdcDataBytes > 0)) {
		return 1;
	}
	return 0;
}

/**
 * 将主机缓冲区写入当前加载的 FAT12 镜像（写入根目录）。
 * 简化实现：仅支持写入根目录（不处理长文件名 LFN），目标文件名会被转换为短名 8.3，
 * 若镜像中存在同名短名文件则覆盖其内容。
 */
int FloppyDriveController::AddFileFromBuffer(const char* filename, const unsigned char* data, unsigned int length)
{
	if(!pDiskFile || !filename) return 0;
	// 不在 I/O 活动期间写入，以避免与正在运行的 FDC 操作冲突
	if(IsActive()) return 0;

	FilePositionGuard guard(pDiskFile);

	Fat12Context ctx;
	if(!LoadFat12Context(pDiskFile, nDiskSize, ctx)) {
		return 0;
	}

	vector<uint8_t> fatData;
	if(!LoadFatTable(pDiskFile, ctx, fatData)) {
		return 0;
	}

	// 读取根目录表
	if(ctx.rootEntryCount == 0) return 0;
	vector<uint8_t> rootBuffer((size_t)ctx.rootEntryCount * 32);
	if(!ReadBytes(pDiskFile, (long)ctx.rootDirOffset, rootBuffer.data(), rootBuffer.size())) {
		return 0;
	}

	// 计算短文件名（简单实现：取文件名部分，转大写，非字母数字替换为 '_'，截断）
	string name(filename);
	size_t pos = name.find_last_of("/\\");
	if(pos != string::npos) name = name.substr(pos + 1);
	// 转为 UTF-8 的 ASCII 子集（简单处理）并大写
	string up;
	for(char ch : name) up.push_back((char)toupper((unsigned char)ch));

	string base = up;
	string ext = "";
	size_t dot = up.find_last_of('.');
	if(dot != string::npos) {
		ext = base.substr(dot + 1);
		base = base.substr(0, dot);
	}

	string shortName(8, ' ');
	string shortExt(3, ' ');
	int idx = 0;
	for(char ch : base) {
		if(idx >= 8) break;
		char c = ch;
		if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
		shortName[idx++] = c;
	}
	idx = 0;
	for(char ch : ext) {
		if(idx >= 3) break;
		char c = ch;
		if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) c = '_';
		shortExt[idx++] = c;
	}

	// 查找可用目录项或者同名项以便覆盖
	int freeEntryIndex = -1;
	for(uint32_t i = 0; i < ctx.rootEntryCount; i++) {
		uint8_t first = rootBuffer[i * 32];
		if(first == 0x00) {
			if(freeEntryIndex == -1) freeEntryIndex = (int)i;
			break; // 后续均为空
		}
		if(first == 0xE5) {
			if(freeEntryIndex == -1) freeEntryIndex = (int)i;
			continue;
		}
		uint8_t attr = rootBuffer[i * 32 + 11];
		if(attr == 0x0F) continue; // LFN

		bool match = true;
		for(int k = 0; k < 8; k++) {
			if(rootBuffer[i * 32 + k] != (uint8_t)shortName[k]) { match = false; break; }
		}
		for(int k = 0; k < 3 && match; k++) {
			if(rootBuffer[i * 32 + 8 + k] != (uint8_t)shortExt[k]) { match = false; break; }
		}
		if(match) {
			freeEntryIndex = (int)i; // 覆盖该条目
			// 释放原有簇链
			uint16_t firstCluster = (uint16_t)(rootBuffer[i * 32 + 26] | (rootBuffer[i * 32 + 27] << 8));
			if(firstCluster >= 2) {
				uint16_t cluster = firstCluster;
				uint32_t maxIter = ctx.totalClusters + 2;
				uint32_t iter = 0;
				while(cluster >= 2 && cluster < 0xFF0 && iter++ < maxIter) {
					uint16_t next = ReadFatValue(fatData, cluster);
					WriteFatValue(fatData, cluster, 0);
					if(next >= 0xFF8 || next == 0) break;
					cluster = next;
				}
			}
			break;
		}
	}
	if(freeEntryIndex == -1) return 0; // 没有可用目录项

	// 计算需要的簇数并寻找空闲簇
	uint32_t clustersNeeded = 0;
	if(length > 0) clustersNeeded = (length + ctx.bytesPerCluster - 1) / ctx.bytesPerCluster;

	vector<uint16_t> freeClusters;
	for(uint16_t c = 2; c < (uint16_t)(ctx.totalClusters + 2) && freeClusters.size() < clustersNeeded; ++c) {
		if(ReadFatValue(fatData, c) == 0) freeClusters.push_back(c);
	}
	if(freeClusters.size() < clustersNeeded) return 0; // 空间不足

	// 将簇链写入 FAT（12-bit 编码）
	for(size_t i = 0; i < freeClusters.size(); ++i) {
		uint16_t cur = freeClusters[i];
		uint16_t val = (uint16_t)((i + 1 < freeClusters.size()) ? freeClusters[i + 1] : 0xFFF);
		if(!WriteFatValue(fatData, cur, val)) return 0;
	}

	// 将文件数据写到相应簇
	const unsigned char* ptr = data;
	size_t remaining = length;
	for(size_t i = 0; i < freeClusters.size(); ++i) {
		uint16_t cluster = freeClusters[i];
		long offset = (long)(ctx.dataOffset + (uint32_t)(cluster - 2) * ctx.bytesPerCluster);
		if(::fseek(pDiskFile, offset, SEEK_SET) != 0) return 0;
		size_t toWrite = remaining > ctx.bytesPerCluster ? ctx.bytesPerCluster : remaining;
		if(toWrite > 0) {
			if(::fwrite(ptr, 1, toWrite, pDiskFile) != toWrite) return 0;
			ptr += toWrite;
			remaining -= toWrite;
		}
		// 若簇未被填满，补零
		if(toWrite < ctx.bytesPerCluster) {
			uint32_t fill = ctx.bytesPerCluster - (uint32_t)toWrite;
			static vector<uint8_t> zeroBuf;
			if(zeroBuf.size() < fill) zeroBuf.resize(fill);
			if(::fwrite(zeroBuf.data(), 1, fill, pDiskFile) != fill) return 0;
		}
	}

	// 将修改后的 FAT 写回磁盘（所有 FAT 副本）
	uint32_t fatSize = ctx.sectorsPerFat * ctx.bytesPerSector;
	for(uint32_t copy = 0; copy < ctx.fatCount; ++copy) {
		long fatPos = (long)(ctx.fatOffset + copy * fatSize);
		if(::fseek(pDiskFile, fatPos, SEEK_SET) != 0) return 0;
		if(::fwrite(fatData.data(), 1, fatData.size(), pDiskFile) != fatData.size()) return 0;
	}

	// 写入或覆盖根目录项
	uint8_t entry[32];
	memset(entry, 0, sizeof(entry));
	memcpy(entry, shortName.c_str(), 8);
	memcpy(entry + 8, shortExt.c_str(), 3);
	entry[11] = 0x20; // Archive
	uint16_t firstClusterAssigned = freeClusters.empty() ? 0 : freeClusters[0];
	entry[26] = (uint8_t)(firstClusterAssigned & 0xFF);
	entry[27] = (uint8_t)((firstClusterAssigned >> 8) & 0xFF);
	entry[28] = (uint8_t)(length & 0xFF);
	entry[29] = (uint8_t)((length >> 8) & 0xFF);
	entry[30] = (uint8_t)((length >> 16) & 0xFF);
	entry[31] = (uint8_t)((length >> 24) & 0xFF);

	memcpy(rootBuffer.data() + freeEntryIndex * 32, entry, 32);

	// 将根目录写回文件
	if(::fseek(pDiskFile, ctx.rootDirOffset, SEEK_SET) != 0) return 0;
	if(::fwrite(rootBuffer.data(), 1, rootBuffer.size(), pDiskFile) != rootBuffer.size()) return 0;

	::fflush(pDiskFile);

	return 1;
}

/**
 * 获取镜像中指定文件的大小（字节）。
 * @param filename 文件名（支持长/短文件名，与 JSON 中名称一致）。
 * @return 返回文件大小（字节），未找到或发生错误返回 0。
 */
int FloppyDriveController::GetFileSize(const char* filename)
{
	if(!pDiskFile || !filename) return 0;

	FilePositionGuard guard(pDiskFile);

	Fat12Context ctx;
	if(!LoadFat12Context(pDiskFile, nDiskSize, ctx)) return 0;

	vector<uint8_t> fatData;
	if(!LoadFatTable(pDiskFile, ctx, fatData)) return 0;

	if(ctx.rootEntryCount == 0) return 0;
	vector<uint8_t> rootBuffer((size_t)ctx.rootEntryCount * 32);
	if(!ReadBytes(pDiskFile, (long)ctx.rootDirOffset, rootBuffer.data(), rootBuffer.size())) return 0;

	vector<FdcFileNode> children;
	ParseDirectoryData(pDiskFile, ctx, fatData, rootBuffer.data(), rootBuffer.size(), children);

	uint16_t firstCluster = 0;
	uint32_t size = 0;
	if(FindFileNodeByName(children, filename, firstCluster, size)) {
		return (int)size;
	}
	return 0;
}

/**
 * 从镜像中读取指定文件的数据到调用方缓冲区。
 * @param filename 要读取的文件名（与 GetFileSize 相同的匹配规则）。
 * @param outBuffer 输出缓冲区指针。
 * @param maxLength 输出缓冲区最大长度。
 * @return 返回实际写入的字节数，失败或未找到返回 0。
 */
int FloppyDriveController::ReadFileToBuffer(const char* filename, unsigned char* outBuffer, uint32_t maxLength)
{
	if(!pDiskFile || !filename || !outBuffer || maxLength == 0) return 0;

	FilePositionGuard guard(pDiskFile);

	Fat12Context ctx;
	if(!LoadFat12Context(pDiskFile, nDiskSize, ctx)) return 0;

	vector<uint8_t> fatData;
	if(!LoadFatTable(pDiskFile, ctx, fatData)) return 0;

	if(ctx.rootEntryCount == 0) return 0;
	vector<uint8_t> rootBuffer((size_t)ctx.rootEntryCount * 32);
	if(!ReadBytes(pDiskFile, (long)ctx.rootDirOffset, rootBuffer.data(), rootBuffer.size())) return 0;

	vector<FdcFileNode> children;
	ParseDirectoryData(pDiskFile, ctx, fatData, rootBuffer.data(), rootBuffer.size(), children);

	uint16_t firstCluster = 0;
	uint32_t size = 0;
	if(!FindFileNodeByName(children, filename, firstCluster, size)) return 0;

	vector<uint8_t> fileData;
	if(firstCluster >= 2) {
		if(!ReadClusterChain(pDiskFile, ctx, fatData, firstCluster, fileData)) return 0;
	}

	uint32_t toCopy = size <= maxLength ? (uint32_t)size : maxLength;
	if(toCopy > 0) {
		if(fileData.size() < toCopy) {
			// 数据不完整，仅拷贝可用部分
			toCopy = (uint32_t)fileData.size();
		}
		if(toCopy > 0) memcpy(outBuffer, fileData.data(), toCopy);
	}

	return (int)toCopy;
}

	/**
	 * 从镜像根目录中删除指定文件的实现（仅限根目录）。
	 * 实现策略：
	 * 1) 根据 ParseDirectoryData/FindFileNodeByName 查找文件的首簇和大小；
	 * 2) 在根目录表中找到对应的目录项并将首字节标记为 0xE5（已删除）；
	 * 3) 释放该文件占用的 FAT 簇链（将 FAT 条目置 0），并写回所有 FAT 副本；
	 * 4) 将根目录表写回镜像并刷新。
	 * 仅针对根目录项执行删除（不递归），并在出现 I/O 活动或错误时返回失败。
	 */
	int FloppyDriveController::DeleteFileByName(const char* filename)
	{
		if(!pDiskFile || !filename) return 0;
		if(IsActive()) return 0; // 正在 I/O 时拒绝修改

		FilePositionGuard guard(pDiskFile);

		Fat12Context ctx;
		if(!LoadFat12Context(pDiskFile, nDiskSize, ctx)) return 0;

		vector<uint8_t> fatData;
		if(!LoadFatTable(pDiskFile, ctx, fatData)) return 0;

		if(ctx.rootEntryCount == 0) return 0;
		vector<uint8_t> rootBuffer((size_t)ctx.rootEntryCount * 32);
		if(!ReadBytes(pDiskFile, (long)ctx.rootDirOffset, rootBuffer.data(), rootBuffer.size())) return 0;

		// 使用现有解析逻辑寻找文件的首簇与大小
		vector<FdcFileNode> children;
		ParseDirectoryData(pDiskFile, ctx, fatData, rootBuffer.data(), rootBuffer.size(), children);

		uint16_t firstCluster = 0;
		uint32_t size = 0;
		if(!FindFileNodeByName(children, filename, firstCluster, size)) {
			return 0; // 未找到
		}

		// 在根目录原始表中定位对应的短目录项索引（跳过 LFN 条目）
		int entryIndex = -1;
		for(uint32_t i = 0; i < ctx.rootEntryCount; ++i) {
			uint8_t first = rootBuffer[i * 32];
			if(first == 0x00) break;
			uint8_t attr = rootBuffer[i * 32 + 11];
			if(attr == 0x0F) continue; // LFN
			uint16_t fc = (uint16_t)(rootBuffer[i * 32 + 26] | (rootBuffer[i * 32 + 27] << 8));
			uint32_t fsize = (uint32_t)(rootBuffer[i * 32 + 28] | (rootBuffer[i * 32 + 29] << 8) | (rootBuffer[i * 32 + 30] << 16) | (rootBuffer[i * 32 + 31] << 24));
			if(fc == firstCluster && fsize == size) {
				entryIndex = (int)i;
				break;
			}
		}

		// 若未通过簇/大小定位到目录项，则回退使用长名/短名解析匹配（复制 ParseDirectoryData 的解析方式）
		if(entryIndex == -1) {
			vector<std::u16string> longNameParts;
			for(uint32_t i = 0; i < ctx.rootEntryCount; ++i) {
				const uint8_t* entry = rootBuffer.data() + i * 32;
				uint8_t first = entry[0];
				if(first == 0x00) break;
				uint8_t attr = entry[11];
				if(attr == 0x0F) {
					uint8_t sequence = entry[0] & 0x1F;
					if((entry[0] & 0x40) != 0) {
						longNameParts.clear();
						longNameParts.resize(sequence);
					}
					if(sequence >= 1 && sequence <= longNameParts.size()) {
						longNameParts[sequence - 1] = ExtractLongNameSegment(entry);
					}
					continue;
				}
				if(first == 0xE5) { longNameParts.clear(); continue; }
				if(attr & 0x08) { longNameParts.clear(); continue; }

				string name;
				if(!longNameParts.empty()) {
					name = BuildLongName(longNameParts);
				}
				if(name.empty()) {
					name = BuildShortName(entry, entry + 8);
				}
				longNameParts.clear();

				if(name == filename) {
					entryIndex = (int)i;
					break;
				}
			}
		}

		if(entryIndex == -1) return 0; // 仍未找到

		// 释放簇链（若存在首簇）
		if(firstCluster >= 2) {
			uint16_t cluster = firstCluster;
			uint32_t maxIter = ctx.totalClusters + 2;
			uint32_t iter = 0;
			while(cluster >= 2 && cluster < 0xFF0 && iter++ < maxIter) {
				uint16_t next = ReadFatValue(fatData, cluster);
				if(!WriteFatValue(fatData, cluster, 0)) return 0;
				if(next >= 0xFF8 || next == 0) break;
				cluster = next;
			}
		}

		// 标记目录项为已删除
		rootBuffer[entryIndex * 32] = 0xE5;

		// 将修改后的根目录写回
		if(::fseek(pDiskFile, ctx.rootDirOffset, SEEK_SET) != 0) return 0;
		if(::fwrite(rootBuffer.data(), 1, rootBuffer.size(), pDiskFile) != rootBuffer.size()) return 0;

		// 将修改后的 FAT 写回所有副本
		uint32_t fatSize = ctx.sectorsPerFat * ctx.bytesPerSector;
		for(uint32_t copy = 0; copy < ctx.fatCount; ++copy) {
			long fatPos = (long)(ctx.fatOffset + copy * fatSize);
			if(::fseek(pDiskFile, fatPos, SEEK_SET) != 0) return 0;
			if(::fwrite(fatData.data(), 1, fatData.size(), pDiskFile) != fatData.size()) return 0;
		}

		::fflush(pDiskFile);

		return 1;
	}