#include <src/core/bus.h>
#include <src/core/arm9/arm9.h>

#include <csignal>

void signal(int)
{
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

    std::signal(SIGABRT, signal);
    std::signal(SIGINT, signal);
    std::atexit(ARM9::Dump);

    while (1)
        ARM9::Clock();

    return 0;
}