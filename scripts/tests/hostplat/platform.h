// Host-test platform shim. The host unit tests under scripts/tests compile
// shared device sources with the host compiler against the tg5040 platform
// header. Tests that touch the card pass -DHOSTTEST_SDCARD="<scratch dir>" so
// every path macro derived from SDCARD_PATH (SHARED_USERDATA_PATH, ...) lands
// in their scratch directory instead of /mnt/SDCARD.
#ifndef HOSTTEST_PLATFORM_H
#define HOSTTEST_PLATFORM_H
#include "../../../workspace/tg5040/platform/platform.h"
#ifdef HOSTTEST_SDCARD
#undef SDCARD_PATH
#define SDCARD_PATH HOSTTEST_SDCARD
#endif
#endif
