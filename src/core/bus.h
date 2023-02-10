#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace Bus
{

void AddARMBios(std::string file_name, bool is_arm9);
void LoadNDS(std::string file);

void Write32(uint32_t addr, uint32_t data);
void Write16(uint32_t addr, uint16_t data);
void Write8(uint32_t addr, uint8_t data);

uint32_t Read32(uint32_t addr);
uint16_t Read16(uint32_t addr);
uint8_t Read8(uint32_t addr);

void Write8_ARM7(uint32_t addr, uint8_t data);
void Write16_ARM7(uint32_t addr, uint16_t data);
void Write32_ARM7(uint32_t addr, uint32_t data);

uint32_t Read32_ARM7(uint32_t addr);
uint16_t Read16_ARM7(uint32_t addr);
uint8_t Read8_ARM7(uint32_t addr);

void RemapDTCM(uint32_t addr);

void TriggerInterrupt7(int i);
bool IsInterruptAvailable7();

enum class Keys
{
	KEY_A,
	KEY_B,
	KEY_SELECT,
	KEY_START,
	KEY_RIGHT,
	KEY_LEFT,
	KEY_UP,
	KEY_DOWN,
	KEY_R,
	KEY_L
};

void PressKey(Keys k);
void ReleaseKey(Keys k);
void ResetKeys();

void Dump();

}