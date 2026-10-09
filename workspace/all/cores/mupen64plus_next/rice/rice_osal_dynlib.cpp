/* Rice looks up the core's config and video-extension functions by name in
 * PluginStartup. Inside the libretro core there is no library handle to
 * search: the core hands out the addresses itself (rice_glue.c). */
#include "osal_dynamiclib.h"

extern "C" void* rice_core_proc(const char* name);

void* osal_dynlib_getproc(m64p_dynlib_handle LibHandle, const char* pccProcedureName)
{
    (void)LibHandle;
    return rice_core_proc(pccProcedureName);
}
