#ifndef GNM_PLUGIN_PARADOX_PARADOX_H_
#define GNM_PLUGIN_PARADOX_PARADOX_H_

#include <gnumeric.h>
#include <pxversion.h>
/* if we are there, pxlib has been built with gsf support, so we need
 * to define HAVE_GSF at least for pxlib-0.6.7 see #769319 and
 * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=832827 */
#define HAVE_GSF 1
#include <paradox-gsf.h>

/* PX_MEMORY_DEBUGGING makes pxlib track every allocation in a fixed table of
 * 10000 slots, which overflows on big imports (the plugin frees values with
 * g_free, so slots are never released).  Define it only for debugging. */
/* #define PX_MEMORY_DEBUGGING */
#ifdef PX_MEMORY_DEBUGGING
#include <paradox-mp.h>
#endif

#endif
