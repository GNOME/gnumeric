/**
 * excel.h: Excel support interface to gnumeric
 *
 * Authors:
 *    Michael Meeks (michael@ximian.com)
 *    Jody Goldberg (jody@gnome.org)
 *
 * (C) 1998-2004 Michael Meeks, Jody Goldberg
 **/
#ifndef GNM_MS_EXCEL_H
#define GNM_MS_EXCEL_H

#include "gnumeric.h"
#include <gsf/gsf.h>

void excel_read_workbook (IOContext *context, WorkbookView *new_wb,
			  GsfInput *input, gboolean *is_double_stream_file);

typedef struct _ExcelWriteState	 ExcelWriteState;
void		 excel_write_state_free (ExcelWriteState *ewb);
ExcelWriteState *excel_write_state_new  (IOContext *context, WorkbookView const *wbv,
					 gboolean biff7, gboolean biff8);

void excel_write_v7 (ExcelWriteState *ewb, GsfOutfile *output);
void excel_write_v8 (ExcelWriteState *ewb, GsfOutfile *output);

typedef struct {
	guint8 r, g, b;
} ExcelPaletteEntry;
extern ExcelPaletteEntry const excel_default_palette_v8 [];
extern ExcelPaletteEntry const excel_default_palette_v7 [];

#define EXCEL_DEF_PAL_LEN   56

extern  char const *excel_builtin_formats[];
#define EXCEL_BUILTIN_FORMAT_LEN 0x32

#define ROW_BLOCK_MAX_LEN 32

#define	CODENAME_KEY	"XL_CODENAME_utf8"

#endif /* GNM_MS_EXCEL_H */
