#ifndef GNUMERIC_CELL_H
#define GNUMERIC_CELL_H

typedef struct {
	int   column;
	int   row;
	Value value;
	Style style;

	/* Rendered versions of the cell */
	char  *text;		/* Text displayed */
	int   width;		/* Width of text */
	int   height;		/* Height of text */
} Cell;

#endif /* GNUMERIC_CELL_H */
