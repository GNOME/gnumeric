#ifndef CLIPBOARD_H
#define CLIPBOARD_H

enum {
	PASTE_VALUES   = 0,
	PASTE_FORMULAS = 1,
	PASTE_FORMATS  = 2,

	/* Operations that can be performed at paste time on a cell */
	PASTE_OP_ADD   = 4,
	PASTE_OP_SUB   = 8,
	PASTE_OP_MULT  = 16,
	PASTE_OP_DIV   = 32
};

#define PASTE_ALL_TYPES (PASTE_FORMULAS | PASTE_VALUES | PASTE_FORMATS)
#define PASTE_DEFAULT   PASTE_ALL_TYPES
#define PASTE_OP_MASK   (PASTE_OP_ADD | PASTE_OP_SUB | PASTE_OP_MULT | PASTE_OP_DIV)

CellRegion *clipboard_copy_cell_range    (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col,   int end_row);

void        clipboard_paste_region       (CellRegion *region,
					  Sheet      *dest_sheet,
					  int         dest_col,
					  int         dest_row,
					  int         paste_flags,
					  guint32     time);

void        clipboard_release            (CellRegion *region);

void        x_clipboard_bind_workbook    (Workbook *wb);
	
#endif
