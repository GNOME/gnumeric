#ifndef CLIPBOARD_H
#define CLIPBOARD_H

typedef struct {
	int col_offset, pos_offset; /* Position of the cell */
	Cell *cell;
} CellCopy;

typedef GList CellRegionList;

typedef struct {
	int            cols, rows;
	CelLRegionList list;
} CellRegion;

CellRegion *clipboard_copy_cell_range    (Sheet *sheet,
					  int start_col, int start_row,
					  int end_col,   int end_row);

void        clipboard_paste_region       (CellRegion *region,
					  Sheet      *dest_sheet,
					  int         dest_col,
					  int         dest_row);
#endif
