// ma_present.h - present thread for a USB Cable lockstep follower.
//
// While gpSP's link-cable lockstep follower has a busy link, the swap (which
// blocks on vsync; the Brick's GPU ignores the swap interval) leaves it unable
// to answer the leader's next transfer, and the leader stalls. In that state
// alone the frame's GL work and swap (PLAT_GL_Swap) run on a thread of their
// own, the latest frame wins, and the follower's emulation is paced by the
// link. In every other case nothing here runs and minarch presents as always.
#ifndef MA_PRESENT_H
#define MA_PRESENT_H

#include <stdbool.h>

#include "api.h"

// Per frame, before presenting: starts or stops the thread as the follower's
// link becomes busy or quiet. True: present with Present_submit instead of
// GFX_blitRenderer + screen_flip.
bool Present_update(void);

// Hand the thread this frame (copied; the caller may reuse its buffer).
void Present_submit(const GFX_Renderer* r);

// Stop the thread and give the GL context back to the main thread. Every
// main-thread GL or SDL-renderer user calls this first. No-op when stopped.
void Present_stop(void);

// Guards main-thread writes that PLAT_GL_Swap reads (the notification layer).
// Stopped: always true, nothing held. Running: trylock; on false skip the
// write this frame. Pair a true with Present_unlockFrameState.
bool Present_lockFrameState(void);
void Present_unlockFrameState(void);

#endif
