/*------------------------------------------------------------------------
名称：BBK FD1 控制设备头文件
说明：实现 BBK 专用的键盘矩阵与EM84502 鼠标协议（用于 BBK BIOS）。
作者：Lion
邮箱：chengbin@3578.cn
日期：2025-10-12
备注：
------------------------------------------------------------------------*/

#pragma once
#include "pch.h"
#include "Shared/BaseControlDevice.h"
#include "Shared/KeyManager.h"
#include "Shared/EmuSettings.h"
#include "Utilities/Serializer.h"

// 描述：BBK FD1 设备实现，作为 Mapper 的输入设备（MapperInputPort）。
// 功能：实现 ReadRam(0x4016/0x4017) 与 WriteRam(0x4016) 的行为，支持
//       键盘矩阵轮询与 EM84502 鼠标命令/数据传输。
class BbkFd1 : public BaseControlDevice
{
private:
	// 键盘扫描状态
	bool _bOut = false;
	uint8_t _scanNo = 0; // 0..13

	// 鼠标子系统
	bool _mouseActived = false;
	int _mouseKey = 0;
	int _mouseDeltaX = 0;
	int _mouseDeltaY = 0;

	// 4016/4017 读写计数（用于启动鼠标读序列）
	uint8_t _prev4016 = 0;
	int _read4016Count = 0;
	int _read4017Count = 0;

	// EM84502 状态机（接收命令 / 发送数据）
	enum EM_STATE { EMS_IDLE = 0, EMS_RX_COMMAND, EMS_TX_DATA };
	int _emState = EMS_IDLE;
	uint32_t _emCommand = 0;
	uint8_t _emBitCount = 0;
	uint32_t _emTxData = 0; // 32-bit wave data
	uint8_t _emTxCount = 0; // number of bytes left to send (1..4)
	uint8_t _emData[3] = { 0 };

	// 按键位定义：列出按键并为其分配位编号。
	enum Buttons
	{
		K_4, K_G, K_F, K_C, K_F2, K_E, K_5, K_V,
		K_2, K_D, K_S, K_END, K_F1, K_W, K_3, K_X,
		K_INSERT, K_BACK, K_NEXT, K_RIGHT, K_F8, K_PRIOR, K_DELETE, K_HOME,
		K_9, K_I, K_L, K_COMMA, K_F5, K_O, K_0, K_PERIOD,
		K_RBRACKET, K_RETURN, K_UP, K_LEFT, K_F7, K_LBRACKET, K_BACKSLASH, K_DOWN,
		K_Q, K_CAPS, K_Z, K_TAB, K_ESCAPE, K_A, K_1, K_LCONTROL,
		K_7, K_Y, K_K, K_M, K_F4, K_U, K_8, K_J,
		K_MINUS, K_SEMICOLON, K_APOSTROPHE, K_SLASH, K_F6, K_P, K_EQUALS, K_LSHIFT, K_RSHIFT,
		K_T, K_H, K_N, K_SPACE, K_F3, K_R, K_6, K_B,
		K_F11, K_F12,
		K_SUBTRACT, K_ADD, K_MULTIPLY, K_F10, K_DIVIDE, K_NUMLOCK,
		K_GRAVE, K_LALT, K_F9, K_DECIMAL,
		ButtonsCount
	};

	// 生成 EM84502 波形编码。
	static uint32_t MouseGenTxData(uint8_t data)
	{
		int i;
		uint32_t txd = 0;
		uint32_t mask = 0x10;
		int parity = 0;

		// start 4 bits 0b1011
		txd = 0x0B;

		// 8 data bits, each encoded as 3'b10d
		for(i = 0; i < 8; i++) {
			if((data & 1) == 0) {
				txd |= mask;
				parity ^= 1;
			}
			txd |= (mask << 2);
			mask <<= 3;
			data >>= 1;
		}

		// parity bits 3'b10p
		if(parity) txd |= mask;
		mask <<= 2;
		txd |= mask;
		// final 1'b0 implicit

		return txd;
	}

protected:
	// 从输入系统读取按键/鼠标状态
	void InternalSetStateFromInput() override
	{
		// 动态从设置读取按键映射（仅在设置非空时覆盖设备内的映射），
		// 这样 Mapper 可以在 InitMapper 中注入默认或自定义的映射，而不被每帧覆盖。
		auto settingsMappings = _emu->GetSettings()->GetNesConfig().MapperInput.Keys.GetKeyMappingArray();
		if(!settingsMappings.empty()) {
			_keyMappings = settingsMappings;
		}

		// 将 CustomKeys 映射到我们定义的 Buttons 列表，保持与其他设备一致的映射策略
		for(KeyMapping& km : _keyMappings) {
			for(int i = 0; i < (int)ButtonsCount && i < 100; i++) {
				SetPressedState(i, km.CustomKeys[i]);
			}
		}

		// 鼠标：使用 KeyManager 的鼠标移动量
		MouseMovement mov = KeyManager::GetMouseMovement(_emu, _emu->GetSettings()->GetInputConfig().MouseSensitivity);
		_mouseDeltaX += mov.dx;
		_mouseDeltaY += mov.dy;
		SetMovement(mov);

		int mouseButtons = 0;
		if(KeyManager::IsMouseButtonPressed(MouseButton::RightButton)) {
			mouseButtons |= 0x01;
		}
		if(KeyManager::IsMouseButtonPressed(MouseButton::MiddleButton)) {
			mouseButtons |= 0x02;
		}
		if(KeyManager::IsMouseButtonPressed(MouseButton::LeftButton)) {
			mouseButtons |= 0x04;
		}
		_mouseKey = mouseButtons;
	}

	void Serialize(Serializer& s) override
	{
		BaseControlDevice::Serialize(s);
		SV(_bOut); SV(_scanNo);
		SV(_mouseActived); SV(_mouseKey); SV(_mouseDeltaX); SV(_mouseDeltaY);
		SV(_read4016Count); SV(_read4017Count);
		SV(_emState); SV(_emCommand); SV(_emBitCount); SV(_emTxData); SV(_emTxCount);
		SVArray(_emData, 3);
	}

	void RefreshStateBuffer() override
	{
		// 当 EM84502 在发送数据阶段时，需要准备下一次返回位流
		if(_emState == EMS_TX_DATA) {
			// 如果正在发送，则 _emTxData 已包含当前字节的编码位流，由 ReadRam 按位消费
			// 当该字节的 32 位消耗完毕后，下一次会由 Write4016/Read4017 切换到下一个字节
		}
	}

public:
	BbkFd1(Emulator* emu) : BaseControlDevice(emu, ControllerType::BbkFd1, BaseControlDevice::MapperInputPort)
	{
	}

	// 描述：由 Mapper 主动注入按键映射（直接从 Mapper 中设置），用于在不依赖 UI 回调时
	//      将 MapperInput 的绑定直接传给设备。参数 mappings 为设置中的 KeyMapping 数组。
	// 参数：mappings 映射数组（会被复制到设备内部用于 IsPressed 判定）。
	void SetKeyMappings(const std::vector<KeyMapping>& mappings)
	{
		_keyMappings = mappings;
	}

	// 该设备使用鼠标移动量作为坐标数据，声明 HasCoordinates 为 true
	// 仅修改本设备文件，严格不改框架源码。
	bool HasCoordinates() override { return true; }

	// ReadRam / WriteRam: 映射到 0x4016 / 0x4017 的行为
	uint8_t ReadRam(uint16_t addr) override
	{
		if(addr == 0x4016) {
			// Read4016 行为：用于触发鼠标读序列
			_read4016Count++;
			if(!_mouseActived && _read4016Count >= 9) {
				_mouseActived = true;
				_emState = EMS_IDLE;
			}
			return 0;
		} else if(addr == 0x4017) {
			// Read4017 行为：当鼠标激活时输出 EM84502 的位流，否则输出键盘矩阵位
			_read4016Count = 0;

			uint8_t data = 0xFE; // high bits set

			if(_mouseActived) {
				switch(_emState) {
					case EMS_IDLE:
						data |= 0x01; // line idle
						break;
					case EMS_RX_COMMAND:
						if((_read4017Count & 1) == 0) data |= 0x01;
						_read4017Count++;
						break;
					case EMS_TX_DATA:
						if(_read4017Count < 32) {
							if(_emTxData & (1u << _read4017Count)) data |= 0x01;
							_read4017Count++;
						} else {
							_read4017Count = 0;
							if(_emTxCount > 0) {
								// 准备下一字节（如果有）
								_emTxCount--;
								if(_emTxCount > 0) {
									uint8_t nextByte = _emData[3 - _emTxCount];
									_emTxData = MouseGenTxData(nextByte);
								} else {
									_mouseActived = false;
									_emState = EMS_IDLE;
								}
							}
						}
						break;
				}
			}

			// 精确的键盘矩阵映射
			switch(_scanNo) {
				case 1:
					if(_bOut) {
						if(IsPressed(K_4)) data &= ~0x02;
						if(IsPressed(K_G)) data &= ~0x04;
						if(IsPressed(K_F)) data &= ~0x08;
						if(IsPressed(K_C)) data &= ~0x10;
					} else {
						if(IsPressed(K_F2)) data &= ~0x02;
						if(IsPressed(K_E)) data &= ~0x04;
						if(IsPressed(K_5)) data &= ~0x08;
						if(IsPressed(K_V)) data &= ~0x10;
					}
					break;
				case 2:
					if(_bOut) {
						if(IsPressed(K_2)) data &= ~0x02;
						if(IsPressed(K_D)) data &= ~0x04;
						if(IsPressed(K_S)) data &= ~0x08;
						if(IsPressed(K_END)) data &= ~0x10;
					} else {
						if(IsPressed(K_F1)) data &= ~0x02;
						if(IsPressed(K_W)) data &= ~0x04;
						if(IsPressed(K_3)) data &= ~0x08;
						if(IsPressed(K_X)) data &= ~0x10;
					}
					break;
				case 3:
					if(_bOut) {
						if(IsPressed(K_INSERT)) data &= ~0x02;
						if(IsPressed(K_BACK)) data &= ~0x04;
						if(IsPressed(K_NEXT)) data &= ~0x08;
						if(IsPressed(K_RIGHT)) data &= ~0x10;
					} else {
						if(IsPressed(K_F8)) data &= ~0x02;
						if(IsPressed(K_PRIOR)) data &= ~0x04;
						if(IsPressed(K_DELETE)) data &= ~0x08;
						if(IsPressed(K_HOME)) data &= ~0x10;
					}
					break;
				case 4:
					if(_bOut) {
						if(IsPressed(K_9)) data &= ~0x02;
						if(IsPressed(K_I)) data &= ~0x04;
						if(IsPressed(K_L)) data &= ~0x08;
						if(IsPressed(K_COMMA)) data &= ~0x10;
					} else {
						if(IsPressed(K_F5)) data &= ~0x02;
						if(IsPressed(K_O)) data &= ~0x04;
						if(IsPressed(K_0)) data &= ~0x08;
						if(IsPressed(K_PERIOD)) data &= ~0x10;
					}
					break;
				case 5:
					if(_bOut) {
						if(IsPressed(K_RBRACKET)) data &= ~0x02;
						if(IsPressed(K_RETURN)) data &= ~0x04;
						if(IsPressed(K_UP)) data &= ~0x08;
						if(IsPressed(K_LEFT)) data &= ~0x10;
					} else {
						if(IsPressed(K_F7)) data &= ~0x02;
						if(IsPressed(K_LBRACKET)) data &= ~0x04;
						if(IsPressed(K_BACKSLASH)) data &= ~0x08;
						if(IsPressed(K_DOWN)) data &= ~0x10;
					}
					break;
				case 6:
					if(_bOut) {
						if(IsPressed(K_Q)) data &= ~0x02;
						if(IsPressed(K_CAPS)) data &= ~0x04;
						if(IsPressed(K_Z)) data &= ~0x08;
						if(IsPressed(K_TAB)) data &= ~0x10;
					} else {
						if(IsPressed(K_ESCAPE)) data &= ~0x02;
						if(IsPressed(K_A)) data &= ~0x04;
						if(IsPressed(K_1)) data &= ~0x08;
						if(IsPressed(K_LCONTROL)) data &= ~0x10;
					}
					break;
				case 7:
					if(_bOut) {
						if(IsPressed(K_7)) data &= ~0x02;
						if(IsPressed(K_Y)) data &= ~0x04;
						if(IsPressed(K_K)) data &= ~0x08;
						if(IsPressed(K_M)) data &= ~0x10;
					} else {
						if(IsPressed(K_F4)) data &= ~0x02;
						if(IsPressed(K_U)) data &= ~0x04;
						if(IsPressed(K_8)) data &= ~0x08;
						if(IsPressed(K_J)) data &= ~0x10;
					}
					break;
				case 8:
					if(_bOut) {
						if(IsPressed(K_MINUS)) data &= ~0x02;
						if(IsPressed(K_SEMICOLON)) data &= ~0x04;
						if(IsPressed(K_APOSTROPHE)) data &= ~0x08;
						if(IsPressed(K_SLASH)) data &= ~0x10;
					} else {
						if(IsPressed(K_F6)) data &= ~0x02;
						if(IsPressed(K_P)) data &= ~0x04;
						if(IsPressed(K_EQUALS)) data &= ~0x08;
						if(IsPressed(K_LSHIFT) || IsPressed(K_RSHIFT)) data &= ~0x10;
					}
					break;
				case 9:
					if(_bOut) {
						if(IsPressed(K_T)) data &= ~0x02;
						if(IsPressed(K_H)) data &= ~0x04;
						if(IsPressed(K_N)) data &= ~0x08;
						if(IsPressed(K_SPACE)) data &= ~0x10;
					} else {
						if(IsPressed(K_F3)) data &= ~0x02;
						if(IsPressed(K_R)) data &= ~0x04;
						if(IsPressed(K_6)) data &= ~0x08;
						if(IsPressed(K_B)) data &= ~0x10;
					}
					break;
				case 10:
					if(!_bOut) {
						data &= ~0x02;
					}
					break;
				case 11:
					if(_bOut) {
						if(IsPressed(K_F11)) data &= ~0x10;
					} else {
						if(IsPressed(K_F12)) data &= ~0x02;
					}
					break;
				case 12:
					if(_bOut) {
						if(IsPressed(K_SUBTRACT)) data &= ~0x02;
						if(IsPressed(K_ADD)) data &= ~0x04;
						if(IsPressed(K_MULTIPLY)) data &= ~0x08;
					} else {
						if(IsPressed(K_F10)) data &= ~0x02;
						if(IsPressed(K_DIVIDE)) data &= ~0x08;
						if(IsPressed(K_NUMLOCK)) data &= ~0x10;
					}
					break;
				case 13:
					if(_bOut) {
						if(IsPressed(K_GRAVE)) data &= ~0x02;
						if(IsPressed(K_LALT) || IsPressed(K_LALT)) data &= ~0x08;
						if(IsPressed(K_TAB)) data &= ~0x10;
					} else {
						if(IsPressed(K_F9)) data &= ~0x02;
						if(IsPressed(K_DECIMAL)) data &= ~0x08;
					}
					break;
				default:
					break;
			}

			return data;
		}
		return 0;
	}

	void WriteRam(uint16_t addr, uint8_t value) override
	{
		// 处理 0x4016 写入（strobe）行为
		if(addr == 0x4016) {
			_read4016Count = 0;

			if(_mouseActived) {
				switch(_emState) {
					case EMS_IDLE:
						if(value == 0x05) {
							_read4017Count = 0;
							_emCommand = 0;
							_emBitCount = 0;
							_emState = EMS_RX_COMMAND;
						}
						break;
					case EMS_RX_COMMAND:
						if(value & 4) {
							// 收集位（反向）
							if((value & 1) == 0) _emCommand |= (1u << _emBitCount);
							_emBitCount++;
							if(_emBitCount == 22) {
								uint8_t cmd = _emCommand & 0xFF;
								_emTxData = MouseGenTxData(0xFA); // response
								if(cmd == 0xF0) {
									_emTxCount = 1;
								} else if(cmd == 0xEB) {
									// read data
									int dx = -_mouseDeltaX;
									int dy = _mouseDeltaY;
									_mouseDeltaX = 0; _mouseDeltaY = 0;
									uint8_t flag = 0;
									if(dx < 0) flag |= 0x20;
									if(dy < 0) flag |= 0x10;
									if(dx > 255 || dx < -256) { flag |= 0x80; dx = 0; }
									if(dy > 255 || dy < -256) { flag |= 0x40; dy = 0; }
									_emData[0] = _mouseKey | flag;
									_emData[1] = (uint8_t)dy;
									_emData[2] = (uint8_t)dx;
									_emTxCount = 4;
								} else {
									// 未知命令，忽略
									_emTxCount = 0;
								}
								_read4017Count = 0;
								_emBitCount = 0;
								_emState = (_emTxCount > 0) ? EMS_TX_DATA : EMS_IDLE;
							}
						}
						break;
					case EMS_TX_DATA:
						// 发送中，写入不改变状态
						break;
				}
			}

			// 键盤輪詢控制
			if(value & 4) {
				if(value == 0x05) {
					_bOut = false;
					_scanNo = 0;
				} else if(value == 0x04) {
					if(++_scanNo > 13) _scanNo = 0;
					_bOut = !_bOut;
				} else if(value == 0x06) {
					_bOut = !_bOut;
				}
			}
		} else {
			// 非 4016 写入仅执行 strobe 处理
			StrobeProcessWrite(value);
		}
	}
};