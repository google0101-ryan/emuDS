#include "cart.h"

#include <cstdio>
#include <cstdlib>
#include <src/core/bus.h>

uint8_t command_data[8];
uint32_t romctrl;
uint32_t data_output;

void Cartridge::SendCommandByte(uint8_t data, int index)
{
	printf("Adding command byte 0x%02x to %d\n", data, index);
	command_data[index] = data;
}

int bytes_left;

enum Command
{
	DUMMY,
	READ_HEADER,
	GET_CHIP_ID,
	ENABLE_KEY1,
} cmd;

uint32_t data_pos = 0;
int cycles_left = 8;

void Cartridge::WriteROMCTRL(uint32_t data)
{
	bool old_transfer_busy = romctrl & (1 << 31);

	printf("Writing 0x%08x to romctrl\n", data);
	romctrl = data;

	uint8_t block_size = (romctrl >> 24) & 0x7;

	if (!old_transfer_busy && romctrl & (1 << 31))
	{
		romctrl &= ~(1 << 23);

		if (block_size == 0)
			bytes_left = 0;
		else if (block_size == 7)
			bytes_left = 4;
		else
			bytes_left = 0x100 << block_size;

		switch (command_data[0])
		{
		case 0x9f:
			cmd = Command::DUMMY;
			break;
		case 0x00:
			cmd = Command::READ_HEADER;
			data_pos = 0;
			break;
		case 0x90:
			cmd = Command::GET_CHIP_ID;
			break;
		case 0x3C:
			cmd = Command::ENABLE_KEY1;
			break;
		default:
			printf("Unknown cartridge command 0x%02x%02x%02x%02x%02x%02x%02x%02x\n"
				, command_data[0], command_data[1], command_data[2], command_data[3], 
				command_data[4], command_data[5], command_data[6], command_data[7]);
			exit(1);
		}
	}
}

uint32_t Cartridge::ReadROMCTRL()
{
	printf("Reading from romctrl 0x%08x\n", romctrl);
	return romctrl;
}

uint8_t Cartridge::ReadDataOut(int index)
{
	uint8_t* d = (uint8_t*)&data_output;
	return d[index];
}

uint32_t Cartridge::ReadDataOut()
{
	if (romctrl & (1 << 23))
	{
		romctrl &= ~(1 << 23);
		cycles_left = 8;
	}
	return data_output;
}

uint32_t auxspicnt = 0;

void Cartridge::WriteAUXSPICNT(uint32_t data)
{
	auxspicnt = data;
}

uint32_t Cartridge::ReadAUXSPICNT()
{
	return auxspicnt;
}

void Cartridge::Run(int cycles)
{
	bool block_busy = romctrl & (1 << 31);
	bool word_status = romctrl & (1 << 23);

	if (block_busy && !word_status)
	{
		cycles_left -= cycles;
		if (cycles_left > 0)
			return;
		
		cycles_left = 8;
		switch (cmd)
		{
		case Command::DUMMY:
			data_output = 0xFFFFFFFF;
			break;
		case Command::READ_HEADER:
			data_output = 0xFFFFFFFF;
			data_pos += 4;
			if (data_pos > 0xFFF)
				data_pos = 0;
			romctrl |= (1 << 23);
			break;
		case Command::GET_CHIP_ID:
			data_output = 0x3FC2;
			romctrl |= (1 << 23);
			break;
		case Command::ENABLE_KEY1:
			break;
		default:
			printf("Unknown command %d\n", cmd);
			exit(1);
		}
		bytes_left -= 4;
		if (bytes_left <= 0)
		{
			romctrl &= ~(1 << 31);
			if (auxspicnt & (1 << 14))
			{
				printf("Triggering Cart interrupt\n");
				Bus::TriggerInterrupt7(19);
			}
		}
	}
}
