#include "gpu.h"

#include <cstdio>
#include <cstdlib>
#include <SDL2/SDL.h>
#include <src/core/bus.h>

uint8_t* VRAMA;
uint8_t* VRAMB;

void GPU::Dump()
{
	std::ofstream vram_a("vram_a.dump");
	std::ofstream vram_b("vram_b.dump");

	for (int i = 0; i < 128*1024; i++)
	{
		vram_a << VRAMA[i];
		vram_b << VRAMB[i];
	}

	vram_a.close();
	vram_b.close();
}

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* tex = nullptr;

void GPU::InitMem()
{
	VRAMA = new uint8_t[128*1024];
	VRAMB = new uint8_t[128*1024];

	window = SDL_CreateWindow("NDS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 256, 192*2, SDL_WINDOW_SHOWN);
	renderer = SDL_CreateRenderer(window, -1, 0);
	tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR1555, SDL_TEXTUREACCESS_STATIC, 256, 192);	
}

uint8_t bitmap_bank = 0;

void GPU::Draw()
{
	uint8_t* bank;

	Bus::ResetKeys();

	switch (bitmap_bank)
	{
	case 0:
		bank = VRAMA;
		break;
	default:
		printf("Unknown bank %d\n", bitmap_bank);
		exit(1);
	}

	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = 256;
	rect.h = 192;

 	SDL_UpdateTexture(tex, NULL, (void*)bank, 256*2);
	
	SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
	SDL_RenderClear(renderer);
	
	SDL_RenderCopy(renderer, tex, NULL, &rect);
	
	SDL_RenderPresent(renderer);

	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_KEYDOWN:
		{
			if (event.key.keysym.sym == SDLK_DOWN)
			{
				Bus::PressKey(Bus::Keys::KEY_DOWN);
			}
			else
			{
				Bus::ReleaseKey(Bus::Keys::KEY_DOWN);
			}
			if (event.key.keysym.sym == SDLK_UP)
			{
				Bus::PressKey(Bus::Keys::KEY_UP);
			}
			else
			{
				Bus::ReleaseKey(Bus::Keys::KEY_UP);
			}
			if (event.key.keysym.sym == SDLK_RETURN)
			{
				Bus::PressKey(Bus::Keys::KEY_START);
			}
			else
			{
				Bus::ReleaseKey(Bus::Keys::KEY_START);
			}
			if (event.key.keysym.sym == SDLK_SPACE)
			{
				Bus::PressKey(Bus::Keys::KEY_SELECT);
			}
			else
			{
				Bus::ReleaseKey(Bus::Keys::KEY_SELECT);
			}
			break;
		}
		case SDL_QUIT:
			SDL_DestroyRenderer(renderer);
			SDL_DestroyWindow(window);
			exit(1);
			break;
		}
	}
}

void GPU::WriteDISPCNT(uint32_t data)
{
	bitmap_bank = (data >> 18) & 3;
	printf("Using bitmap bank %d\n", bitmap_bank);
}

struct VRAMCNT
{
	uint8_t mst;
	uint8_t offset;
	bool enabled;
} VRAMCNTA, VRAMCNTB;

void GPU::WriteVRAMCNT_A(uint32_t data)
{
	VRAMCNTA.mst = (data & 7);
	VRAMCNTA.offset = (data >> 3) & 2;
	VRAMCNTA.enabled = (data >> 7) & 1;
	if (VRAMCNTA.enabled)
		printf("Enabling VRAM bank A\n");
}

void GPU::WriteVRAMCNT_B(uint32_t data)
{
	VRAMCNTB.mst = (data & 7);
	VRAMCNTB.offset = (data >> 3) & 2;
	VRAMCNTB.enabled = (data >> 7) & 1;
	if (VRAMCNTB.enabled)
		printf("Enabling VRAM bank B\n");
}

bool dispStat = true;

// Simulate VBLANK for ARMWrestler
uint16_t GPU::ReadDIPSTAT()
{
	bool d = dispStat;
	dispStat = !dispStat;
	return d;
}

void GPU::WriteLCDC(uint32_t addr, uint16_t halfword)
{
	if ((addr >= 0x06800000 && addr < 0x06820000) && VRAMCNTA.mst == 0)
	{
		if (VRAMCNTA.enabled)
		{
			*(uint16_t*)&VRAMA[addr & 0x1FFFF] = halfword;
		}
		return;
	}
	else if ((addr >= 0x06820000 && addr < 0x06840000) && VRAMCNTA.mst == 0)
	{
		if (VRAMCNTA.enabled)
		{
			*(uint16_t*)&VRAMB[addr & 0x1FFFF] = halfword;
		}
		return;
	}
	else
	{
		printf("Writing to unknown LCDC address 0x%08x\n", addr);
		exit(1);
	}
}
