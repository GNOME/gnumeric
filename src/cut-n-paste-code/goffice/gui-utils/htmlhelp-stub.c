#include "htmlhelp-stub.h"

HWND HtmlHelp_ (HWND    hwndCaller,
		 LPCSTR  pszFile,
		 UINT    uCommand,
		 DWORD   dwData)
{
	return HtmlHelp (hwndCaller, pszFile, uCommand, dwData);
}
