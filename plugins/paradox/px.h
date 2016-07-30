#ifndef GNUMERIC_PLUGIN_PARADOX_PARADOX_H
#define GNUMERIC_PLUGIN_PARADOX_PARADOX_H

#include <gnumeric.h>
#include <pxversion.h>
/* if we are there, pxlib has been built with gsf support, so we need
 * to define HAVE_GSF at least for pxlib-0.6.7 see #769319 and
 * https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=832827 */
#define HAVE_GSF 1
#include <paradox-gsf.h>

#define PX_MEMORY_DEBUGGING
#ifdef PX_MEMORY_DEBUGGING
#include <paradox-mp.h>
#endif

#endif
