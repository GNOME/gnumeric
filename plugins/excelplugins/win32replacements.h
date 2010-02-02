#ifndef GNM_XLL_WIN32REPALCEMENTS_H
#define GNM_XLL_WIN32REPALCEMENTS_H

#ifndef _WORD_DEFINED
typedef unsigned short WORD;
#define _WORD_DEFINED
#endif

#ifndef _DWORD_DEFINED
typedef unsigned long DWORD;
#define _DWORD_DEFINED
#endif

#ifndef _BOOL_DEFINED
typedef int BOOL;
#define _BOOL_DEFINED
#endif

#ifndef _BYTE_DEFINED
typedef unsigned char BYTE;
#define _BYTE_DEFINED
#endif

#ifndef _LPSTR_DEFINED
typedef char * LPSTR;
#define _LPSTR_DEFINED
#endif

#ifndef FAR
#define FAR
#endif

#ifndef far
#define far
#endif

#ifndef _HANDLE_DEFINED
typedef void * HANDLE;
#define _HANDLE_DEFINED
#endif

#ifndef _cdecl
#define _cdecl
#endif

#ifndef pascal
#define pascal
#endif

#if ( defined( WIN32 ) || defined( WIN64 ) ) && ! defined( WINAPI )
#define WINAPI __attribute__((stdcall))
#else
#define WINAPI /* By default, C/C++ calling conventions are used throughout. This can be changed if required. */
#endif

#ifndef APIENTRY
#define APIENTRY WINAPI
#endif

#ifndef LPVOID
#define LPVOID void*
#endif

#ifndef TRUE
#define TRUE                1
#endif

#ifndef FALSE
#define FALSE               0
#endif

#endif
