/*
 * xlcall32_emulation.c:  callback module required by (genuine) Excel plugins (also known as XLLs).
 *
 * Author:
 *   Peter Jaeckel (peter.jaeckel@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* For quick manual building, use something like
 *
 *     gcc -fPIC -shared -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include xlcall32_emulation.c -Wl,-soname -Wl,xlcall32.so -o .libs/xlcall32.so
 * or
 *     i586-mingw32msvc-gcc -shared -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include xlcall32_emulation.c -Wl,-soname -Wl,xlcall32.dll -o .libs/xlcall32.dll
 */

#include <gnumeric-config.h>
#include <gmodule.h>

#if defined( WIN32 ) || defined( WIN64 )
#include <windef.h>
#else
#include "win32replacements.h"
#endif

G_MODULE_EXPORT void register_actual_excel4v(void*p);

G_MODULE_EXPORT int far _cdecl Excel4(int xlfn, void* operRes, int count, ...);

G_MODULE_EXPORT int far pascal Excel4v(int xlfn, void* operRes, int count, void** opers);

G_MODULE_EXPORT int far pascal XLCallVer(void);

typedef int (*Excel4vFunc)(int xlfn, void* /* XLOper * operRes */, int /*count*/, void** /* XLOper ** opers */ );

static Excel4vFunc actual_excel4v = NULL;

G_MODULE_EXPORT void register_actual_excel4v(void*p){
        actual_excel4v = (Excel4vFunc)p;
}

G_MODULE_EXPORT int far pascal Excel4v(int xlfn, void* operRes, int count, void** opers) {
        if (NULL!=actual_excel4v)
                return actual_excel4v(xlfn,operRes,count,opers);
        return -1;
}

G_MODULE_EXPORT int far _cdecl Excel4(int xlfn, void* operRes, int count, ...) {
	void **opers=g_new(void *, MAX(1,count));
	va_list arg_list;
	int i, res;
        va_start(arg_list,count);
	for (i=0;i<count;++i)
		opers[i]=va_arg(arg_list,void*);
	va_end(arg_list);
	res = Excel4v(xlfn,operRes,count,opers);
	g_free (opers);
	return res;
}

int far pascal XLCallVer(void){
	/*
	 * From http://msdn.microsoft.com/en-us/library/bb687851.aspx
	 *
	 * "You can call this function from any XLL command or function and is thread safe.
	 *
	 *  In Excel 97 through Excel 2003, XLCallVer returns 1280 = 0x0500 hex = 5 x 256, which indicates Excel version
	 *  5. In Excel 2007, it returns 3072 = 0x0c00 hex = 12 x 256, which similarly indicates version 12."
	 *
	 */
	return 1280;
}

#ifdef WIN32
asm (".section .drectve");
asm (".ascii \"-export:Excel4v=Excel4v@16,XLCallVer=XLCallVer@0\"");
#endif
