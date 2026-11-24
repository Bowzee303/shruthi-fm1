#pragma once
#include <stdint.h>
#define PROGMEM
typedef const uint8_t prog_uint8_t; typedef const int8_t prog_int8_t;
typedef const uint16_t prog_uint16_t; typedef const int16_t prog_int16_t;
typedef const uint32_t prog_uint32_t; typedef const char prog_char;
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define pgm_read_word(p) (*(const uint16_t*)(p))
#include <string.h>
#define memcpy_P memcpy
#define strncpy_P strncpy
#define pgm_read_dword(p) (*(const uint32_t*)(p))
