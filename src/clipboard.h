#ifndef GNUMERIC_CLIPBOARD_H
#define GNUMERIC_CLIPBOARD_H

enum {
	PASTE_VALUES		= 1 << 0, /* At most VALUES or FORMATS can */
	PASTE_FORMULAS		= 1 << 1, /*  be applied */
	PASTE_FORMATS		= 1 << 2,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD		= 1 << 3,
	PASTE_OPER_SUB		= 1 << 4,
	PASTE_OPER_MULT		= 1 << 5,
	PASTE_OPER_DIV		= 1 << 6,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE		= 1 << 7,

	PASTE_EXPR_RELOCATE	= 1 << 8,

	PASTE_LINK              = 1 << 9,

	/* If copying a range that includes blank cells, this
	   prevents pasting blank cells over existing data */
	PASTE_SKIP_BLANKS       = 1 << 10
};

#define PASTE_ALL_TYPES (PASTE_FORMULAS | PASTE_VALUES | PASTE_FORMATS)
#define PASTE_DEFAULT   PASTE_ALL_TYPES
#define PASTE_OPER_MASK (PASTE_OPER_ADD | PASTE_OPER_SUB | PASTE_OPER_MULT | PASTE_OPER_DIV)

typedef enum {
	CELL_COPY_TYPE_CELL,
	CELL_COPY_TYPE_TEXT,
	CELL_COPY_TYPE_TEXT_AND_COMMENT,
} CellCopyType;

typedef struct {
	int col_offset, row_offset; /* Position of the cell */
	CellCopyType type;
	char *comment;
	union {
		Cell   *cell;
		char *text;
	} u;
} CellCopy;

typedef GList CellCopyList;

struct _CellRegion {
	int          base_col, base_row;
	int          cols, rows;
	CellCopyList *list;
	GList        *styles;
};

struct _PasteTarget {
	Sheet      *sheet;
	Range	    range;
	int         paste_flags;
};

CellRegion *clipboard_copy_range   (Sheet *sheet, Range const *r);
void        clipboard_release      (CellRegion *region);
gboolean    clipboard_paste_region (CommandContext *context,
				    PasteTarget const *pt,
				    CellRegion *content);
void 	    clipboard_paste	   (CommandContext *context,
				    PasteTarget const *pt, guint32 time);
PasteTarget*paste_target_init      (PasteTarget *pt,
				    Sheet *sheet, Range const *r, int flags);
void        x_clipboard_bind_workbook    (Workbook *wb);
	
#endif /* GNUMERIC_CLIPBOARD_H */
