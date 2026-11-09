#pragma once

#define PSTR(value) (value)
#define pgm_read_byte(address) (*reinterpret_cast<const unsigned char *>(address))
