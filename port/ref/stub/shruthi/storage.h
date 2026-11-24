// Host stub: the real storage.h drives AVR EEPROM/SPI. Only SysExDump is referenced by part.cc.
#pragma once
#include "shruthi/patch.h"
namespace shruthi { struct Storage { static void SysExDump(Patch*) {} }; }
