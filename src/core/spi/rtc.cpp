#include "rtc.h"
#include <cassert>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Thanks to PSI-Rockin's CorgiDS code for allowing me to understand RTC

uint16_t io_reg = 0;
uint8_t internal_output[7];
int command;
int input;
int input_bit_num;
int input_index;
int output_bit_num;
int output_index;
uint8_t stat1_reg = 0;

uint8_t byte_to_BCD(uint8_t byte)
{
    return (byte / 10 * 16) + (byte % 10);
}

void interpret_input()
{
	if (input_index == 0)
	{
		command = (input & 0x70) >> 4;
		if (input & 0x80)
		{
			switch (command)
			{
			case 0:
				internal_output[0] = stat1_reg;
				break;
			case 2:
			{
				time_t t = time(0);
				struct tm* now = localtime(&t);
				internal_output[0] = byte_to_BCD(now->tm_year - 100);
				internal_output[1] = byte_to_BCD(now->tm_mon + 1);
				internal_output[2] = byte_to_BCD(now->tm_mday);
				internal_output[3] = byte_to_BCD(now->tm_wday);
				if (stat1_reg & 0x2)
				{
					internal_output[4] = byte_to_BCD(now->tm_hour);
				}
				else
				{
					internal_output[4] = byte_to_BCD(now->tm_hour % 12);
				}
				internal_output[4] |= (now->tm_hour >= 12) << 6;
				internal_output[5] = byte_to_BCD(now->tm_min);
				internal_output[6] = byte_to_BCD(now->tm_sec);
				break;
			}
			default:
				printf("[emu/RTC]: Unknown command %d\n", command);
				exit(1);
			}
		}
	}
	else
	{
		assert(0);
	}

	input_index++;
}

void RTC::Write(uint16_t value, bool is_8bit)
{
	if (is_8bit)
		value |= io_reg & 0xFF00;

	int data = value & 0x1;
	bool clock_out = value & 0x2;
	bool select_out = value & 0x4;
	bool is_writing = value & 0x10;

	if (select_out)
	{
		if (!(io_reg & 0x4))
		{
			input = 0;
			input_bit_num = 0;
			input_index = 0;
			output_bit_num = 0;
			output_index = 0;
		}
		else
		{
			if (!clock_out)
			{
				if (is_writing)
				{
					input |= data << input_bit_num;
					input_bit_num++;

					if (input_bit_num == 8)
					{
						input_bit_num = 0;
						interpret_input();
						input = 0;
					}
				}
				else
				{
					assert(0);
				}
			}
			else
			{
				if (internal_output[output_index] & (1 << output_bit_num))
					io_reg |= 0x1;
				else
					io_reg &= ~0x1;
				output_bit_num++;

				if (output_bit_num == 8)
				{
					output_bit_num = 0;
					if (output_index < 7)
						output_index++;
				}
			}
		}
	}
	
	if (is_writing)
		io_reg = value;
	else
		io_reg = (io_reg & 1) | (value & 0xFE);
}