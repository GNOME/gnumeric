#include <glib.h>
#include "lpkit.h"
#include "lpglob.h"
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


/* Globals */
int        lp_solve_Level;
gnum_float lp_solve_Trej;
gnum_float lp_solve_Extrad;


lprec *
lp_solve_make_lp (int rows, int columns)
{
        lprec *newlp;
	int   i, sum;  

	if (rows < 0 || columns < 0)
	        g_print ("rows < 0 or columns < 0");

	sum = rows + columns;

	newlp = g_new (lprec, 1);

	strcpy (newlp->lp_name, "unnamed");

	newlp->verbose = FALSE;
	newlp->print_duals = FALSE;
	newlp->print_sol = FALSE;
	newlp->debug = FALSE;
	newlp->print_at_invert = FALSE;
	newlp->trace = FALSE;

	newlp->rows = rows;
	newlp->columns = columns;
	newlp->sum = sum;
	newlp->rows_alloc = rows;
	newlp->columns_alloc = columns;
	newlp->sum_alloc = sum;
	newlp->names_used = FALSE;

	newlp->obj_bound = DEF_INFINITE;
	newlp->infinite = DEF_INFINITE;
	newlp->epsilon = DEF_EPSILON;
	newlp->epsb = DEF_EPSB;
	newlp->epsd = DEF_EPSD;
	newlp->epsel = DEF_EPSEL;
	newlp->non_zeros = 0;
	newlp->mat_alloc = 1;
	newlp->mat = g_new0 (matrec, newlp->mat_alloc);
	newlp->col_no = g_new0 (int, newlp->mat_alloc + 1);
	newlp->col_end = g_new0 (int, columns + 1);
	newlp->row_end = g_new0 (int, rows + 1);
	newlp->row_end_valid = FALSE;
	newlp->orig_rh = g_new0 (gnum_float, rows + 1);
	newlp->rh = g_new0 (gnum_float, rows + 1);
	newlp->rhs = g_new0 (gnum_float, rows + 1);

	newlp->must_be_int = g_new0 (char, sum + 1);
	for (i = 0; i <= sum; i++)
	        newlp->must_be_int[i] = FALSE;

	newlp->orig_upbo = g_new0 (gnum_float, sum + 1);
	for (i = 0; i <= sum; i++)
	        newlp->orig_upbo[i] = newlp->infinite;

	newlp->upbo = g_new0 (gnum_float, sum + 1);
	newlp->orig_lowbo = g_new0 (gnum_float, sum + 1);
	newlp->lowbo = g_new0 (gnum_float, sum + 1);

	newlp->basis_valid = TRUE;
	newlp->bas = g_new0 (int, rows+1);
	newlp->basis = g_new0 (char, sum + 1);
	newlp->lower = g_new0 (char, sum + 1);

	for (i = 0; i <= rows; i++) {
	        newlp->bas[i] = i;
		newlp->basis[i] = TRUE;
	}

	for (i = rows + 1; i <= sum; i++)
	        newlp->basis[i] = FALSE;


	for (i = 0 ; i <= sum; i++)
	        newlp->lower[i] = TRUE;
 
	newlp->eta_valid = TRUE;
	newlp->eta_size = 0;
	newlp->eta_alloc = INITIAL_MAT_SIZE;
	newlp->max_num_inv = DEFNUMINV;

	newlp->nr_lagrange = 0;

	newlp->eta_value = g_new0 (gnum_float, newlp->eta_alloc);
	newlp->eta_row_nr = g_new0 (int, newlp->eta_alloc);

	/* +1 reported by Christian Rank */
	newlp->eta_col_end = g_new0 (int, newlp->rows_alloc +
				     newlp->max_num_inv + 1);

	newlp->bb_rule = FIRST_NI;
	newlp->break_at_int = FALSE;
	newlp->break_value = 0;

	newlp->iter = 0;
	newlp->total_iter = 0;
	newlp->max_total_iter = 1000; /* Default */

	newlp->solution = g_new0 (gnum_float, sum + 1);
	newlp->best_solution = g_new0 (gnum_float, sum + 1);
	newlp->duals = g_new0 (gnum_float, rows + 1);

	newlp->maximise = FALSE;
	newlp->floor_first = TRUE;

	newlp->scaling_used = FALSE;
	newlp->columns_scaled = FALSE;

	newlp->ch_sign = g_new0 (char, rows + 1);
	for (i = 0; i <= rows; i++)
	        newlp->ch_sign[i] = FALSE;

	newlp->valid = FALSE; 

	/* create two hash tables for names */

	return (newlp);
}

void
lp_solve_delete_lp (lprec *lp)
{
        int i; 

	if (lp->names_used) {
	        g_free (lp->row_name);
		g_free (lp->col_name);
	}

	g_free (lp->mat);
	g_free (lp->col_no);
	g_free (lp->col_end);
	g_free (lp->row_end);
	g_free (lp->orig_rh);
	g_free (lp->rh);
	g_free (lp->rhs);
	g_free (lp->must_be_int);
	g_free (lp->orig_upbo);
	g_free (lp->orig_lowbo);
	g_free (lp->upbo);
	g_free (lp->lowbo);
	g_free (lp->bas);
	g_free (lp->basis);
	g_free (lp->lower);
	g_free (lp->eta_value);
	g_free (lp->eta_row_nr);
	g_free (lp->eta_col_end);
	g_free (lp->solution);
	g_free (lp->best_solution);
	g_free (lp->duals);
	g_free (lp->ch_sign);
	if (lp->scaling_used)
	        g_free (lp->scale);
	if (lp->nr_lagrange > 0) {
	        g_free (lp->lag_rhs);
		g_free (lp->lambda);
		g_free (lp->lag_con_type);
		for (i = 0; i < lp->nr_lagrange; i++)
		        g_free (lp->lag_row[i]);
		g_free (lp->lag_row);
	}

	g_free (lp);
}  

static void
inc_mat_space (lprec *lp, int maxextra)
{
        if (lp->non_zeros + maxextra >= lp->mat_alloc) {
	        /* let's allocate at least INITIAL_MAT_SIZE  entries */
	        if (lp->mat_alloc < INITIAL_MAT_SIZE)
		        lp->mat_alloc = INITIAL_MAT_SIZE;
     
		/* increase the size by 50% each time it becomes too small */
		while (lp->non_zeros + maxextra >= lp->mat_alloc)
		        lp->mat_alloc *= 1.5;

		lp->mat = g_renew (matrec, lp->mat, lp->mat_alloc);
		lp->col_no = g_renew (int, lp->col_no, lp->mat_alloc + 1);
	}
}

void
lp_solve_set_max_iter (lprec *lp, int max)
{
        lp->max_total_iter = max;
}

void
lp_solve_set_max_time (lprec *lp, int max, gnum_float st)
{
        lp->max_time   = max;
	lp->start_time = st;
}

void
lp_solve_set_mat (lprec *lp, int Row, int Column, gnum_float Value)
{
        int elmnr, lastelm, i;

	/* This function is very inefficient if used to add new matrix
	 * entries in other places than at the end of the matrix. OK for
	 * replacing existing non-zero values */

	if (Row > lp->rows || Row < 0)
	        g_print ("Row out of range");
	if (Column > lp->columns || Column < 1)
	        g_print ("Column out of range");

	/* scaling is performed twice? MB */
	if (lp->scaling_used)
	        Value *= lp->scale[Row] * lp->scale[lp->rows + Column];
  
	if (lp->basis[Column] == TRUE && Row > 0)
	        lp->basis_valid = FALSE;
	lp->eta_valid = FALSE;

	/* find out if we already have such an entry */
	elmnr = lp->col_end[Column - 1];
	while ((elmnr < lp->col_end[Column]) && (lp->mat[elmnr].row_nr != Row))
	        elmnr++;
  
	if ((elmnr != lp->col_end[Column]) && (lp->mat[elmnr].row_nr == Row)) {
	        /* there is an existing entry */
	        if (ABS (Value) > lp->epsilon) {
		        /* we replace it by something non-zero */
		        if (lp->scaling_used) {
			        if (lp->ch_sign[Row])
				        lp->mat[elmnr].value = -Value *
						lp->scale[Row] * lp->scale[Column];
				else
				        lp->mat[elmnr].value = Value *
						lp->scale[Row] * lp->scale[Column];
			}
			else { /* no scaling */
			        if (lp->ch_sign[Row])
				        lp->mat[elmnr].value = -Value;
				else
				        lp->mat[elmnr].value = Value;
			}
		}
		else { /* setting existing non-zero entry to zero. Remove
			* the entry */

		        /* this might remove an entire column, or leave
			 * just a bound. No nice solution for that yet */
      
		        /* Shift the matrix */
		        lastelm = lp->non_zeros; 
			for (i = elmnr; i < lastelm ; i++)
			        lp->mat[i] = lp->mat[i + 1];
			for (i = Column; i <= lp->columns; i++)
			        lp->col_end[i]--;
      
			lp->non_zeros--;
		}
	}
	else if (ABS (Value) > lp->epsilon) {
	        /* no existing entry. make new one only if not nearly zero */
	        /* check if more space is needed for matrix */
	        inc_mat_space (lp, 1);
    
		/* Shift the matrix */
		lastelm = lp->non_zeros; 
		for (i = lastelm; i > elmnr ; i--)
		        lp->mat[i] = lp->mat[i - 1];
		for (i = Column; i <= lp->columns; i++)
		        lp->col_end[i]++;
    
		/* Set new element */
		lp->mat[elmnr].row_nr = Row;
    
		if (lp->scaling_used) {
		        if (lp->ch_sign[Row])
			        lp->mat[elmnr].value = -Value * 
				        lp->scale[Row] * lp->scale[Column];
			else
			        lp->mat[elmnr].value = Value * lp->scale[Row] *
				        lp->scale[Column];
		}
		else { /* no scaling */
		        if (lp->ch_sign[Row])
			        lp->mat[elmnr].value = -Value;
			else
			        lp->mat[elmnr].value = Value;
		}
    
		lp->row_end_valid = FALSE;
    
		lp->non_zeros++;
	}
}

void
lp_solve_set_upbo (lprec *lp, int column, gnum_float value)
{
        if (column > lp->columns || column < 1)
	        g_print ("Column out of range");
	if (lp->scaling_used)
	        value /= lp->scale[lp->rows + column];
	if (value < lp->orig_lowbo[lp->rows + column])
	        g_print ("Upperbound must be >= lowerbound"); 
	lp->eta_valid = FALSE;
	lp->orig_upbo[lp->rows + column] = value;
}

void
lp_solve_set_lowbo (lprec *lp, int column, gnum_float value)
{
        if (column > lp->columns || column < 1)
	        g_print ("Column out of range");
	if (lp->scaling_used)
	        value /= lp->scale[lp->rows + column];
	if (value > lp->orig_upbo[lp->rows + column])
	        g_print ("Upperbound must be >= lowerbound"); 
	/*
	  if (value < 0)
	  g_print ("Lower bound cannot be < 0");
	*/
	lp->eta_valid = FALSE;
	lp->orig_lowbo[lp->rows + column] = value;
}

void
lp_solve_set_int (lprec *lp, int column, gboolean must_be_int)
{
        if (column > lp->columns || column < 1)
	        g_print ("Column out of range");
	lp->must_be_int[lp->rows + column] = must_be_int;
	if (must_be_int == TRUE)
	        if (lp->columns_scaled)
		        lp_solve_unscale_columns (lp);
}

void
lp_solve_set_rh (lprec *lp, int row, gnum_float value)
{
        if (row > lp->rows || row < 0)
	        g_print ("Row out of Range");

	if ((row == 0) && (!lp->maximise))  /* setting of RHS of OF IS
					     * meaningful */
	        value = -value;
	if (lp->scaling_used) {
	        if (lp->ch_sign[row])
		        lp->orig_rh[row] = -value * lp->scale[row];
		else
		        lp->orig_rh[row] = value * lp->scale[row];
	}
	else
	        if (lp->ch_sign[row])
		        lp->orig_rh[row] = -value;
		else
		        lp->orig_rh[row] = value;
	lp->eta_valid = FALSE;
} 

void
lp_solve_set_maxim (lprec *lp)
{
        int i;
	if (lp->maximise == FALSE) {
	        for (i = 0; i < lp->non_zeros; i++)
		        if (lp->mat[i].row_nr == 0)
			        lp->mat[i].value *= -1;
		lp->eta_valid = FALSE;
		lp->orig_rh[0] *= -1;
	} 
	lp->maximise = TRUE;
	lp->ch_sign[0] = TRUE;
}

void
lp_solve_set_minim (lprec *lp)
{
        int i;
	if (lp->maximise == TRUE) {
	        for (i = 0; i < lp->non_zeros; i++)
		        if (lp->mat[i].row_nr == 0)
			        lp->mat[i].value = -lp->mat[i].value;
		lp->eta_valid = FALSE;
		lp->orig_rh[0] *= -1;
	} 
	lp->maximise = FALSE;
	lp->ch_sign[0] = FALSE;
}

void
lp_solve_set_constr_type (lprec *lp, int row, SolverConstraintType con_type)
{
        int i;
	if (row > lp->rows || row < 1)
	        g_print ("Row out of Range");
	if (con_type == SolverEQ) {
	        lp->orig_upbo[row] = 0;
		lp->basis_valid = FALSE;
		if (lp->ch_sign[row]) {
		        for (i = 0; i < lp->non_zeros; i++)
			        if (lp->mat[i].row_nr == row)
				        lp->mat[i].value *= -1;
			lp->eta_valid = FALSE;
			lp->ch_sign[row] = FALSE;
			if (lp->orig_rh[row] != 0)
			        lp->orig_rh[row] *= -1;
		}
	} else if (con_type == SolverLE) {
	        lp->orig_upbo[row] = lp->infinite;
		lp->basis_valid = FALSE;
		if (lp->ch_sign[row]) {
		        for (i = 0; i < lp->non_zeros; i++)
			        if (lp->mat[i].row_nr == row)
				        lp->mat[i].value *= -1;
			lp->eta_valid = FALSE;
			lp->ch_sign[row] = FALSE;
			if (lp->orig_rh[row] != 0)
			        lp->orig_rh[row] *= -1;
		}
	} else if (con_type == SolverGE) {
	        lp->orig_upbo[row] = lp->infinite;
		lp->basis_valid = FALSE;
		if (!lp->ch_sign[row]) {
		        for (i = 0; i < lp->non_zeros; i++)
			        if (lp->mat[i].row_nr == row)
				        lp->mat[i].value *= -1;
			lp->eta_valid = FALSE;
			lp->ch_sign[row] = TRUE;
			if (lp->orig_rh[row] != 0)
			        lp->orig_rh[row] *= -1;
		}
	} else
	        g_print ("Constraint type not (yet) implemented");
}

#if 0
gnum_float
mat_elm (lprec *lp, int row, int column)
{
        gnum_float value;
	int elmnr;
	if (row < 0 || row > lp->rows)
	        g_print ("Row out of range in mat_elm");
	if (column < 1 || column > lp->columns)
	        g_print ("Column out of range in mat_elm");
	value = 0;
	elmnr = lp->col_end[column-1];
	while (lp->mat[elmnr].row_nr != row && elmnr < lp->col_end[column])
	        elmnr++;
	if (elmnr != lp->col_end[column]) {
	        value = lp->mat[elmnr].value;
		if (lp->ch_sign[row])
		        value = -value;
		if (lp->scaling_used)
		        value /= lp->scale[row] * lp->scale[lp->rows + column];
	}
	return (value);
}
#endif

void
lp_solve_get_row (lprec *lp, int row_nr, gnum_float *row)
{
        int i, j;
	
	if (row_nr <0 || row_nr > lp->rows)
	        g_print ("Row nr. out of range in get_row");
	for (i = 1; i <= lp->columns; i++) {
	        row[i] = 0;
		for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++)
		        if (lp->mat[j].row_nr == row_nr)
			        row[i] = lp->mat[j].value;
		if (lp->scaling_used)
		        row[i] /= lp->scale[lp->rows + i] * lp->scale[row_nr];
	}
	if (lp->ch_sign[row_nr])
	        for (i = 0; i <= lp->columns; i++)
		        if (row[i] != 0)
			        row[i] = -row[i];
}

#if 0
gboolean
lp_solve_is_feasible (lprec *lp, gnum_float *values)
{
        int        i, elmnr;
	gnum_float *this_rhs;
	gnum_float dist;

	if (lp->scaling_used) {
	        for (i = lp->rows + 1; i <= lp->sum; i++)
		        if (values[i - lp->rows] < lp->orig_lowbo[i] *
			    lp->scale[i]
			    || values[i - lp->rows] > lp->orig_upbo[i] *
			    lp->scale[i])
			        return FALSE;
	} else {
	        for (i = lp->rows + 1; i <= lp->sum; i++)
		        if (values[i - lp->rows] < lp->orig_lowbo[i]
			    || values[i - lp->rows] > lp->orig_upbo[i])
			        return FALSE;
	}
	this_rhs = g_new (gnum_float, lp->rows + 1);
	if (lp->columns_scaled) {
	        for (i = 1; i <= lp->columns; i++)
		        for (elmnr = lp->col_end[i - 1];
			     elmnr < lp->col_end[i]; elmnr++)
			        this_rhs[lp->mat[elmnr].row_nr] +=
				        lp->mat[elmnr].value * values[i] /
				        lp->scale[lp->rows + i];
	} else {
	        for (i = 1; i <= lp->columns; i++)
		        for (elmnr = lp->col_end[i - 1];
			     elmnr < lp->col_end[i]; elmnr++)
			        this_rhs[lp->mat[elmnr].row_nr] +=
				        lp->mat[elmnr].value * values[i];
	}
	for (i = 1; i <= lp->rows; i++) {
	        dist = lp->orig_rh[i] - this_rhs[i];
		my_round (dist, 0.001); /* ugly constant, MB */
		if ((lp->orig_upbo[i] == 0 && dist != 0) || dist < 0) {
		        g_free (this_rhs);
			return FALSE;
		}     
	} 
	g_free (this_rhs);
	return TRUE;
}
#endif

void
lp_solve_print_lp (lprec *lp)
{
        int        i, j;
	gnum_float *fatmat;

	fatmat = g_new (gnum_float, (lp->rows + 1) * lp->columns);
	for (i = 1; i <= lp->columns; i++)
	        for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++)
		        fatmat[(i - 1) * (lp->rows + 1) + lp->mat[j].row_nr] =
			        lp->mat[j].value;

	printf ("problem name: %s\n", lp->lp_name);
	printf ("          ");
	for (j = 1; j <= lp->columns; j++)
	        if (lp->names_used)
		        printf ("%8s ", lp->col_name[j]);
		else
		        printf ("Var[%3d] ", j);
	if (lp->maximise) {
	        printf ("\nMaximise  ");
		for (j = 0; j < lp->columns; j++)
		        printf ("%8g ", (double) -fatmat[j * (lp->rows + 1)]);
	} else {
	        printf ("\nMinimize  ");
		for (j = 0; j < lp->columns; j++)
		        printf ("%8g ", (double) fatmat[j * (lp->rows + 1)]);
	}
	printf ("\n");
	for (i = 1; i <= lp->rows; i++) {
	        if (lp->names_used)
		        printf ("%-9s ", lp->row_name[i]);
		else
		        printf ("Row[%3d]  ", i);
		for (j = 0; j < lp->columns; j++)
		        if (lp->ch_sign[i] && fatmat[j*(lp->rows+1) + i] != 0)
			        printf ("%8g ",
					(double)-fatmat[j * (lp->rows+1) + i]);
			else
			        printf ("%8g ",
					(double)fatmat[j * (lp->rows + 1) + i]);
		if (lp->orig_upbo[i] != 0) {
		        if (lp->ch_sign[i])
			        printf (">= ");
			else
			        printf ("<= ");
		} else
		        printf (" = ");
		if (lp->ch_sign[i])
		        printf ("%8g", (double)-lp->orig_rh[i]);
		else
		        printf ("%8g", (double)lp->orig_rh[i]);
		if (lp->orig_lowbo[i] != 0) {
		        printf ("  %s = %8g", (lp->ch_sign[i]) ? "lowbo" :
				"upbo", (double)lp->orig_lowbo[i]);
		}
		if ((lp->orig_upbo[i] != lp->infinite) 
		    && (lp->orig_upbo[i] != 0.0)) {
		        printf ("  %s = %8g", (lp->ch_sign[i]) ? "upbo" :
				"lowbo", (double)lp->orig_upbo[i]);
		}
		printf ("\n");
	}
	printf ("Type      ");
	for (i = 1; i <= lp->columns; i++)
	        if (lp->must_be_int[lp->rows + i] == TRUE)
		        printf ("     Int ");
		else
		        printf ("    Real ");
	printf ("\nupbo      ");
	for (i = 1; i <= lp->columns; i++)
	        if (lp->orig_upbo[lp->rows + i] == lp->infinite)
		        printf (" Infinite");
		else
		        printf ("%8g ", (double)lp->orig_upbo[lp->rows + i]);
	printf ("\nlowbo     ");
	for (i = 1; i <= lp->columns; i++)
	        printf ("%8g ", (double)lp->orig_lowbo[lp->rows + i]);
	printf ("\n");
	for (i = 0; i < lp->nr_lagrange; i++) {
	        printf ("lag[%d]  ", i);
		for (j = 1; j <= lp->columns; j++)
		        printf ("%8g ", (double)lp->lag_row[i][j]);
		if (lp->orig_upbo[i] == lp->infinite) {
		        if (lp->lag_con_type[i] == SolverGE)
			        printf (">= ");
			else if (lp->lag_con_type[i] == SolverLE)
			        printf ("<= ");
			else if (lp->lag_con_type[i] == SolverEQ)
			        printf (" = ");
		}
		printf ("%8g\n", (double)lp->lag_rhs[i]);
	}

	g_free (fatmat);
}  

static gnum_float
minmax_to_scale (gnum_float min, gnum_float max)
{
        gnum_float scale;

	/* should do something sensible when min or max is 0, MB */
	if ((min == 0) || (max == 0))
	        return 1;

	/* scale = 1 / pow (10, (log10(min) + log10(max)) / 2); */
	/* Jon van Reet noticed: can be simplified to: */
	scale = 1 / sqrtgnum (min * max);

	return scale;
}

void
lp_solve_unscale_columns (lprec *lp)
{
        int i, j;

	/* unscale mat */
	for (j = 1; j <= lp->columns; j++)
	        for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++)
		        lp->mat[i].value /= lp->scale[lp->rows + j];

	/* unscale bounds as well */
	for (i = lp->rows + 1; i <= lp->sum; i++) { /* was <, changed by PN */
	        if (lp->orig_lowbo[i] != 0)
		        lp->orig_lowbo[i] *= lp->scale[i];
		if (lp->orig_upbo[i] != lp->infinite)
		        lp->orig_upbo[i] *= lp->scale[i];
	}
    
	for (i = lp->rows + 1; i<= lp->sum; i++)
	        lp->scale[i] = 1;
	lp->columns_scaled = FALSE;
	lp->eta_valid = FALSE;
}

#if 0
void
unscale (lprec *lp)
{
        int i, j;
  
	if (lp->scaling_used) {
	        /* unscale mat */
	        for (j = 1; j <= lp->columns; j++)
		        for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++)
			        lp->mat[i].value /= lp->scale[lp->rows + j];

		/* unscale bounds */
		for (i = lp->rows + 1; i <= lp->sum; i++) { /* was <, changed 
							     * by PN */
		        if (lp->orig_lowbo[i] != 0)
			        lp->orig_lowbo[i] *= lp->scale[i];
			if (lp->orig_upbo[i] != lp->infinite)
			        lp->orig_upbo[i] *= lp->scale[i];
		}
    
		/* unscale the matrix */
		for (j = 1; j <= lp->columns; j++)
		        for (i = lp->col_end[j-1]; i < lp->col_end[j]; i++)
			        lp->mat[i].value /= 
					lp->scale[lp->mat[i].row_nr];

		/* unscale the rhs! */
		for (i = 0; i <= lp->rows; i++)
		        lp->orig_rh[i] /= lp->scale[i];

		/* and don't forget to unscale the upper and lower 
		 * bounds ... */
		for (i = 0; i <= lp->rows; i++) {
		        if (lp->orig_lowbo[i] != 0)
			        lp->orig_lowbo[i] /= lp->scale[i];
			if (lp->orig_upbo[i] != lp->infinite)
			        lp->orig_upbo[i] /= lp->scale[i];
		}

		g_free (lp->scale);
		lp->scaling_used = FALSE;
		lp->eta_valid = FALSE;
	}
}
#endif

void
lp_solve_auto_scale (lprec *lp)
{
        int        i, j, row_nr;
	gnum_float *row_max, *row_min, *scalechange, absval;
	gnum_float col_max, col_min;
  
	if (!lp->scaling_used) {
	        lp->scale = g_new (gnum_float, lp->sum_alloc + 1);
		for (i = 0; i <= lp->sum; i++)
		        lp->scale[i] = 1;
	}
  
	row_max     = g_new (gnum_float, lp->rows + 1);
	row_min     = g_new (gnum_float, lp->rows + 1);
	scalechange = g_new (gnum_float, lp->sum + 1);

	/* initialise min and max values */
	for (i = 0; i <= lp->rows; i++) {
	        row_max[i] = 0;
		row_min[i] = lp->infinite;
	}

	/* calculate min and max absolute values of rows */
	for (j = 1; j <= lp->columns; j++)
	        for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++) {
		        row_nr = lp->mat[i].row_nr;
			absval = ABS (lp->mat[i].value);
			if (absval != 0) {
			        row_max[row_nr] = MAX (row_max[row_nr],
						       absval);
				row_min[row_nr] = MIN (row_min[row_nr],
						       absval);
			}
		}    
	/* calculate scale factors for rows */
	for (i = 0; i <= lp->rows; i++) {
	        scalechange[i] = minmax_to_scale (row_min[i], row_max[i]);
		lp->scale[i] *= scalechange[i];
	}

	/* now actually scale the matrix */
	for (j = 1; j <= lp->columns; j++)
	        for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++)
		        lp->mat[i].value *= scalechange[lp->mat[i].row_nr];

	/* and scale the rhs and the row bounds (RANGES in MPS!!) */
	for (i = 0; i <= lp->rows; i++) {
	        lp->orig_rh[i] *= scalechange[i];

		if ((lp->orig_upbo[i] < lp->infinite)
		    && (lp->orig_upbo[i] != 0))
		        lp->orig_upbo[i] *= scalechange[i];

		if (lp->orig_lowbo[i] != 0)
		        lp->orig_lowbo[i] *= scalechange[i];
	}

	g_free (row_max);
	g_free (row_min);
  
	/* calculate column scales */
	for (j = 1; j <= lp->columns; j++) {
	        if (lp->must_be_int[lp->rows + j]) {
		        /* do not scale integer columns */
		        scalechange[lp->rows + j] = 1;
		} else {
		        col_max = 0;
			col_min = lp->infinite;
			for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++) {
			        if (lp->mat[i].value != 0) {
				        col_max = MAX (col_max,
						       ABS (lp->mat[i].value));
					col_min = MIN (col_min,
						       ABS (lp->mat[i].value));
				}
			}
			scalechange[lp->rows + j]  = minmax_to_scale (col_min,
								      col_max);
			lp->scale[lp->rows + j] *= scalechange[lp->rows + j];
		}
	}
  
	/* scale mat */
	for (j = 1; j <= lp->columns; j++)
	        for (i = lp->col_end[j - 1]; i < lp->col_end[j]; i++)
		        lp->mat[i].value *= scalechange[lp->rows + j];
  
	/* scale bounds as well */
	for (i = lp->rows + 1; i <= lp->sum; i++) { /* was <, changed by PN */
	        if (lp->orig_lowbo[i] != 0)
		        lp->orig_lowbo[i] /= scalechange[i];
		if (lp->orig_upbo[i] != lp->infinite)
		        lp->orig_upbo[i] /= scalechange[i];
	}
	lp->columns_scaled = TRUE;

	g_free (scalechange);
	lp->scaling_used = TRUE;
	lp->eta_valid = FALSE;
}

#if 0
void
lp_solve_reset_basis (lprec *lp)
{
        lp->basis_valid = FALSE;
}

gnum_float
get_constraint_value (lprec *lp, int row)
{
        return lp->best_solution[row + 1];
}
#endif

void
lp_solve_print_solution (lprec *lp)
{
        int  i;
	FILE *stream;

	stream = stdout;

	fprintf (stream, "Value of objective function: %g\n",
		 (double)lp->best_solution[0]);

	/* print normal variables */
	for (i = 1; i <= lp->columns; i++)
	        if (lp->names_used)
		        fprintf (stream, "%-20s %g\n", lp->col_name[i],
				 (double)lp->best_solution[lp->rows + i]);
		else
		        fprintf (stream, "Var [%d] %g\n", i,
				 (double)lp->best_solution[lp->rows + i]);

	/* print achieved constraint values */
	if (lp->verbose) {
	        fprintf (stream, "\nActual values of the constraints:\n");
		for (i = 1; i <= lp->rows; i++)
		        if (lp->names_used)
			        fprintf (stream, "%-20s %g\n", lp->row_name[i],
					 (double)lp->best_solution[i]);
			else
			        fprintf (stream, "Row [%d] %g\n", i,
					 (double)lp->best_solution[i]);  
	}

	if ((lp->verbose || lp->print_duals)) {
	        if (lp->max_level != 1)
		        fprintf (stream,
				 "These are the duals from the node that "
				 "gave the optimal solution.\n");
		else
		        fprintf (stream, "\nDual values:\n");
		for (i = 1; i <= lp->rows; i++)
		        if (lp->names_used)
			        fprintf (stream, "%-20s %g\n", lp->row_name[i],
					 (double)lp->duals[i]);
			else
			        fprintf (stream, "Row [%d] %g\n", i,
					 (double)lp->duals[i]); 
	}
	fflush (stream);
}

#if 0
/* We may reuse some of the code in the MPS plugin if we implement also the
 * exporting.
 */
void
write_MPS (lprec *lp, FILE *output)
{
        int        i, j, marker, putheader;
	gnum_float *column, a;

	column = g_new (gnum_float, lp->rows + 1);
	marker = 0;   
	fprintf (output, "NAME          %s\n", lp->lp_name);
	fprintf (output, "ROWS\n");
	for (i = 0; i <= lp->rows; i++) {
	        if (i == 0)
		        fprintf (output, " N  ");
		else
		        if (lp->orig_upbo[i] != 0) {
			        if (lp->ch_sign[i])
				        fprintf (output, " G  ");
				else
				        fprintf (output, " L  ");
			}
			else
			        fprintf (output, " E  ");
		if (lp->names_used)
		        fprintf (output, "%s\n", lp->row_name[i]);
		else
		        fprintf (output, "r_%d\n", i);
	}
      
	fprintf (output, "COLUMNS\n");

	for (i = 1; i <= lp->columns; i++) {
	        if ((lp->must_be_int[i + lp->rows]) && (marker % 2) == 0) {
		        fprintf (output,
				 "    MARK%04d  'MARKER'                 "
				 "'INTORG'\n", marker);
			marker++;
		}
		if ((!lp->must_be_int[i + lp->rows]) && (marker % 2) == 1) {
		        fprintf (output,
				 "    MARK%04d  'MARKER'                 "
				 "'INTEND'\n", marker);
			marker++;
		}
		/* this gets slow for large LP problems. Implement a sparse
		 * version? */
		get_column (lp, i, column);
		j = 0;
		if (lp->maximise) {
		        if (column[j] != 0) { 
			        if (lp->names_used)
				        fprintf (output,
						 "    %-8s  %-8s  %12g\n",
						 lp->col_name[i],
						 lp->row_name[j],
						 (double)-column[j]);
				else
				        fprintf (output,
						 "    var_%-4d  r_%-6d  %12g\n"
						 , i, j, (double)-column[j]);
			}
		} else {
		        if (column[j] != 0) { 
			        if (lp->names_used)
				        fprintf (output,
						 "    %-8s  %-8s  %12g\n",
						 lp->col_name[i],
						 lp->row_name[j],
						 (double)column[j]);
				else
				        fprintf (output,
						 "    var_%-4d  r_%-6d  %12g\n"
						 , i, j, (double)column[j]);
			}
		}
		for (j = 1; j <= lp->rows; j++)
		        if (column[j] != 0) { 
			        if (lp->names_used)
				        fprintf (output,
						 "    %-8s  %-8s  %12g\n",
						 lp->col_name[i],
						 lp->row_name[j],
						 (double)column[j]);
				else
				        fprintf (output,
						 "    var_%-4d  r_%-6d  %12g\n"
						 , i, j, (double)column[j]);
			}
	}
	if ((marker % 2) == 1) {
	        fprintf (output, "    MARK%04d  'MARKER'                 "
			 "'INTEND'\n", marker);
		/* marker++; */ /* marker not used after this */
	}

	fprintf (output, "RHS\n");
	for (i = 1; i <= lp->rows; i++) {
	        a = lp->orig_rh[i];
		if (lp->scaling_used)
		        a /= lp->scale[i];

		if (lp->ch_sign[i]) {
		        if (lp->names_used)
			        fprintf (output,
					 "    RHS       %-8s  %12g\n",
					 lp->row_name[i], (double) -a);
			else
			        fprintf (output,
					 "    RHS       r_%-6d  %12g\n",
					 i, (double)-a);
		}
		else {
		        if (lp->names_used)
			        fprintf (output,
					 "    RHS       %-8s  %12g\n",
					 lp->row_name[i], (double) a);
			else
			        fprintf (output,
					 "    RHS       r_%-6d  %12g\n",
					 i, (double) a);
		}
	}

	putheader = TRUE;
	for (i = 1; i <= lp->rows; i++)
	        if ((lp->orig_upbo[i] != lp->infinite)
		    && (lp->orig_upbo[i] != 0.0)) {
		        if (putheader) {
			        fprintf (output, "RANGES\n");
				putheader = FALSE;
			}
			a = lp->orig_upbo[i];
			if (lp->scaling_used)
			        a /= lp->scale[i];
			if (lp->names_used)
			        fprintf (output,
					 "    RGS       %-8s  %12g\n",
					 lp->row_name[i], (double) a);
			else
			        fprintf (output,
					 "    RGS       r_%-6d  %12g\n", i,
					 (double) a);
		}
		else if ((lp->orig_lowbo[i] != 0.0)) {
		        if (putheader) {
			        fprintf (output, "RANGES\n");
				putheader = FALSE;
			}
			a = lp->orig_lowbo[i];
			if (lp->scaling_used)
			        a /= lp->scale[i];
			if (lp->names_used)
			        fprintf (output,
					 "    RGS       %-8s  %12g\n",
					 lp->row_name[i], (double) -a);
			else
			        fprintf (output,
					 "    RGS       r_%-6d  %12g\n", i,
					 (double) -a);
		}

	fprintf (output, "BOUNDS\n");
	if (lp->names_used)
	        for (i = lp->rows + 1; i <= lp->sum; i++) {
		        if ((lp->orig_lowbo[i] != 0)
			    && (lp->orig_upbo[i] < lp->infinite) &&
			    (lp->orig_lowbo[i] == lp->orig_upbo[i])) {
			        a = lp->orig_upbo[i];
				if (lp->scaling_used)
				        a *= lp->scale[i];
				fprintf (output, " FX BND       %-8s  %12g\n",
					 lp->col_name[i - lp->rows],
					 (double)a);
			} else {
			        if (lp->orig_upbo[i] < lp->infinite) {
				        a = lp->orig_upbo[i];
					if (lp->scaling_used)
					        a *= lp->scale[i];
					fprintf (output,
						 " UP BND       %-8s  %12g\n",
						 lp->col_name[i - lp->rows],
						 (double) a);
				}
				if (lp->orig_lowbo[i] != 0) {
				        a = lp->orig_lowbo[i];
					if (lp->scaling_used)
					        a *= lp->scale[i];
					/* bug? should a be used instead of 
					 * lp->orig_lowbo[i] MB */
					fprintf (output,
						 " LO BND       %-8s  %12g\n",
						 lp->col_name[i - lp->rows],
						 (double) lp->orig_lowbo[i]);
				}
			}
		} else
			for (i = lp->rows + 1; i <= lp->sum; i++) {
				if ((lp->orig_lowbo[i] != 0)
				    && (lp->orig_upbo[i] < lp->infinite) &&
				    (lp->orig_lowbo[i] == lp->orig_upbo[i])) {
					a = lp->orig_upbo[i];
					if (lp->scaling_used)
						a *= lp->scale[i];
					fprintf (output,
						 " FX BND       %-8s  %12g\n",
						 lp->col_name[i - lp->rows],
						 (double) a);
				} else {
					if (lp->orig_upbo[i] < lp->infinite) {
						a = lp->orig_upbo[i];
						if (lp->scaling_used)
							a *= lp->scale[i];
						fprintf (output,
							 " UP BND       var_%-4d  %12g\n",
							 i - lp->rows, (double) a);
					}
					if (lp->orig_lowbo[i] != 0) {
						a = lp->orig_lowbo[i];
						if (lp->scaling_used)
							a *= lp->scale[i];
						fprintf (output,
							 " LO BND       var_%-4d  %12g\n",
							 i - lp->rows, (double)a);
					}
				}
			}
	fprintf (output, "ENDATA\n");
	g_free (column);
}
#endif
