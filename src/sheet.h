#ifndef GNUMERIC_SHEET_H
#define GNUMERIC_SHEET_H

typedef GList ColStyleList;

struct Workbook;

typedef struct {
	RowType    row;
	int        height;
	Style      *style;		/* if existant, this row style */
} RowInfo;

typedef struct {
	ColType    col;
	int        width;
	Style      style;		/* if existant, this column style */
} ColInfo;

typedef struct {
	struct     Workbook *parent_workbook;
	char       *name;
		   
	Style      style;
	GList      *cols_info;
	GList      *rows_info;
	void       *contents;
} Sheet;

typedef struct {
	Style      style;
	GHashTable *sheets;	/* keeps a list of the sheets on this workbook */
} Workbook;
#endif
