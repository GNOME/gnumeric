#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef unsigned char  ColType;
typedef unsigned short RowType;

typedef enum {
	VALUE_STRING,
	VALUE_INTEGER,
	VALUE_FLOAT
} ValueType;

/*
 * We use the GNU Multi-precission library for storing integers
 * and floating point numbers
 */
typedef struct {
	ValueType type;
	union {
		char  *string;	/* string */
		mpz_t integer;	/* integer number */
		mpf_t fp;	/* floating point */
	} v;
} Value;

typedef {
	int       ref_count;
	GdkColor  color;
} CellColor;

typedef struct {
	ColType   col;
	RowType   row;

	/* Text as entered by the user */
	char      *entered_text;

	/* Type of the content and the actual parsed content */
	Value value;
	Style style;
	
	/* computed versions of the cell contents */
	char      *text;	/* Text displayed */
	GdkColor  color;	/* color for the displayed text */
	int       width;	/* Width of text */
	int       height;	/* Height of text */
} Cell;

#define CELL_IS_FORMULA(cell) (cell->entered_text [0] == '=')

#endif /* GNUMERIC_CELL_H */
