#ifndef __SCALER_H__
#define __SCALER_H__
#include <stdint.h>

//
//	C integer scaler for the software-blit path.
//	args/	src :	src offset		address of top left corner
//		dst :	dst offset		address	of top left corner
//		sw  :	src width		pixels
//		sh  :	src height		pixels
//		sp  :	src pitch (stride)	bytes	if 0, (src width * 2) is used
//		dw  :	dst width		pixels
//		dh  :	dst height		pixels
//		dp  :	dst pitch (stride)	bytes	if 0, (src width * 2) is used
//		ymul:	vertical repeat		rows per source row
//
//	NOTE: the former NEON/C scaleNxM family and generic scaler_[nc](16|32)
//	dispatchers were unused (PLAT_getScaler always returns scale1x1_c16, the
//	GPU/SDL_Render path does all real scaling) and have been removed.
//

typedef void (*scaler_t)(void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dw, uint32_t dh, uint32_t dp);

void scale1x_c16(void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dw, uint32_t dh, uint32_t dp, uint32_t ymul);
void scale1x1_c16(void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dw, uint32_t dh, uint32_t dp);

#endif
