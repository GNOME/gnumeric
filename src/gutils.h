#ifndef G_UTILS_H
#define G_UTILS_H

/* Gets an integer in the buffer in start to end */
void      int_get_from_range   (char *start, char *end, int_t *t);
void      float_get_from_range (char *start, char *end, float_t *t);

char      *cell_name           (int col, int row);
char      *cellref_name        (CellRef *cell_ref, int eval_col, int eval_row);

int       parse_cell_name      (char *cell_str, int *col, int *row);

#endif
