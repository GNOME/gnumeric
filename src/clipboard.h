#ifndef GNUMERIC_CLIPBOARD_H
#define GNUMERIC_CLIPBOARD_H

#include "gnumeric.h"

enum {
	PASTE_CONTENT		= 1 << 0, /* either CONTENT or AS_VALUES */
	PASTE_AS_VALUES		= 1 << 1, /*  can be applied, not both */
	PASTE_FORMATS		= 1 << 2,
	PASTE_COMMENTS		= 1 << 3,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD		= 1 << 4,
	PASTE_OPER_SUB		= 1 << 5,
	PASTE_OPER_MULT		= 1 << 6,
	PASTE_OPER_DIV		= 1 << 7,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE		= 1 << 8,

	PASTE_EXPR_RELOCATE	= 1 << 9,

	PASTE_LINK              = 1 << 10,

	/* If copying a range that includes blank cells, this
	   prevents pasting blank cells over existing data */
	PASTE_SKIP_BLANKS       = 1 << 11,

	/* Do not paste merged regions (probably not needed) */
	PASTE_DONT_MERGE        = 1 << 12,

	/* Do not clear comments */
	PASTE_IGNORE_COMMENTS   = 1 << 13,

	/* Update the row height when pasting? (for large fonts, etc.) */
	PASTE_UPDATE_ROW_HEIGHT = 1 << 14
};

#define PASTE_ALL_TYPES (PASTE_CONTENT | PASTE_FORMATS | PASTE_COMMENTS)
#define PASTE_DEFAULT   (PASTE_CONTENT | PASTE_FORMATS)
#define PASTE_OPER_MASK (PASTE_OPER_ADD | PASTE_OPER_SUB | PASTE_OPER_MULT | PASTE_OPER_DIV)

typedef enum {
	CELL_COPY_TYPE_CELL,
	CELL_COPY_TYPE_TEXT
} CellCopyType;

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	CellCopyType type;
	union {
		Cell *cell;
		char *text;
	} u;
	char *comment;
} CellCopy;

typedef GList CellCopyList;

struct _CellRegion {
	Sheet		*origin_sheet; /* can be NULL */
	CellPos		 base;
	int		 cols, rows;
	CellCopyList	*content;
	StyleList	*styles;
	GSList		*merged;
	gboolean	 not_as_content;
};

struct _PasteTarget {
	Sheet      *sheet;
	Range	    range;
	int         paste_flags;
};

CellRegion *clipboard_copy_range   (Sheet *sheet, Range const *r);
gboolean    clipboard_paste_region (WorkbookControl *wbc,
				    PasteTarget const *pt,
				    CellRegion const *content);
void 	    clipboard_paste	   (WorkbookControl *wbc,
				    PasteTarget const *pt, guint32 time);
PasteTarget*paste_target_init      (PasteTarget *pt,
				    Sheet *sheet, Range const *r, int flags);

CellRegion *cellregion_new	 (Sheet *origin_sheet);
void        cellregion_free      (CellRegion *content);
char	   *cellregion_to_string (CellRegion const *content);

#endif /* GNUMERIC_CLIPBOARD_H */
