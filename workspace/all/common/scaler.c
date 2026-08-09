#include <stdint.h>
#include <string.h>

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
//
//	NOTE: the former NEON/C scaleNxM family, the c16to32/line/grid variants,
//	and the generic scaler_[nc](16|32) dispatchers were all unreachable
//	(PLAT_getScaler always returns scale1x1_c16; the GPU/SDL_Render path does
//	all real scaling) and have been removed.
//

void scale1x_c16(void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dw, uint32_t dh, uint32_t dp, uint32_t ymul) {
	if (!sw || !sh || !ymul)
		return;
	uint32_t swl = sw * sizeof(uint16_t);
	if (!sp) {
		sp = swl;
	}
	if (!dp) {
		dp = swl * 1;
	}
	if ((ymul == 1) && (swl == sp) && (sp == dp))
		memcpy(dst, src, sp * sh);
	else {
		if (swl > dp)
			swl = dp;
		for (; sh > 0; sh--, src = (uint8_t*)src + sp) {
			for (uint32_t i = ymul; i > 0; i--, dst = (uint8_t*)dst + dp)
				memcpy(dst, src, swl);
		}
	}
}

void scale1x1_c16(void* __restrict src, void* __restrict dst, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dw, uint32_t dh, uint32_t dp) {
	scale1x_c16(src, dst, sw, sh, sp, dw, dh, dp, 1);
}
