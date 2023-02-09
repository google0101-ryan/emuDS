#include "bus.h"

#include <src/core/arm9/arm9.h>
#include <src/core/gpu/gpu.h>
#include <src/core/spi/rtc.h>
#include <src/core/spi/cart.h>
#include <src/core/spi/firmware.h>

#include <cassert>

uint8_t* arm9_bios; // The ARM9 and ARM7 have different BIOSes on different chips, so we keep them in seperate arrays
uint8_t* arm7_bios;

size_t arm9_bios_size;
size_t arm7_bios_size;

uint32_t dtcm_start = 0; // Keep track of the ARM9's DTCM, because it can be remapped via CP15
bool ime_arm9 = false; // IME controls whether interrupts are enabled globally, regardless of any other settings
bool ime_arm7 = false;

uint32_t ie_arm7, if_arm7;
uint32_t ie_arm9, if_arm9;

bool postflg_arm9 = false; // POSTFLG is used for making sure that games following a stray pointer don't execute BIOS code
bool postflg_arm7 = false;

uint8_t dtcm[0x4000]; // Data Tightly-Coupled memory, ARM9 only, remappable
uint8_t* arm9_ram; // ARM9's main RAM, PSRAM, 4MB

uint16_t arm9_ipcsync; // Used for synchronization primitives between ARM9 and ARM7
uint16_t arm7_ipcsync;

uint8_t* arm7_wram; // 16-KiB of ARM7-exclusive WRAM
uint8_t* shared_wram; // Shared WRAM; Each processor can have one 16KiB bank, or one processor gets all 32KiB

uint8_t* arm7_ram; // Same as ARM9's RAM, but for ARM7

int wramcnt = 0;

void Bus::AddARMBios(std::string file_name, bool is_arm9)
{
    std::ifstream bios(file_name, std::ios::ate | std::ios::binary);

    size_t size = bios.tellg();
    bios.seekg(0, std::ios::beg);

    if (is_arm9)
    {
        arm9_bios = new uint8_t[size];
        arm9_bios_size = size;
        bios.read((char*)arm9_bios, size);
    }
    else
    {
        arm7_bios = new uint8_t[size];
		arm7_bios_size = size;
        bios.read((char*)arm7_bios, size);
    }

	arm9_ram = new uint8_t[4*1024*1024];
	arm7_ram = new uint8_t[4*1024*1024];
	arm7_wram = new uint8_t[0x10000];
	shared_wram = new uint8_t[32*1024];
}

void Bus::Write32(uint32_t addr, uint32_t data)
{
	if (addr >= dtcm_start && addr < dtcm_start + 0x4000)
	{
		*(uint32_t*)&dtcm[addr - dtcm_start] = data;
		return;
	}
	if ((addr & 0xFF000000) == 0x02000000)
	{
		*(uint32_t*)&arm9_ram[addr & 0x3FFFFF] = data;
		return;
	}

    switch (addr)
    {
    case 0x040001A0: // Ignore Gamecard ROM and SPI control
		return;
    case 0x040001A4: // Ignore Gamecard bus ROMCTRL
		Cartridge::WriteROMCTRL(data);
        return;
	case 0x04000208:
		ime_arm9 = data & 1;
		return;
	case 0x04000210:
		ie_arm9 = data;
		return;
	case 0x04000214:
		if_arm9 &= ~data;
		return;
    }

    printf("[emu/Bus]: Write32 0x%08x to unknown address 0x%08x\n", data, addr);
    exit(1);
}

void Bus::Write16(uint32_t addr, uint16_t data)
{
	if ((addr & 0xFF000000) == 0x02000000)
	{
		printf("Writing to RAM\n");
		*(uint16_t*)&arm9_ram[addr & 0x3FFFFF] = data;
		return;
	}
	
	switch (addr)
	{
	case 0x04000180:
		arm7_ipcsync &= 0xFFF0;
		arm7_ipcsync |= ((data & 0x0F00) >> 8);
		arm9_ipcsync &= 0xB0FF;
		arm9_ipcsync |= (data & 0x4F00);
		printf("Sending value 0x%x to ARM7\n", (data & 0x0F00) >> 8);
		return;
	case 0x04000204: // Ignore EXMEMCNT
		return;
	case 0x04000304: // Ignore POWCNT1
		return;
	}

    printf("[emu/Bus]: Write16 0x%04x to unknown address 0x%08x\n", data, addr);
    exit(1);
}

void Bus::Write8(uint32_t addr, uint8_t data)
{
	switch (addr)
	{
	case 0x04000208:
		ime_arm9 = data & 1;
		return;
	case 0x04000247:
	{
		wramcnt = data & 3;
		return;
	}
	}

    printf("[emu/Bus]: Write8 0x%02x to unknown address 0x%08x\n", data, addr);
    exit(1);
}

uint32_t Bus::Read32(uint32_t addr)
{
    if (addr >= 0xFFFF0000 && addr < 0xFFFF0000 + arm9_bios_size)
        return *(uint32_t*)&arm9_bios[addr - 0xFFFF0000];
	if (addr >= dtcm_start && addr < dtcm_start + 0x4000)
		return *(uint32_t*)&dtcm[addr - dtcm_start];
	if ((addr & 0xFF000000) == 0x02000000)
	{
		printf("Reading from addr 0x%08x\n", addr);
		return *(uint32_t*)&arm9_ram[addr & 0x3FFFFF];
	}

    printf("[emu/Bus]: Read32 from unknown address 0x%08x\n", addr);
   	exit(1);
}

uint16_t Bus::Read16(uint32_t addr)
{
	if (addr >= 0xFFFF0000 && addr < 0xFFFF0000 + arm9_bios_size)
        return *(uint16_t*)&arm9_bios[addr - 0xFFFF0000];
	if ((addr & 0xFF000000) == 0x02000000)
		return *(uint16_t*)&arm9_ram[addr & 0x3FFFFF];
	
	switch (addr)
	{
	case 0x04000180:
		return arm9_ipcsync;
	}

    printf("[emu/Bus]: Read16 from unknown address 0x%08x\n", addr);
    exit(1);
}

uint8_t Bus::Read8(uint32_t addr)
{

    switch (addr)
    {
    case 0x04000300:
        return postflg_arm9;
    }

    printf("[emu/Bus]: Read8 from unknown address 0x%08x\n", addr);
    exit(1);
}

void Bus::Write8_ARM7(uint32_t addr, uint8_t data)
{
	if ((addr & 0xFFF00000) >= 0x03800000 && (addr & 0xFFF00000) < 0x04000000)
	{
		arm7_wram[addr & 0xFFFF] = data;
		return;
	}

	switch (addr)
	{
	case 0x04000208:
		ime_arm7 = data;
		return;
	case 0x040001A1:
		return;
	case 0x04000138:
		RTC::Write(data, true);
		return;
	case 0x040001A8 ... 0x040001AF:
		Cartridge::SendCommandByte(data, addr - 0x040001A8);
		return;
	case 0x040001c2:
		Firmware::WriteSPIData(data);
		return;
	}

	printf("[emu/ARM7]: Write8 0x%02x to unknown addr 0x%08x\n", data, addr);
	exit(1);
}

void Bus::Write16_ARM7(uint32_t addr, uint16_t data)
{
	if ((addr & 0xFF000000) == 0x02000000)
	{
		*(uint16_t*)&arm7_ram[addr & 0x3FFFFF] = data;
		return;
	}
	else if (addr >= 0x03000000 && addr < 0x03800000)
	{
		switch (wramcnt)
		{
		case 3:
			*(uint16_t*)&shared_wram[addr & 0x7fff] = data;
			return;
		default:
			printf("Unknown wramcnt configuration %d\n", wramcnt);
			exit(1);
		}
	}

	switch (addr)
	{
	case 0x04000180:
		arm9_ipcsync &= 0xFFF0;
		arm9_ipcsync |= ((data & 0x0F00) >> 8);
		arm7_ipcsync &= 0xB0FF;
		arm7_ipcsync |= (data & 0x4F00);
		if ((data & 0x2000) && (arm9_ipcsync & 0x4000))
		{
			printf("IPCSYNC IRQ here!\n");
			exit(1);
		}
		return;
	case 0x040001c0:
		Firmware::WriteSPICNT(data);
		return;
	case 0x040001c2:
		Firmware::WriteSPIData(data);
		return;
	case 0x04000134:
	case 0x04000128:
		return;
	case 0x04000100:
	case 0x04000102:
	case 0x04000104:
	case 0x04000106:
	case 0x04000108:
	case 0x0400010A:
	case 0x0400010C:
	case 0x0400010E:
		return;
	}

	printf("[emu/ARM7]: Write16 0x%04x to unknown addr 0x%08x\n", data, addr);
	exit(1);
}

void Bus::Write32_ARM7(uint32_t addr, uint32_t data)
{
	if (addr >= 0x03800000 && addr < 0x04000000)
	{
		*(uint32_t*)&arm7_wram[addr & 0xFFFF] = data;
		return;
	}
	else if (addr >= 0x03000000 && addr < 0x03800000)
	{
		switch (wramcnt)
		{
		case 3:
			*(uint32_t*)&shared_wram[addr & 0x7fff] = data;
			return;
		default:
			printf("Unknown WRAM configuration %d\n", wramcnt);
			exit(1);
		}
	}
	if ((addr & 0xFF000000) == 0x02000000)
	{
		*(uint32_t*)&arm7_ram[addr & 0x3FFFFF] = data;
		return;
	}

	switch (addr)
	{
	case 0x04000208:
		ime_arm7 = data & 1;
		return;
	case 0x04000210:
		ie_arm7 = data;
		return;
	case 0x04000214:
		if_arm7 &= ~data;
		return;
	case 0x04000004:
		GPU::WriteDISPCNT(data);
		return;
	case 0x040001A4:
		Cartridge::WriteROMCTRL(data);
		return;
	case 0x04000100 ... 0x0400010C:
		return;
	case 0x04000120:
		return;
	}

	printf("[emu/ARM7]: Write32 0x%08x to unknown addr 0x%08x\n", data, addr);
	exit(1);
}

uint32_t Bus::Read32_ARM7(uint32_t addr)
{
	if (addr < arm7_bios_size)
		return *(uint32_t*)&arm7_bios[addr];
	if (addr >= 0x03800000 && addr < 0x04000000)
		return *(uint32_t*)&arm7_wram[addr & 0xFFFF];
	if ((addr & 0xFF000000) == 0x02000000)
		return *(uint32_t*)&arm7_ram[addr & 0x3FFFFF];
	if (addr >= 0x03000000 && addr < 0x03800000)
	{
		switch (wramcnt)
		{
		case 3:
			return *(uint32_t*)&shared_wram[addr & 0x7fff];
		default:
			printf("Unknown wramcnt configuration %d\n", wramcnt);
			exit(1);
		}
	}

	switch (addr)
	{
    case 0x040001A4:
		return Cartridge::ReadROMCTRL();
	case 0x04100010:
		return Cartridge::ReadDataOut();
	case 0x040001c0:
		return 0;
	case 0x04000120:
		return 0;
	case 0x04000210:
		return ie_arm7;
	case 0x04000208:
		return ime_arm7;
	}
	
	printf("[emu/ARM7]: Read32 from unknown addr 0x%08x\n", addr);
	exit(1);
}

uint16_t Bus::Read16_ARM7(uint32_t addr)
{
	if (addr < arm7_bios_size)
		return *(uint16_t*)&arm7_bios[addr];
	if (addr >= 0x03000000 && addr < 0x03800000)
	{
		switch (wramcnt)
		{
		case 3:
			return *(uint16_t*)&shared_wram[addr & 0x7fff];
		default:
			printf("Unknown wramcnt configuration %d\n", wramcnt);
			exit(1);
		}
	}
	if ((addr & 0xFF000000) == 0x02000000)
		return *(uint32_t*)&arm7_ram[addr & 0x3FFFFF];
	if (addr >= 0x03800000 && addr < 0x04000000)
		return *(uint16_t*)&arm7_wram[addr & 0xFFFF];
	
	
	switch (addr)
	{
	case 0x04000180:
		return arm7_ipcsync;
	case 0x04000128:
		return 0;
	}
	
	printf("[emu/ARM7]: Read16 from unknown addr 0x%08x\n", addr);
	exit(1);
}

uint8_t Bus::Read8_ARM7(uint32_t addr)
{
	if ((addr & 0xFFF00000) >= 0x03800000 && (addr & 0xFFF00000) < 0x04000000)
		return arm7_wram[addr & 0xFFFF];
	if (addr >= 0x03000000 && addr < 0x03800000)
	{
		switch (wramcnt)
		{
		case 3:
			return shared_wram[addr & 0x7fff];
		default:
			printf("Unknown wramcnt configuration %d\n", wramcnt);
			exit(1);
		}
	}
	
	switch (addr)
    {
    case 0x04000300:
        return postflg_arm7;
	case 0x04100010 ... 0x04100013:
		return Cartridge::ReadDataOut(addr - 0x04100010);
	case 0x40001C2:
		return Firmware::ReadSPIData();
    }

    printf("[emu/ARM7]: Read8 from unknown address 0x%08x\n", addr);
    exit(1);
}

void Bus::RemapDTCM(uint32_t addr)
{
	dtcm_start = addr;
}

void Bus::Dump()
{
	std::ofstream out("ram.dump");

	for (int i = 0; i < 4*1024*1024; i++)
	{
		out << arm9_ram[i];
	}

	out.close();

	out.open("arm7_wram.dump");

	for (int i = 0; i < 0x10000; i++)
	{
		out << arm7_wram[i];
	}

	out.close();

	out.open("arm7_ram.dump");

	for (int i = 0; i < 4*1024*1024; i++)
	{
		out << arm7_ram[i];
	}

	out.close();

	out.open("shared_wram.dump");

	for (int i = 0; i < 32*1024; i++)
	{
		out << shared_wram[i];
	}

	out.close();
}
