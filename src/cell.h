#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef unsigned char  ColType;
typedef unsigned short RowType;

typedef struct {
	int       ref_count;
	GdkColor  color;
} CellColor;

typedef struct {
	int        pos;			/* the column or row number */
	Style      *style;		/* if existant, this row style */

	/* The height */
	int        units;		/* In units */
	int        margin_a;  		/* in pixels: top/left margin */
	int        margin_b; 		/* in pixels: bottom/right margin */
	int        pixels;		/* we compute this from the above parameters */

	unsigned   int selected:1;	/* is this selected? */

	void       *data;
} ColRowInfo;

typedef enum {
	CELL_COLOR_IS_SET
} CellFlags;

typedef struct {
	void       *sheet;
	ColRowInfo *col;
	ColRowInfo *row;

	/* Text as entered by the user */
	String    *entered_text;
	
	/* Type of the content and the actual parsed content */
	ExprTree  *parsed_node;	/* Parse tree with the expression */
	Value     *value;	/* Last value computed */
	Style     *style;
	
	/* computed versions of the cell contents */
	String    *text;	/* Text rendered and displayed */
	GdkColor  color;	/* color for the displayed text */
	int       width;	/* Width of text */
	int       height;	/* Height of text */

	int       flags;
	int       iteration;
} Cell;

#define CELL_TEXT_GET(cell) ((cell)->text ? cell->text->str : cell->entered_text->str)
#define CELL_IS_FORMULA(cell) (cell->entered_text->str [0] == '=')
#define MAX_ITERATIONS(cell) 1
void        cell_set_text             (Cell *cell, char *text);
void        cell_set_formula          (Cell *cell, char *text);

#endif /* GNUMERIC_CELL_H */

