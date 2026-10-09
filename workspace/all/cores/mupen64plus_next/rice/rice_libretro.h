/* Force-included into every Rice source when it is built into the
 * mupen64plus-next libretro core (build-rice.sh).
 *
 * - The plugin entry points get a "rice" prefix, like GLideN64's "gln64",
 *   so plugin.c can list Rice next to the other video plugins.
 * - GL calls go through glsm (the core's GL state wrapper) so Rice shares
 *   GLideN64's state handling and "framebuffer 0" is the frontend's FBO. */
#ifndef RICE_LIBRETRO_H
#define RICE_LIBRETRO_H

#define PluginStartup ricePluginStartup
#define PluginShutdown ricePluginShutdown
#define PluginGetVersion ricePluginGetVersion
#define ChangeWindow riceChangeWindow
#define InitiateGFX riceInitiateGFX
#define MoveScreen riceMoveScreen
#define ProcessDList riceProcessDList
#define ProcessRDPList riceProcessRDPList
#define RomClosed riceRomClosed
#define RomOpen riceRomOpen
#define ShowCFB riceShowCFB
#define UpdateScreen riceUpdateScreen
#define ViStatusChanged riceViStatusChanged
#define ViWidthChanged riceViWidthChanged
#define ReadScreen2 riceReadScreen2
#define SetRenderingCallback riceSetRenderingCallback
#define ResizeVideoOutput riceResizeVideoOutput
#define FBRead riceFBRead
#define FBWrite riceFBWrite
#define FBGetFrameBufferInfo riceFBGetFrameBufferInfo

#endif
