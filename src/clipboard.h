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

CellRegion *clipboard_copy_cell_range    (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col,   int end_row);

void        clipboard_paste_region       (CmdContext *context, CellRegion *region,
					  Sheet      *dest_sheet,
					  int         dest_col,
					  int         dest_row,
					  int         paste_flags,
					  guint32     time32);

void        clipboard_release            (CellRegion *region);

void        x_clipboard_bind_workbook    (Workbook *wb);
	
#endif /* GNUMERIC_CLIPBOARD_H */
