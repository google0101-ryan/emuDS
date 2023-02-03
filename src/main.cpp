#include <src/core/bus.h>
#include <src/core/arm9/arm9.h>

#include <csignal>

void signal(int)
{
    ARM9::Dump();
    exit(1);
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("Usage: %s <arm9 bios> <arm7 bios>\n", argv[0]);
        return 0;
    }

    Bus::AddARMBios(argv[1], true);
    Bus::AddARMBios(argv[2], false);

    ARM9::Reset();

    while (1)
        ARM9::Clock();

    std::signal(SIGABRT, signal);
    std::atexit(ARM9::Dump);

    return 0;
}