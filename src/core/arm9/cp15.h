#pragma once

#include <cstdint>

namespace CP15
{

void WriteCP15(uint32_t cn, uint32_t cm, uint32_t cp, uint32_t data);
uint32_t ReadCP15(uint32_t cn, uint32_t cm, uint32_t cp);

}