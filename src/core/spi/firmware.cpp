#include "firmware.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>

enum FIRM_COMMAND
{
	NONE,
	READ_STREAM,
	READ_STATUS_REG
} command_id = FIRM_COMMAND::NONE;

uint32_t spicnt;
int selected_device = 0;
bool is_transferring = false;
uint8_t output = 0;
uint8_t* fw;
int total_fw_args = 0;
uint32_t address;
size_t size = 0;

void Firmware::LoadFirmware(std::string fw_path)
{
	std::ifstream file(fw_path, std::ios::ate | std::ios::binary);
	size = file.tellg();
	file.seekg(0, std::ios::beg);

	fw = new uint8_t[size];

	file.read((char*)fw, size);

	file.close();
}

void Firmware::WriteSPICNT(uint32_t data)
{
	printf("Writing 0x%04x to SPICNT\n", data);
	spicnt = data;
	selected_device = (data >> 8) & 0x3;
}

uint8_t HandleFirmwareCommands(uint8_t data)
{
	total_fw_args++;
	switch (command_id)
	{
	case FIRM_COMMAND::READ_STREAM:
		if (total_fw_args < 5)
		{
			address <<= 8;
			address |= data;
		}
		else
		{
			printf("Reading SPI from fw address 0x%08x\n", address);
			address++;
			return fw[(address - 1) & (size - 1)];
		}
		break;
	default:
		switch (data)
		{
		case 3:
			command_id = FIRM_COMMAND::READ_STREAM;
			break;
		default:
			printf("Unknown command 0x%x\n", data);
			exit(1);
		}
		break;
	}

	return data;
}

void Firmware::WriteSPIData(uint32_t data)
{
	if (spicnt & (1 << 15))
	{
		spicnt &= ~(1 << 7);

		switch (selected_device)
		{
		case 1:
			output = HandleFirmwareCommands(data);
			break;
		default:
			printf("Write to unknown device %d\n", selected_device);
			exit(1);
		}
	}
}

uint8_t Firmware::ReadSPIData()
{
	printf("Reading SPI bus\n");
	return output;
}
