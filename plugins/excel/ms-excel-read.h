/**
 * ms-excel.h: MS Excel support for Gnumeric
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#ifndef GNM_MS_EXCEL_READ_H
#define GNM_MS_EXCEL_READ_H

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-container.h"
#include <expr.h>
#include <mstyle.h>
#include <goffice-data.h>
#include <pango/pango-attributes.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

typedef struct {
	Sheet	  *first, *last;
	unsigned   supbook;
} ExcelExternSheetV8;

#define XL_EXTERNSHEET_MAGIC_SELFREF ((Sheet *)1)
#define XL_EXTERNSHEET_MAGIC_DELETED ((Sheet *)2)

typedef enum {
	EXCEL_SUP_BOOK_STD,
	EXCEL_SUP_BOOK_SELFREF,
	EXCEL_SUP_BOOK_PLUGIN
} ExcelSupBookType;
typedef struct {
	ExcelSupBookType type;
	Workbook  *wb;
	GPtrArray *externname;
} ExcelSupBook;

typedef struct {
	MSContainer container;

	Sheet		*sheet;
	GHashTable	*shared_formulae, *tables;

	gboolean	 freeze_panes;
	unsigned	 active_pane;
	GnmFilter	*filter;
	int		 biff2_prev_xf_index;
} ExcelReadSheet;

typedef struct {
	GnmCellPos key;
	guint8 *data;
	guint32 data_len, array_data_len;
	gboolean is_array;
	gboolean being_parsed;
} XLSharedFormula;

typedef struct {
	GnmRange   table;
	GnmCellPos c_in, r_in;
} XLDataTable;

/* Use the upper left corner as the key to a collection of shared formulas */
XLSharedFormula *excel_sheet_shared_formula (ExcelReadSheet const *sheet,
					     GnmCellPos const    *key);
XLDataTable	*excel_sheet_data_table	    (ExcelReadSheet const *esheet,
					     GnmCellPos const    *key);

typedef struct {
	int *red;
	int *green;
	int *blue;
	int length;
	GnmColor **gnm_colors;
} ExcelPalette;

typedef struct {
	unsigned index;
	int height;         /* in 1/20ths of a point   */
	int italic;         /* boolean                 */
	int struck_out;     /* boolean : strikethrough */
	int color_idx;
	int boldness;       /* 100->1000 dec, normal = 0x190, bold = 0x2bc */
	guint16 codepage;
	GOFontScript script;
	MsBiffFontUnderline underline;
	char *fontname;
	PangoAttrList *attrs;
	GOFont const  *go_font;
} ExcelFont;

typedef struct {
	unsigned idx;
	char *name;
} BiffFormatData;

typedef struct {
	GOString	*content;
	GOFormat	*markup;
} ExcelStringEntry;

struct _GnmXLImporter {
	MSContainer	  container;
	GOIOContext	 *context;
	WorkbookView	 *wbv;
	Workbook         *wb;
	MsBiffVersion	  ver;

	GPtrArray	 *excel_sheets;
	GHashTable	 *boundsheet_data_by_stream;
	GPtrArray	 *boundsheet_sheet_by_index;
	GPtrArray	 *names;
	unsigned	  num_name_records; /* names->len has fwd decls */
	GPtrArray	 *XF_cell_records;
	GHashTable	 *font_data;
	GHashTable	 *format_table; /* leave as a hash */
	struct {
		GnmSheetSlicer	  *slicer;
		GODataSlicerField *slicer_field;

		GPtrArray	  *cache_by_index;
		unsigned int	  field_count, record_count;

		unsigned int	  ivd_index; /* 0 = row, 1 = col, > 1 == err */
	} pivot;
	struct {
		GArray	 *supbook;
		GArray	 *externsheet;
	} v8; /* biff8 does this in the workbook */
	ExcelPalette	 *palette;
	unsigned	  sst_len;
	ExcelStringEntry *sst;

	GnmExprSharer    *expr_sharer;
	GIConv            str_iconv;
	int               codepage_override;
};

GnmValue *xls_value_new_err (GnmEvalPos const *pos, guint8 const err);
void	  xls_read_range32  (GnmRange *r, guint8 const *data);
void	  xls_read_range16  (GnmRange *r, guint8 const *data);
void	  xls_read_range8   (GnmRange *r, guint8 const *data);

Sheet		*excel_externsheet_v7	 (MSContainer const *container, gint16 i);
ExcelExternSheetV8 const *excel_externsheet_v8 (GnmXLImporter const *wb, guint16 i);

void		excel_read_EXTERNSHEET_v7 (BiffQuery const *q, MSContainer *container);
MsBiffBofData *ms_biff_bof_data_new     (BiffQuery * q);
void	       ms_biff_bof_data_destroy (MsBiffBofData * data);

char *excel_get_chars (GnmXLImporter const *imp,
		       guint8 const *ptr, size_t length,
		       gboolean use_utf16, guint16 const *codepage);
char * excel_get_text (GnmXLImporter const *imp,
		       guint8 const *pos, guint32 length,
		       guint32 *byte_length, guint16 const *codepage, guint32 maxlen);
char *excel_biff_text_1 (GnmXLImporter const *imp, BiffQuery const *q, guint32 ofs);
char *excel_biff_text_2 (GnmXLImporter const *imp, BiffQuery const *q, guint32 ofs);

GnmColor	*excel_palette_get (GnmXLImporter *imp, gint idx);
ExcelFont const *excel_font_get    (GnmXLImporter const *imp, unsigned idx);
GOFont const	*excel_font_get_gofont (ExcelFont const *font);

GdkPixbuf *excel_read_IMDATA (BiffQuery *q, gboolean keep_image);
void	   excel_read_SCL    (BiffQuery *q, Sheet *esheet);

/* A utility routine to handle unexpected BIFF records */
void excel_unexpected_biff (BiffQuery *q, char const *state, int debug_level);

void xls_read_SXStreamID (GnmXLImporter *imp, BiffQuery *q,
			  GsfInfile *parent);
void xls_read_SXVIEW	 (BiffQuery *q, ExcelReadSheet *esheet);
void xls_read_SXVD	 (BiffQuery *q, ExcelReadSheet *esheet);
void xls_read_SXIVD	 (BiffQuery *q, ExcelReadSheet *esheet);

void excel_read_cleanup (void);
void excel_read_init (void);

#endif /* GNM_MS_EXCEL_READ_H */
