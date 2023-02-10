#include <src/core/bus.h>
#include <src/core/arm9/arm9.h>
#include <src/core/arm7/arm7.h>
#include <src/core/spi/firmware.h>
#include <src/core/spi/cart.h>
#include <src/core/gpu/gpu.h>

#include <csignal>

void signal(int)
{
    exit(1);
}

int main(int argc, char** argv)
{
	#if 1
    if (argc < 4)
    {
        printf("Usage: %s <arm9 bios> <arm7 bios> <firmware image>\n", argv[0]);
        return 0;
    }

    Bus::AddARMBios(argv[1], true);
    Bus::AddARMBios(argv[2], false);

	Firmware::LoadFirmware(argv[3]);
	#else

	if (argc < 2)
	{
		printf("Usage: %s <test rom>\n", argv[0]);
		return 0;
	}

	Bus::LoadNDS(argv[1]);
	#endif

    ARM9::Reset();
	ARM7::Reset();

    std::signal(SIGABRT, signal);
    std::signal(SIGINT, signal);
    std::atexit(ARM9::Dump);
    std::atexit(ARM7::Dump);

    while (1)
	{
		for (int i = 0; i < 2048; i++)
		{
			for (int i = 0; i < 8; i++)
			{
				ARM9::Clock();
				ARM9::Clock();
				ARM7::Clock();
			}
			Cartridge::Run(8);
		}
		GPU::Draw();
	}

    return 0;
}