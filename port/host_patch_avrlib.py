#!/usr/bin/env python3
"""Host-only edits to the COPY of avrlib used by the reference build (see ref/README.md)."""
import sys
d = sys.argv[1]
p = d + '/op.h'; s = open(p).read()
s = s.replace("#define USE_OPTIMIZED_OP\n", "/* host: AVR assembly disabled, portable C fallbacks used */\n", 1)
bad = "uint32_t difference = av - bv;\n  result.integral = sum >> 8;\n  result.fractional = sum & 0xff;"
assert bad in s
s = s.replace(bad, "uint32_t difference = av - bv;\n  result.integral = difference >> 8;\n  result.fractional = difference & 0xff;")
open(p, 'w').write(s)
p = d + '/resources_manager.h'; s = open(p).read()
a = "char* address = (char*)(pgm_read_word(&(Tables::string_table()[resource])));"
b = "uint16_t* address = (uint16_t*)(\n    pgm_read_word(&(Tables::lookup_table_table()[resource])));"
assert a in s and b in s
s = s.replace(a, "char* address = (char*)(Tables::string_table()[resource]);")
s = s.replace(b, "uint16_t* address = (uint16_t*)(Tables::lookup_table_table()[resource]);")
open(p, 'w').write(s)
