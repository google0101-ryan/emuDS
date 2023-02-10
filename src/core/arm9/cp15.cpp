#include "cp15.h"
#include <src/core/arm9/arm9.h>

#include <cstdio>
#include <cstdlib>

namespace CP15
{

uint32_t exception_vectors = 0x00000000;
uint32_t control = 0;
uint32_t dtcm = 0;

void WriteCP15(uint32_t cn, uint32_t cm, uint32_t cp, uint32_t data)
{
	if (cn == 1 && cm == 0 && cp == 0)
	{
		exception_vectors = (data >> 13) & 1 ? 0xFFFF0000 : 0x00000000;
		control = data;
		return;
	}
	else if (cn == 7 && cm == 5 && cp == 0)
	{
		printf("Invalidate icache\n");
		return;
	}
	else if (cn == 7 && cm == 6 && cp == 0)
	{
		printf("Invalidate dcache\n");
		return;
	}
	else if (cn == 7 && cm == 10 && cp == 4)
	{
		printf("Drain write buffer\n");
		return;
	}
	else if (cn == 9 && cm == 1 && cp == 0)
	{
		uint32_t dtcm_base = (data >> 12) & 0xFFFFF;
		dtcm_base <<= 12;

		dtcm = data;

		printf("Remapping Data TCM to 0x%08x\n", dtcm_base);

		Bus::RemapDTCM(dtcm_base);
		return;
	}

	printf("Write to unknown cp15 register 0,C%d,C%d,%d\n", cn, cm, cp);
	ARM9::Dump();
	exit(1);
}

uint32_t ReadCP15(uint32_t cn, uint32_t cm, uint32_t cp)
{
	if (cn == 1 && cm == 0 && cp == 0)
	{
		return control;
	}
	else if (cn == 9 && cm == 1 && cp == 0)
	{
		return dtcm;
	}
	else if (cn == 2 && cm == 0 && cp == 0)
	{
		return 0;
	}
	else if (cn == 2 && cm == 0 && cp == 1)
	{
		return 0;
	}
	else if (cn == 3 && cm == 0 && cp == 0)
	{
		return 0;
	}
	else if (cn == 3 && cm == 0 && cp == 1)
	{
		return 0;
	}
	else if (cn == 5 && cm == 0 && cp == 0)
	{
		return 0;
	}
	else if (cn == 5 && cm == 0 && cp == 1)
	{
		return 0;
	}
	else if (cn == 5 && cm == 0 && cp == 2)
	{
		return 0;
	}
	else if (cn == 5 && cm == 0 && cp == 3)
	{
		return 0;
	}
	else if (cn == 6)
	{
		return 0;
	}
	else if (cn == 9 && cm == 1 && cp == 1)
	{
		return 0;
	}

	printf("Read from unknown cp15 register 0,C%d,C%d,%d\n", cn, cm, cp);
	ARM9::Dump();
	exit(1);
}

}