/**
 * ms-excel-write.h: MS Excel export
 *
 * Authors:
 *    Jody Goldberg (jody@gnome.org)
 *    Michael Meeks (michael@ximian.com)
 *
 * (C) 1998-2001 Michael Meeks
 * (C) 2002-2005 Jody Goldberg
 **/
#ifndef GNM_MS_EXCEL_WRITE_H
#define GNM_MS_EXCEL_WRITE_H

#include "ms-biff.h"
#include "ms-excel-biff.h"
#include "ms-excel-util.h"
#include <style.h>
#include <goffice-data.h>

typedef struct {
	Sheet const *a, *b;
	int idx_a, idx_b;
} ExcelSheetPair;

typedef struct {
	/* Don't use GnmFont.  In the case where a font does not exist on the
	 * display system it does the wrong thing.  GnmStyle can contain an
	 * invalid font.  GnmFont gets remapped to the default
	 */
	guint32    color;
	char const *font_name;
	char       *font_name_copy; /* some times we need to keep a local copy */
	double		size_pts;
	gboolean	is_bold;
	gboolean	is_italic;
	gboolean	is_auto;
	GnmUnderline	underline;
	gboolean	strikethrough;
	unsigned	script;
} ExcelWriteFont;

typedef struct {
	ExcelWriteState	*ewb;
	Sheet		*gnum_sheet;
	unsigned	 streamPos;
	guint32		 boundsheetPos;
	gint32		 max_col, max_row;
	guint16		*col_xf;
	GPtrArray       *col_style;
	GnmStyleList	*conditions, *hlinks, *validations;
	GSList          *blips, *objects, *graphs, *comments;
	GHashTable	*commentshash;
	GHashTable      *widget_macroname;
	unsigned	 cur_obj, num_objs;
} ExcelWriteSheet;

typedef struct {
	Workbook      const *wb;
	WorkbookView  const *wb_view;

	struct {
		TwoWayTable *two_way_table;
		GnmStyle    *default_style;
		GHashTable  *value_fmt_styles;
		GHashTable  *cell_style_variant;
	} xf;
	struct {
		TwoWayTable *two_way_table;
		guint8 entry_in_use[EXCEL_DEF_PAL_LEN];
	} pal;
	struct {
		TwoWayTable *two_way_table;
	} fonts;
	struct {
		TwoWayTable *two_way_table;
	} formats;
	GHashTable *pivot_caches;
} XLExportBase;

struct _XLSExporter {
	XLExportBase	base;

	GOIOContext     *io_context;
	BiffPut	      *bp;

	GPtrArray     *esheets;
	GHashTable    *function_map;
	GHashTable    *sheet_pairs;
	GHashTable    *cell_markup;

	/* we use the ewb as a closure for things, this is useful */
	int tmp_counter;
	int supbook_idx;

	gboolean	 double_stream_file;
	GPtrArray	*externnames;
	GHashTable	*names;
	unsigned	 streamPos;

	/* no need to use a full fledged two table, we already know that the
	 * Strings are unique. */
	struct {
		GHashTable *strings;
		GPtrArray  *indicies;
	} sst;

	unsigned num_obj_groups, cur_obj_group, cur_blip;
	gboolean export_macros;

	guint32 unique_name_id;
};

#define XF_RESERVED 21
#define XF_MAGIC 0
#define FONTS_MINIMUM 5
#define FONT_SKIP 4
#define FONT_MAGIC 0
#define FORMAT_MAGIC 0
#define PALETTE_BLACK 8
#define PALETTE_WHITE 9
#define PALETTE_AUTO_PATTERN 64
#define PALETTE_AUTO_BACK 65
#define PALETTE_AUTO_FONT 0x7fff
#define FILL_NONE 0
#define FILL_SOLID 1
#define FILL_MAGIC FILL_NONE
#define BORDER_MAGIC GNM_STYLE_BORDER_NONE

typedef enum {
	STR_ONE_BYTE_LENGTH	= 0,
	STR_TWO_BYTE_LENGTH	= 1,
	STR_FOUR_BYTE_LENGTH	= 2,
	STR_NO_LENGTH		= 3,
	STR_LENGTH_MASK		= 3, /* (1 << (flag & LENGTH_MASK)) == size */

	/* biff7 will always be LEN_IN_BYTES,
	 * biff8 will respect the flag and default to length in characters */
	STR_LEN_IN_BYTES	= 1 << 2,

	/* Drop 2byte/rich/phonetic header and force the use of 2byte chars */
	STR_SUPPRESS_HEADER	= 1 << 3,

	/* include a trailing null */
	STR_TRAILING_NULL	= 1 << 4
} WriteStringFlags;

unsigned excel_write_string	(BiffPut *bp, WriteStringFlags flags,
				 guint8 const *txt);
unsigned excel_write_BOF	(BiffPut *bp, MsBiffFileType type);
void	 excel_write_SETUP	(BiffPut *bp, ExcelWriteSheet *esheet);
void	 excel_write_SCL	(BiffPut *bp, double zoom, gboolean force);

int palette_get_index (XLExportBase const *ewb, guint c);
int excel_write_get_externsheet_idx (ExcelWriteState *wb,
				     Sheet *gnum_sheeta,
				     Sheet *gnum_sheetb);

int excel_write_map_errcode (GnmValue const *v);

GHashTable *excel_collect_pivot_caches (Workbook const *wb);

void excel_sheet_extent (Sheet const *sheet, GnmRange *extent,
			 GPtrArray *col_styles,
			 int maxcols, int maxrows, GOIOContext *io_context);

int excel_font_from_go_font (XLExportBase *ewb, GOFont const *font);

void xls_write_pivot_caches (ExcelWriteState *ewb, GsfOutfile *outfile,
			     MsBiffVersion version, int codepage);

int excel_write_add_object_format (ExcelWriteState *ewb, GOFormat *format);

#endif /* GNM_MS_EXCEL_WRITE_H */
