#include "cart.h"

#include <cstdio>
#include <cstdlib>

uint8_t command_data[8];
uint32_t romctrl;
uint8_t data_out[4];

void Cartridge::SendCommandByte(uint8_t data, int index)
{
	printf("Adding command byte 0x%02x to %d\n", data, index);
	command_data[index] = data;
}

void Cartridge::WriteROMCTRL(uint32_t data)
{
	printf("Writing 0x%08x to romctrl\n", data);
	romctrl = data;
	if (romctrl & (1 << 31))
	{
		switch (command_data[0])
		{
		case 0x9f:
			for (int i = 0; i < 4; i++)
				data_out[i] = 0xff;
			printf("[emu/Cart]: Dummy initialization\n");
			break;
		case 0x00:
			for (int i = 0; i < 4; i++)
				data_out[i] = 0xff; // Fake the cartridge not being inserted
			printf("[emu/Cart]: Read header\n");
			break;
		case 0x90:
			*((uint32_t*)data_out) = 0x3FC2;
			printf("[emu/Cart]: Get chip ID\n");
			break;
		case 0x3C:
			break;
		default:
			printf("Unknown cartridge command 0x%02x%02x%02x%02x%02x%02x%02x%02x\n"
				, command_data[0], command_data[1], command_data[2], command_data[3], 
				command_data[4], command_data[5], command_data[6], command_data[7]);
			exit(1);
		}

		romctrl &= ~(1 << 31);
		romctrl |= (1 << 23);
	}
}

uint32_t Cartridge::ReadROMCTRL()
{
	printf("Reading from romctrl 0x%08x\n", romctrl);
	return romctrl;
}

uint8_t Cartridge::ReadDataOut(int index)
{
	printf("Reading result from most recent cartridge command\n");
	return data_out[index];
}

uint32_t Cartridge::ReadDataOut()
{
	return *(uint32_t*)data_out;
}
