#ifndef NX_LEGACY_SAVE_H
#define NX_LEGACY_SAVE_H
#include <stddef.h>
#include <stdint.h>
/* Standalone N64.pak saves (mupen64plus SaveFilenameFormat 1:
 * "<goodname>-<MD5[0:8]>.eep|.mpk|.sra|.fla") read into the libretro core's
 * save memory. Returns a mask of what was imported (1 eep, 2 mpk, 4 sra,
 * 8 fla). Read-only on dir. */
int nx_legacy_save_import(const char* dir, const char* md5,
						  uint8_t* eeprom, size_t eeprom_size, uint8_t* mempak, size_t mempak_size,
						  uint8_t* sram, size_t sram_size, uint8_t* flashram, size_t flashram_size);
#endif
