// Host stub: EEPROM reads return 0xff (erased), writes are ignored.
#pragma once
#include <stdint.h>
#include <string.h>
#define EEMEM
static inline void eeprom_read_block(void* dst, const void*, size_t n) { memset(dst, 0xff, n); }
static inline void eeprom_write_block(const void*, void*, size_t) {}
static inline uint8_t eeprom_read_byte(const uint8_t*) { return 0xff; }
static inline void eeprom_write_byte(uint8_t*, uint8_t) {}
static inline void eeprom_update_byte(uint8_t*, uint8_t) {}
static inline uint16_t eeprom_read_word(const uint16_t*) { return 0xffff; }
