#ifndef GNUMERIC_CLIPBOARD_H
#define GNUMERIC_CLIPBOARD_H

enum {
	PASTE_VALUES   = 1 << 0,
	PASTE_FORMULAS = 1 << 1,
	PASTE_FORMATS  = 1 << 2,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OPER_ADD   = 1 << 3,
	PASTE_OPER_SUB   = 1 << 4,
	PASTE_OPER_MULT  = 1 << 5,
	PASTE_OPER_DIV   = 1 << 6,

	/* Whether the paste transposes or not */
	PASTE_TRANSPOSE  = 1 << 7
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
	guint8 type;
	char *comment;
	union {
		Cell   *cell;
		char *text;
	} u;
} CellCopy;

typedef GList CellCopyList;

struct _CellRegion {
	int          cols, rows;
	CellCopyList *list;
	GList        *styles;
};

CellRegion *clipboard_copy_cell_range    (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col,   int end_row);

void        clipboard_paste_region       (CommandContext *context, CellRegion *region,
					  Sheet      *dest_sheet,
					  int         dest_col,
					  int         dest_row,
					  int         paste_flags,
					  guint32     time32);

void        clipboard_release            (CellRegion *region);

void        x_clipboard_bind_workbook    (Workbook *wb);
	
#endif /* GNUMERIC_CLIPBOARD_H */
