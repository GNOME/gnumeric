#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef unsigned char  ColType;
typedef unsigned short RowType;

typedef enum {
	VALUE_STRING,
	VALUE_NUMBER
} ValueType;

/*
 * We use the GNU Multi-precission library for storing our 
 * numbers
 */
typedef struct {
	ValueType type;
	union {
		char  *string;	/* string */
		mpf_t number;	/* floating point */
	} v;
} Value;

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

typedef struct {
	ColRowInfo *col;
	ColRowInfo *row;

	/* Text as entered by the user */
	char      *entered_text;

	/* Type of the content and the actual parsed content */
	Value     *value;
	Style     *style;
	
	/* computed versions of the cell contents */
	char      *text;	/* Text displayed */
	GdkColor  color;	/* color for the displayed text */
	int       width;	/* Width of text */
	int       height;	/* Height of text */
} Cell;

#define CELL_IS_FORMULA(cell) (cell->entered_text [0] == '=')

#endif /* GNUMERIC_CELL_H */

