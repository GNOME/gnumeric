#ifndef GNUMERIC_CLIPBOARD_H
#define GNUMERIC_CLIPBOARD_H

#include "gnumeric.h"
#include <pango/pango-context.h>

enum {
	PASTE_CONTENT		= 1 << 0, /* either CONTENT or AS_VALUES */
	PASTE_AS_VALUES		= 1 << 1, /*  can be applied, not both */
	PASTE_FORMATS		= 1 << 2,
	PASTE_COMMENTS		= 1 << 3,
	PASTE_OBJECTS		= 1 << 4,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD		= 1 << 5,
	PASTE_OPER_SUB		= 1 << 6,
	PASTE_OPER_MULT		= 1 << 7,
	PASTE_OPER_DIV		= 1 << 8,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE		= 1 << 9,

	PASTE_LINK              = 1 << 10,

	/* If copying a range that includes blank cells, this
	   prevents pasting blank cells over existing data */
	PASTE_SKIP_BLANKS       = 1 << 11,

	/* Do not paste merged regions (probably not needed) */
	PASTE_DONT_MERGE        = 1 << 12,

	/* Do not clear comments */
	PASTE_IGNORE_COMMENTS   = 1 << 13,

	/* Update the row height when pasting? (for large fonts, etc.) */
	PASTE_UPDATE_ROW_HEIGHT = 1 << 14,

	PASTE_EXPR_LOCAL_RELOCATE = 1 << 15
};

#define PASTE_ALL_TYPES (PASTE_CONTENT | PASTE_FORMATS | PASTE_COMMENTS | PASTE_OBJECTS)
#define PASTE_DEFAULT   PASTE_ALL_TYPES
#define PASTE_OPER_MASK (PASTE_OPER_ADD | PASTE_OPER_SUB | PASTE_OPER_MULT | PASTE_OPER_DIV)

typedef enum {
	CELL_COPY_TYPE_CELL,
	CELL_COPY_TYPE_TEXT
} CellCopyType;

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	CellCopyType type;
	union {
		GnmCell *cell;
		char *text;
	} u;
} CellCopy;

typedef GSList CellCopyList;

struct _GnmCellRegion {
	Sheet		*origin_sheet; /* can be NULL */
	GnmCellPos	 base;
	int		 cols, rows;
	CellCopyList	*content;
	GnmStyleList	*styles;
	GSList		*merged;
	GSList		*objects;
	gboolean	 not_as_content;
	unsigned	 ref_count;
};

struct _GnmPasteTarget {
	Sheet      *sheet;
	GnmRange    range;
	int         paste_flags;
};

GnmCellRegion *clipboard_copy_range   (Sheet *sheet, GnmRange const *r);
gboolean       clipboard_paste_region (GnmCellRegion const *content,
				       GnmPasteTarget const *pt,
				       GnmCmdContext *cc);
GnmPasteTarget*paste_target_init      (GnmPasteTarget *pt,
				       Sheet *sheet, GnmRange const *r, int flags);

GnmCellRegion *cellregion_new	 (Sheet *origin_sheet);
void        cellregion_ref       (GnmCellRegion *content);
void        cellregion_unref     (GnmCellRegion *content);
char	   *cellregion_to_string (GnmCellRegion const *content, PangoContext *context);

#endif /* GNUMERIC_CLIPBOARD_H */
