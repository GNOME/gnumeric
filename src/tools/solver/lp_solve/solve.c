#include <glib.h>
#include <string.h>
#include "lpkit.h"
#include "lpglob.h"
#include "lp-solve-debug.h"

/* Globals used by solver */
static gboolean     JustInverted;
static SolverStatus Status;
static gboolean     Doiter;
static gboolean     DoInvert;
static gboolean     Break_bb;


static void
ftran (lprec *lp, gnum_float *pcol)
{
        int        i, j, k, r, *rowp;
	gnum_float theta, *valuep;

	for (i = 1; i <= lp->eta_size; i++) {
	        k = lp->eta_col_end[i] - 1;
		r = lp->eta_row_nr[k];
		theta = pcol[r];
		if (theta != 0) {
		        j = lp->eta_col_end[i - 1];
      
			/* CPU intensive loop, let's do pointer arithmetic */
			for (rowp = lp->eta_row_nr + j,
			     valuep = lp->eta_value + j;
			     j < k; j++, rowp++, valuep++)
			        pcol[*rowp] += theta * *valuep;

			pcol[r] *= lp->eta_value[k];
		}
	}

	/* round small values to zero */
	for (i = 0; i <= lp->rows; i++)
	        my_round(pcol[i], lp->epsel);
} /* ftran */


void
btran (lprec *lp, gnum_float *row)
{
        int  i, j, k, *rowp;
	gnum_float f, *valuep;

	for (i = lp->eta_size; i >= 1; i--) {
	        f = 0;
		k = lp->eta_col_end[i] - 1;
		j = lp->eta_col_end[i - 1];

		for (rowp = lp->eta_row_nr + j, valuep = lp->eta_value + j;
		     j <= k;
		     j++, rowp++, valuep++)
		        f += row[*rowp] * *valuep;
    
		my_round(f, lp->epsel);
		row[lp->eta_row_nr[k]] = f;
	}
} /* btran */


static gboolean
isvalid (lprec *lp)
{
        int i, j, *rownum, *colnum;
	int *num, row_nr;

	if (!lp->row_end_valid) {
	        num = g_new (int, lp->rows + 1);
		rownum = g_new (int, lp->rows + 1);

		for (i = 0; i <= lp->rows; i++) {
		        num[i] = 0;
			rownum[i] = 0;
		}

		for (i = 0; i < lp->non_zeros; i++)
		        rownum[lp->mat[i].row_nr]++;
    
		lp->row_end[0] = 0;

		for (i = 1; i <= lp->rows; i++)
		        lp->row_end[i] = lp->row_end[i - 1] + rownum[i];

		for (i = 1; i <= lp->columns; i++)
		        for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++) {
			        row_nr = lp->mat[j].row_nr;
				if (row_nr != 0) {
				        num[row_nr]++;
					lp->col_no[lp->row_end[row_nr - 1]
						  + num[row_nr]] = i;
				}
			}
		
		g_free (num);
		g_free (rownum);
		lp->row_end_valid = TRUE;
	}

	if (lp->valid)
	        return TRUE;

	rownum = g_new0(int, lp->rows + 1);
	colnum = g_new0(int, lp->columns + 1);

	for (i = 1 ; i <= lp->columns; i++)
	        for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++) {
		        colnum[i]++;
			rownum[lp->mat[j].row_nr]++;
		}

	for (i = 1; i <= lp->columns; i++)
	        if (colnum[i] == 0) {
		        if (lp->names_used)
			        g_print ("Warning: Variable %s not used in "
					 "any constraints\n",
					 lp->col_name[i]);
			else
			        g_print ("Warning: Variable %d not used in "
					 "any constraints\n",
					 i);
		}
	g_free (rownum);
	g_free (colnum);
	lp->valid = TRUE;
	return TRUE;
} 

static void
resize_eta (lprec *lp, int min_size)
{
        while (lp->eta_alloc <= min_size)
	        lp->eta_alloc *= 1.5;
	/* fprintf(stderr, "resizing eta to size %d\n", lp->eta_alloc); */
	lp->eta_value = g_renew (gnum_float, lp->eta_value, lp->eta_alloc + 1);
	lp->eta_row_nr = g_renew (int, lp->eta_row_nr, lp->eta_alloc + 1);
} /* resize_eta */

static void
condensecol (lprec *lp, int row_nr, gnum_float *pcol)
{
        int i, elnr, min_size;
  
	elnr = lp->eta_col_end[lp->eta_size];

	min_size = elnr + lp->rows + 2;
	if (min_size >= lp->eta_alloc) /* maximum local growth of Eta */
	        resize_eta(lp, min_size);

	for (i = 0; i <= lp->rows; i++)
	        if (i != row_nr && pcol[i] != 0) {
		        lp->eta_row_nr[elnr] = i;
			lp->eta_value[elnr] = pcol[i];
			elnr++;
		}

	lp->eta_row_nr[elnr] = row_nr;
	lp->eta_value[elnr] = pcol[row_nr];
	elnr++;
	lp->eta_col_end[lp->eta_size + 1] = elnr;
} /* condensecol */


static void
addetacol (lprec *lp)
{
        int  i, j, k;
	gnum_float theta;
  
	j = lp->eta_col_end[lp->eta_size];
	lp->eta_size++;
	k = lp->eta_col_end[lp->eta_size] - 1;
	theta = 1 / (gnum_float) lp->eta_value[k];
	lp->eta_value[k] = theta;
	for (i = j; i < k; i++)
	        lp->eta_value[i] *= -theta;
	JustInverted = FALSE;
} /* addetacol */


static void
setpivcol (lprec *lp,
	   gboolean lower, 
	   int   varin,
	   gnum_float *pcol)
{
        int  i, colnr;
  
	for (i = 0; i <= lp->rows; i++)
	        pcol[i] = 0;

	if (lower) {
	        if (varin > lp->rows) {
		        colnr = varin - lp->rows;
			for (i = lp->col_end[colnr - 1];
			     i < lp->col_end[colnr]; i++)
			        pcol[lp->mat[i].row_nr] = lp->mat[i].value;
			pcol[0] -= lp_solve_Extrad;
		}
		else
		        pcol[varin] = 1;
	}
	else { /* !lower */
	        if (varin > lp->rows) {
		        colnr = varin - lp->rows;
			for (i = lp->col_end[colnr - 1];
			     i < lp->col_end[colnr]; i++)
			        pcol[lp->mat[i].row_nr] = -lp->mat[i].value;
			pcol[0] += lp_solve_Extrad;
		} else
		        pcol[varin] = -1;
	}

	ftran(lp, pcol);
} /* setpivcol */


static void
minoriteration (lprec *lp, int colnr, int row_nr)
{
        int  i, j, k, wk, varin, varout, elnr;
	gnum_float piv = 0, theta;
  
	varin = colnr + lp->rows;
	elnr = lp->eta_col_end[lp->eta_size];
	wk = elnr;
	lp->eta_size++;

	if (lp_solve_Extrad != 0) {
	        lp->eta_row_nr[elnr] = 0;
		lp->eta_value[elnr] = -lp_solve_Extrad;
		elnr++;
		if (elnr >= lp->eta_alloc)
		        resize_eta(lp, elnr);
	}

	for (j = lp->col_end[colnr - 1] ; j < lp->col_end[colnr]; j++) {
	        k = lp->mat[j].row_nr;

		if (k == 0 && lp_solve_Extrad != 0)
		        lp->eta_value[lp->eta_col_end[lp->eta_size - 1]] +=
			  lp->mat[j].value;
		else if (k != row_nr) {
		        lp->eta_row_nr[elnr] = k;
			lp->eta_value[elnr] = lp->mat[j].value;
			elnr++;
			if (elnr >= lp->eta_alloc)
			        resize_eta(lp, elnr);
		}
		else
		        piv = lp->mat[j].value;
	}

	lp->eta_row_nr[elnr] = row_nr;
	lp->eta_value[elnr] = 1 / piv;
	theta = lp->rhs[row_nr] / piv;
	lp->rhs[row_nr] = theta;

	for (i = wk; i < elnr; i++)
	        lp->rhs[lp->eta_row_nr[i]] -= theta * lp->eta_value[i];

	varout = lp->bas[row_nr];
	lp->bas[row_nr] = varin;
	lp->basis[varout] = FALSE;
	lp->basis[varin] = TRUE;

	for (i = wk; i < elnr; i++)
	        lp->eta_value[i] /= -piv;

	lp->eta_col_end[lp->eta_size] = elnr + 1;
} /* minoriteration */


static void
rhsmincol (lprec *lp, gnum_float theta, int row_nr, int varin)
{
        int  i, j, k, varout;
	gnum_float f;
  
	if (row_nr > lp->rows + 1) {
	        g_print ("Error: rhsmincol called with row_nr: "
			 "%d, rows: %d\n", row_nr, lp->rows);
		g_print ("This indicates numerical instability\n");
		exit(EXIT_FAILURE);
	}

	j = lp->eta_col_end[lp->eta_size];
	k = lp->eta_col_end[lp->eta_size + 1];
	for (i = j; i < k; i++) {
	        f = lp->rhs[lp->eta_row_nr[i]] - theta * lp->eta_value[i];
		my_round(f, lp->epsb);
		lp->rhs[lp->eta_row_nr[i]] = f;
	}

	lp->rhs[row_nr] = theta;
	varout = lp->bas[row_nr];
	lp->bas[row_nr] = varin;
	lp->basis[varout] = FALSE;
	lp->basis[varin] = TRUE;
} /* rhsmincol */


void
invert (lprec *lp)
{
        int    i, j, v, wk, numit, varnr, row_nr, colnr, varin;
	gnum_float theta;
	gnum_float *pcol;
	short  *frow;
	short  *fcol;
	int    *rownum, *col, *row;
	int    *colnum;

	if (lp->print_at_invert) 
	        fprintf(stderr,
			"Start Invert iter %d eta_size %d rhs[0] %g \n",
			lp->iter, lp->eta_size, (double) -lp->rhs[0]); 
 
	rownum = g_new0 (int, lp->rows + 1);
	col = g_new0 (int, lp->rows + 1);
	row = g_new0 (int, lp->rows + 1);
	pcol = g_new0 (gnum_float, lp->rows + 1);
	frow = g_new0 (short, lp->rows + 1);
	fcol = g_new0 (short, lp->columns + 1);
	colnum = g_new0 (int, lp->columns + 1);
 
	for (i = 0; i <= lp->rows; i++)
	        frow[i] = TRUE;

	for (i = 0; i < lp->columns; i++)
	        fcol[i] = FALSE;

	for (i = 0; i < lp->rows; i++)
	        rownum[i] = 0;

	for (i = 0; i <= lp->columns; i++)
	        colnum[i] = 0;

	for (i = 0; i <= lp->rows; i++)
	        if (lp->bas[i] > lp->rows)
		        fcol[lp->bas[i] - lp->rows - 1] = TRUE;
		else
		        frow[lp->bas[i]] = FALSE;
	
	for (i = 1; i <= lp->rows; i++)
	        if (frow[i])
		        for (j = lp->row_end[i - 1] + 1;
			     j <= lp->row_end[i]; j++) {
			        wk = lp->col_no[j];
				if (fcol[wk - 1]) {
				        colnum[wk]++;
					rownum[i - 1]++;
				}
			}

	for (i = 1; i <= lp->rows; i++)
	        lp->bas[i] = i;

	for (i = 1; i <= lp->rows; i++)
	        lp->basis[i] = TRUE;

	for (i = 1; i <= lp->columns; i++)
	        lp->basis[i + lp->rows] = FALSE;

	for (i = 0; i <= lp->rows; i++)
	        lp->rhs[i] = lp->rh[i];

	for (i = 1; i <= lp->columns; i++) {
	        varnr = lp->rows + i;
		if (!lp->lower[varnr]) {
		        theta = lp->upbo[varnr];
			for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++)
			        lp->rhs[lp->mat[j].row_nr] -= theta
				        * lp->mat[j].value;
		}
	}

	for (i = 1; i <= lp->rows; i++)
	        if (!lp->lower[i])
		        lp->rhs[i] -= lp->upbo[i];
	
	lp->eta_size = 0;
	v = 0;
	row_nr = 0;
	lp->num_inv = 0;
	numit = 0;

	while (v < lp->rows) {
	        row_nr++;
		if (row_nr > lp->rows)
		        row_nr = 1;

		v++;

		if (rownum[row_nr - 1] == 1)
		        if (frow[row_nr]) {
			        v = 0;
				j = lp->row_end[row_nr - 1] + 1;

				while (!(fcol[lp->col_no[j] - 1]))
				        j++;

				colnr = lp->col_no[j];
				fcol[colnr - 1] = FALSE;
				colnum[colnr] = 0;

				for (j = lp->col_end[colnr - 1];
				     j < lp->col_end[colnr]; j++)
				        if (frow[lp->mat[j].row_nr])
					        rownum[lp->mat[j].row_nr - 1]--;

				frow[row_nr] = FALSE;
				minoriteration(lp, colnr, row_nr);
			}
	}
	v = 0;
	colnr = 0;
	while (v < lp->columns) {
	        colnr++;
		if (colnr > lp->columns)
		        colnr = 1;

		v++;
    
		if (colnum[colnr] == 1)
		        if (fcol[colnr - 1]) {
			        v = 0;
				j = lp->col_end[colnr - 1] + 1;

				while (!(frow[lp->mat[j - 1].row_nr]))
				        j++;

				row_nr = lp->mat[j - 1].row_nr;
				frow[row_nr] = FALSE;
				rownum[row_nr - 1] = 0;

				for (j = lp->row_end[row_nr - 1] + 1;
				     j <= lp->row_end[row_nr]; j++)
				        if (fcol[lp->col_no[j] - 1])
					        colnum[lp->col_no[j]]--;

				fcol[colnr - 1] = FALSE;
				numit++;
				col[numit - 1] = colnr;
				row[numit - 1] = row_nr;
			}
	}
	for (j = 1; j <= lp->columns; j++)
	        if (fcol[j - 1]) {
		        fcol[j - 1] = FALSE;
			setpivcol(lp, lp->lower[lp->rows + j],
				  j + lp->rows, pcol);
			row_nr = 1;

			while ((row_nr <= lp->rows) && (!(frow[row_nr]
							  && pcol[row_nr])))
			        row_nr++;

			/* if (row_nr == lp->rows + 1) */
			if (row_nr > lp->rows) /* problems! */
			        g_print ("Inverting failed");

			frow[row_nr] = FALSE;
			condensecol(lp, row_nr, pcol);
			theta = lp->rhs[row_nr] / (gnum_float) pcol[row_nr];
			rhsmincol(lp, theta, row_nr, lp->rows + j);
			addetacol(lp);
		}

	for (i = numit - 1; i >= 0; i--) {
	        colnr = col[i];
		row_nr = row[i];
		varin = colnr + lp->rows;

		for (j = 0; j <= lp->rows; j++)
		        pcol[j] = 0;

		for (j = lp->col_end[colnr - 1]; j < lp->col_end[colnr]; j++)
		        pcol[lp->mat[j].row_nr] = lp->mat[j].value;

		pcol[0] -= lp_solve_Extrad;
		condensecol(lp, row_nr, pcol);
		theta = lp->rhs[row_nr] / (gnum_float) pcol[row_nr];
		rhsmincol(lp, theta, row_nr, varin);
		addetacol(lp);
	}

	for (i = 1; i <= lp->rows; i++)
	        my_round(lp->rhs[i], lp->epsb);

	if (lp->print_at_invert) 
	        fprintf(stderr,
			"End Invert                eta_size %d rhs[0] %g\n",
			lp->eta_size, (double) -lp->rhs[0]); 
	
	JustInverted = TRUE;
	DoInvert = FALSE;
	g_free (rownum);
	g_free (col);
	g_free (row);
	g_free (pcol);
	g_free (frow);
	g_free (fcol);
	g_free (colnum);
} /* invert */

static gboolean
colprim (lprec *lp,
	 int *colnr,
	 gboolean minit,
	 gnum_float   *drow)
{
        int  varnr, i, j;
	gnum_float f, dpiv;
  
	dpiv = -lp->epsd;
	(*colnr) = 0;
	if (!minit) {
	        for (i = 1; i <= lp->sum; i++)
		        drow[i] = 0;
		drow[0] = 1;
		btran(lp, drow);
		for (i = 1; i <= lp->columns; i++) {
		        varnr = lp->rows + i;
			if (!lp->basis[varnr])
			        if (lp->upbo[varnr] > 0) {
				  f = 0;
				  for (j = lp->col_end[i - 1]; 
				       j < lp->col_end[i]; j++)
				          f += drow[lp->mat[j].row_nr]
					    * lp->mat[j].value;
				  drow[varnr] = f;
				}
		}
		for (i = 1; i <= lp->sum; i++)
		        my_round(drow[i], lp->epsd);
	}
	for (i = 1; i <= lp->sum; i++)
	        if (!lp->basis[i])
		        if (lp->upbo[i] > 0) {
			        if (lp->lower[i])
				        f = drow[i];
				else
				        f = -drow[i];
				if (f < dpiv) {
				        dpiv = f;
					(*colnr) = i;
				}
			}
	if (lp->trace) {
	        if ((*colnr)>0)
		        fprintf(stderr, "col_prim:%d, reduced cost: %g\n",
				(*colnr), (double)dpiv);
		else
		        fprintf(stderr,
				"col_prim: no negative reduced costs found, "
				"optimality!\n");
	}
	if (*colnr == 0) {
	        Doiter   = FALSE;
		DoInvert = FALSE;
		Status   = SolverOptimal;
	}
	return((*colnr) > 0);
} /* colprim */

static gboolean
rowprim (lprec      *lp,
	 int        colnr,
	 int        *row_nr,
	 gnum_float *theta,
	 gnum_float *pcol)
{
        int  i;
	gnum_float f = -42, quot; 

	(*row_nr) = 0;
	(*theta) = lp->infinite;
	for (i = 1; i <= lp->rows; i++) {
	        f = pcol[i];
		if (f != 0) {
		        if (ABS(f) < lp_solve_Trej) {
			        lp_solve_debug_print(lp,
						     "pivot %g rejected, "
						     "too small (limit %g)\n",
						     (double)f, 
						     (double) lp_solve_Trej);
			}
			else { /* pivot alright */
			        quot = 2 * lp->infinite;
				if (f > 0)
				        quot = lp->rhs[i] / (gnum_float) f;
				else if (lp->upbo[lp->bas[i]] < lp->infinite)
				        quot = (lp->rhs[i]
						- lp->upbo[lp->bas[i]])
					  / (gnum_float) f;
				my_round(quot, lp->epsel);
				if (quot < (*theta)) {
				        (*theta) = quot;
					(*row_nr) = i;
				}
			}
		}
	}
	if ((*row_nr) == 0)  
	        for (i = 1; i <= lp->rows; i++) {
		        f = pcol[i];
			if (f != 0) {
			        quot = 2 * lp->infinite;
				if (f > 0)
				        quot = lp->rhs[i] / (gnum_float) f;
				else
				        if (lp->upbo[lp->bas[i]] < lp->infinite)
					        quot = (lp->rhs[i] -
							lp->upbo[lp->bas[i]]) /
						  (gnum_float) f;
				my_round(quot, lp->epsel);
				if (quot < (*theta)) {
				        (*theta) = quot;
					(*row_nr) = i;
				}
			}
		}

	if ((*theta) < 0) {
	        fprintf(stderr, "Warning: Numerical instability, qout = %g\n",
			(double)(*theta));
		fprintf(stderr,
			"pcol[%d] = %18g, rhs[%d] = %18g , upbo = %g\n",
			(*row_nr), (double)f, (*row_nr),
			(double)lp->rhs[(*row_nr)],
			(double)lp->upbo[lp->bas[(*row_nr)]]);
	}
	if ((*row_nr) == 0) {
	        if (lp->upbo[colnr] == lp->infinite) {
		  Doiter   = FALSE;
		  DoInvert = FALSE;
		  Status   = SolverUnbounded;
		} else {
		        i = 1;
			while (pcol[i] >= 0 && i <= lp->rows)
			        i++;
			if (i > lp->rows) { /* empty column with upperbound! */
			        lp->lower[colnr] = FALSE;
				lp->rhs[0] += lp->upbo[colnr]*pcol[0];
				Doiter = FALSE;
				DoInvert = FALSE;
			}
			else if (pcol[i]<0) {
			        (*row_nr) = i;
			}
		}
	}
	if ((*row_nr) > 0)
	        Doiter = TRUE;
	if (lp->trace)
	        fprintf(stderr, "row_prim:%d, pivot element:%18g\n", (*row_nr),
			(double)pcol[(*row_nr)]);

	return((*row_nr) > 0);
} /* rowprim */

static gboolean
rowdual (lprec *lp, int *row_nr)
{
        int        i;
	gnum_float f, g, minrhs;
	gboolean   artifs;

	(*row_nr) = 0;
	minrhs = -lp->epsb;
	i = 0;
	artifs = FALSE;
	while (i < lp->rows && !artifs) {
	        i++;
		f = lp->upbo[lp->bas[i]];
		if (f == 0 && (lp->rhs[i] != 0)) {
		        artifs = TRUE;
			(*row_nr) = i;
		} else {
		        if (lp->rhs[i] < f - lp->rhs[i])
			        g = lp->rhs[i];
			else
			        g = f - lp->rhs[i];
			if (g < minrhs) {
			        minrhs = g;
				(*row_nr) = i;
			}
		}
	}

	if (lp->trace) {  
	        if ((*row_nr) > 0) { 
		        fprintf(stderr,
				"row_dual:%d, rhs of selected row:           "
				"%18g\n",
				(*row_nr), (double)lp->rhs[(*row_nr)]);
			if (lp->upbo[lp->bas[(*row_nr)]] < lp->infinite)
			        fprintf(stderr,
					"\t\tupper bound of basis variable:"
					"%18g\n",
					(double)lp->upbo[lp->bas[(*row_nr)]]);
		} else
		        fprintf(stderr, "row_dual: no infeasibilities found\n");
	}
    
	return((*row_nr) > 0);
} /* rowdual */

static gboolean
coldual (lprec      *lp,
	 int        row_nr,
	 int        *colnr,
	 gboolean   minit,
	 gnum_float *prow,
	 gnum_float *drow)
{
        int  i, j, k, r, varnr, *rowp, row;
	gnum_float theta, quot, pivot, d, f, g, *valuep, value;
  
	Doiter = FALSE;
	if (!minit) {
	        for (i = 0; i <= lp->rows; i++) {
		        prow[i] = 0;
			drow[i] = 0;
		}

		drow[0] = 1;
		prow[row_nr] = 1;

		for (i = lp->eta_size; i >= 1; i--) {
		        d = 0;
			f = 0;
			k = lp->eta_col_end[i] - 1;
			r = lp->eta_row_nr[k];
			j = lp->eta_col_end[i - 1];
      
			/* this is one of the loops where the program 
			 * consumes a lot of CPU time */
			/* let's help the compiler by doing some pointer
			 * arithmetic instead of array indexing */
			for (rowp = lp->eta_row_nr + j,
			       valuep = lp->eta_value + j;
			     j <= k;
			     j++, rowp++, valuep++) {
			        f += prow[*rowp] * *valuep;
				d += drow[*rowp] * *valuep;
			}
			
			my_round(f, lp->epsel);
			prow[r] = f;
			my_round(d, lp->epsd);
			drow[r] = d;
		}

		for (i = 1; i <= lp->columns; i++) {
		        varnr = lp->rows + i;
			if (!lp->basis[varnr]) {
			        matrec *matentry;

				d = - lp_solve_Extrad * drow[0];
				f = 0;
				k = lp->col_end[i];
				j = lp->col_end[i - 1];

				/* this is one of the loops where the program
				 * consumes a lot of cpu time */
				/* let's help the compiler with pointer 
				 * arithmetic instead of array indexing */
				for (matentry = lp->mat + j;
				     j < k;
				     j++, matentry++) {
				        row = (*matentry).row_nr;
					value = (*matentry).value;
					d += drow[row] * value;
					f += prow[row] * value;
				}

				my_round(f, lp->epsel);
				prow[varnr] = f;
				my_round(d, lp->epsd);
				drow[varnr] = d;
			}
		}
	}

	if (lp->rhs[row_nr] > lp->upbo[lp->bas[row_nr]])
	        g = -1;
	else
	        g = 1;

	pivot = 0;
	(*colnr) = 0;
	theta = lp->infinite;

	for (i = 1; i <= lp->sum; i++) {
	        if (lp->lower[i])
		        d = prow[i] * g;
		else
		        d = -prow[i] * g;
    
		if ((d < 0) && (!lp->basis[i]) && (lp->upbo[i] > 0)) {
		        if (lp->lower[i])
			        quot = -drow[i] / (gnum_float) d;
			else
			        quot = drow[i] / (gnum_float) d;
			if (quot < theta) {
			        theta = quot;
				pivot = d;
				(*colnr) = i;
			}
			else if ((quot == theta) 
				 && (ABS(d) > ABS(pivot))) {
			        pivot = d;
				(*colnr) = i;
			}
		}
	}

	if (lp->trace)
	        fprintf(stderr,
			"col_dual:%d, pivot element:  %18g\n", (*colnr),
			(double)prow[(*colnr)]);
	
	if ((*colnr) > 0)
	        Doiter = TRUE;

	return((*colnr) > 0);
} /* coldual */

static void
iteration (lprec      *lp,
	   int        row_nr,
	   int        varin,
	   gnum_float *theta,
	   gnum_float up,
	   gboolean   *minit,
	   char       *low,
	   gboolean   primal)
{
        int        i, k, varout;
	gnum_float f;
	gnum_float pivot;
  
	lp->iter++;
  
	if (((*minit) = (*theta) > (up + lp->epsb))) {
	        (*theta) = up;
		(*low) = !(*low);
	}

	k = lp->eta_col_end[lp->eta_size + 1];
	pivot = lp->eta_value[k - 1];

	for (i = lp->eta_col_end[lp->eta_size]; i < k; i++) {
	        f = lp->rhs[lp->eta_row_nr[i]] - (*theta) * lp->eta_value[i];
		my_round(f, lp->epsb);
		lp->rhs[lp->eta_row_nr[i]] = f;
	}

	if (!(*minit)) {
	        lp->rhs[row_nr] = (*theta);
		varout = lp->bas[row_nr];
		lp->bas[row_nr] = varin;
		lp->basis[varout] = FALSE;
		lp->basis[varin] = TRUE;

		if (primal && pivot < 0)
		        lp->lower[varout] = FALSE;

		if (!(*low) && up < lp->infinite) {
		        (*low) = TRUE;
			lp->rhs[row_nr] = up - lp->rhs[row_nr];
			for (i = lp->eta_col_end[lp->eta_size]; i < k; i++)
			        lp->eta_value[i] = -lp->eta_value[i];
		}

		addetacol(lp);
		lp->num_inv++;
	}

	if (lp->trace) {
	        fprintf(stderr, "Theta = %g ", (double)(*theta));
		if ((*minit)) {
		        if (!lp->lower[varin])
			        fprintf(stderr,
					"Iteration: %d, variable %d changed "
					"from 0 to its upper bound of %g\n",
					lp->iter, varin,
					(double) lp->upbo[varin]);
			else
			        fprintf(stderr,
					"Iteration: %d, variable %d changed "
					"its upper bound of %g to 0\n",
					lp->iter, varin,
					(double) lp->upbo[varin]);
		}
		else
		  fprintf(stderr,
			  "Iteration: %d, variable %d entered basis at: %g\n",
			  lp->iter, varin, (double)lp->rhs[row_nr]);
		if (!primal) {
		        f = 0;
			for (i = 1; i <= lp->rows; i++)
			        if (lp->rhs[i] < 0)
				        f -= lp->rhs[i];
				else
				        if (lp->rhs[i] > lp->upbo[lp->bas[i]])
					        f += lp->rhs[i] -
						  lp->upbo[lp->bas[i]];
			fprintf(stderr, "feasibility gap of this basis: %g\n",
				(double) f);
		} else
		        fprintf(stderr,
				"objective function value of this feasible "
				"basis: %g\n",
				(double)lp->rhs[0]);
	}
} /* iteration */


static int
solvelp (lprec *lp)
{
        int        i, j, varnr;
	gnum_float f, theta;
	gboolean   primal;
	gnum_float *drow, *prow, *Pcol;
	gboolean   minit;
	int        colnr, row_nr;

	drow = g_new0 (gnum_float, lp->sum + 1);
	prow = g_new0 (gnum_float, lp->sum + 1);
	Pcol = g_new0 (gnum_float, lp->rows + 1);

	lp->iter = 0;
	minit    = FALSE;
	Status   = SolverRunning;
	DoInvert = FALSE;
	Doiter   = FALSE;

	for (i = 1, primal = TRUE; (i <= lp->rows) && primal; i++)
	        primal = (lp->rhs[i] >= 0)
		  && (lp->rhs[i] <= lp->upbo[lp->bas[i]]);

	if (lp->trace) {
	        if (primal)
		        fprintf(stderr, "Start at feasible basis\n");
		else
	  	        fprintf(stderr, "Start at infeasible basis\n");
	}

	if (!primal) {
	        drow[0] = 1;

		for (i = 1; i <= lp->rows; i++)
		        drow[i] = 0;

		/* fix according to Joerg Herbers */
		btran(lp, drow);

		lp_solve_Extrad = 0;

		for (i = 1; i <= lp->columns; i++) {
		        varnr = lp->rows + i;
			drow[varnr] = 0;

			for (j = lp->col_end[i - 1]; j < lp->col_end[i]; j++)
			        if (drow[lp->mat[j].row_nr] != 0)
				        drow[varnr] += drow[lp->mat[j].row_nr]
					  * lp->mat[j].value;

			if (drow[varnr] < lp_solve_Extrad)
			        lp_solve_Extrad = drow[varnr];
		}
	} else
	        lp_solve_Extrad = 0;

	if (lp->trace)
	        fprintf(stderr, "lp_solve_Extrad = %g\n",
			(double) lp_solve_Extrad);

	minit = FALSE;

	while (Status == SolverRunning) {
	        Doiter = FALSE;
		DoInvert = FALSE;

		if (primal) {
		       if (colprim(lp, &colnr, minit, drow)) {
			       setpivcol(lp, lp->lower[colnr], colnr, Pcol);
	
			       if (rowprim(lp, colnr, &row_nr, &theta, Pcol))
				       condensecol(lp, row_nr, Pcol);
		       }
		} else /* not primal */ {
		        if (!minit)
			        rowdual(lp, &row_nr);

			if (row_nr > 0 ) {
			        if (coldual(lp, row_nr, &colnr, minit, prow,
					    drow)) {
				        setpivcol(lp, lp->lower[colnr], colnr,
						  Pcol);

					/* getting div by zero here. Catch it
					 * and try to recover */
					if (Pcol[row_nr] == 0) {
					        fprintf(stderr,
							"An attempt was made "
							"to divide by zero "
							"(Pcol[%d])\n",
							row_nr);
						fprintf(stderr,
							"This indicates "
							"numerical instability"
							"\n");
						Doiter = FALSE;
						if (!JustInverted) {
						        fprintf(stderr,
								"Trying to "
								"recover. "
								"Reinverting "
								"Eta\n");
							DoInvert = TRUE;
						} else {
						        fprintf(stderr,
								"Can't "
								"reinvert, "
								"failure\n");
							Status = SolverFailure;
						}
					} else {
					        condensecol(lp, row_nr, Pcol);
						f = lp->rhs[row_nr] -
						  lp->upbo[lp->bas[row_nr]];
						
						if (f > 0) {
						        theta = f /
							  (gnum_float) Pcol[row_nr];
							if (theta <=
							    lp->upbo[colnr])
							        lp->lower[lp->bas[row_nr]] =
								  !lp->lower[lp->bas[row_nr]];
						}
						else /* f <= 0 */
						        theta = lp->rhs[row_nr] /
							  (gnum_float) Pcol[row_nr];
					}
				} else
				        Status = SolverInfeasible;
			} else {
			        primal            = TRUE;
				Doiter            = FALSE;
				lp_solve_Extrad   = 0;
				DoInvert          = TRUE;
			}	  
		}

		if (Doiter)
		        iteration(lp, row_nr, colnr, &theta, lp->upbo[colnr],
				  &minit, &lp->lower[colnr], primal);
		
		if (lp->num_inv >= lp->max_num_inv)
		        DoInvert = TRUE;

		if (DoInvert) {
		        if (lp->print_at_invert)
			        fprintf(stderr, "Inverting: Primal = %d\n",
					primal);
			invert(lp);
		}
	} 

	lp->total_iter += lp->iter;
 
	g_free (drow);
	g_free (prow);
	g_free (Pcol);

	return (Status);
} /* solvelp */


static gboolean
is_int (lprec *lp, int i)
{
        gnum_float value, error;

	value = lp->solution[i];
	error = value - floorgnum (value);

	if (error < lp->epsilon)
	        return TRUE;

	if (error > (1 - lp->epsilon))
	        return TRUE;

	return FALSE;
} /* is_int */


static void
construct_solution (lprec *lp)
{
        int        i, j, basi;
	gnum_float f;

	/* zero all results of rows */
	memset(lp->solution, '\0', (lp->rows + 1) * sizeof(gnum_float));

	lp->solution[0] = -lp->orig_rh[0];

	if (lp->scaling_used) {
	        lp->solution[0] /= lp->scale[0];

		for (i = lp->rows + 1; i <= lp->sum; i++)
		        lp->solution[i] = lp->lowbo[i] * lp->scale[i];

		for (i = 1; i <= lp->rows; i++) {
		        basi = lp->bas[i];
			if (basi > lp->rows)
			        lp->solution[basi] += lp->rhs[i]
				  * lp->scale[basi];
		}
		for (i = lp->rows + 1; i <= lp->sum; i++)
		        if (!lp->basis[i] && !lp->lower[i])
			        lp->solution[i] += lp->upbo[i] * lp->scale[i];

		for (j = 1; j <= lp->columns; j++) {
		        f = lp->solution[lp->rows + j];
			if (f != 0)
			        for (i = lp->col_end[j - 1];
				     i < lp->col_end[j]; i++)
				        lp->solution[lp->mat[i].row_nr] +=
					  (f / lp->scale[lp->rows+j])
					  * (lp->mat[i].value /
					     lp->scale[lp->mat[i].row_nr]);
		}
  
		for (i = 0; i <= lp->rows; i++) {
		        if (ABS(lp->solution[i]) < lp->epsb)
			        lp->solution[i] = 0;
			else if (lp->ch_sign[i])
			        lp->solution[i] = -lp->solution[i];
		}
	} else { /* no scaling */
	        for (i = lp->rows + 1; i <= lp->sum; i++)
		        lp->solution[i] = lp->lowbo[i];

		for (i = 1; i <= lp->rows; i++) {
		        basi = lp->bas[i];
			if (basi > lp->rows)
			        lp->solution[basi] += lp->rhs[i];
		}

		for (i = lp->rows + 1; i <= lp->sum; i++)
		        if (!lp->basis[i] && !lp->lower[i])
			        lp->solution[i] += lp->upbo[i];

		for (j = 1; j <= lp->columns; j++) {
		        f = lp->solution[lp->rows + j];
			if (f != 0)
			        for (i = lp->col_end[j - 1];
				     i < lp->col_end[j]; i++)
				        lp->solution[lp->mat[i].row_nr] +=
					  f * lp->mat[i].value;
		}
  
		for (i = 0; i <= lp->rows; i++) {
		        if (ABS(lp->solution[i]) < lp->epsb)
			        lp->solution[i] = 0;
			else if (lp->ch_sign[i])
			        lp->solution[i] = -lp->solution[i];
		}
	}
} /* construct_solution */

static void
calculate_duals (lprec *lp)
{
        int i;

	/* initialize */
	lp->duals[0] = 1;
	for (i = 1; i <= lp->rows; i++)
	        lp->duals[i] = 0;

	btran(lp, lp->duals);

	if (lp->scaling_used)
	        for (i = 1; i <= lp->rows; i++)
		        lp->duals[i] *= lp->scale[i] / lp->scale[0];

	/* the dual values are the reduced costs of the slacks */
	/* When the slack is at its upper bound, change the sign. */
	for (i = 1; i <= lp->rows; i++) {
	        if (lp->basis[i])
		        lp->duals[i] = 0;
		/* added a test if variable is different from 0 because 
		 * sometime you get -0 and this is different from 0 on
		 * for example INTEL processors (ie 0 != -0 on INTEL !) PN */
		else if ((lp->ch_sign[0] == lp->ch_sign[i]) && lp->duals[i])
		       lp->duals[i] = - lp->duals[i];
	}
} /* calculate_duals */

static void
check_if_less (gnum_float x,
	       gnum_float y,
	       gnum_float value)
{
        if (x >= y) {
	        fprintf(stderr,
			"Error: new upper or lower bound is not more "
			"restrictive\n");
		fprintf(stderr, "bound 1: %g, bound 2: %g, value: %g\n",
			(double)x, (double)y, (double)value);
		/* exit(EXIT_FAILURE); */
	}
}

#if 0
/* This is currently not used, J-P.
 */
static void
check_solution (lprec      *lp,
		gnum_float *upbo,
		gnum_float *lowbo)
{
        int i;

	/* check if all solution values are within the bounds, but allow
	 * some margin for numerical errors */

#define CHECK_EPS 1e-2

	if (lp->columns_scaled)
	        for (i = lp->rows + 1; i <= lp->sum; i++) {
		        if (lp->solution[i] < lowbo[i] * lp->scale[i]
			    - CHECK_EPS) {
			        fprintf(stderr,
					"Error: variable %d (%s) has a "
					"solution (%g) smaller than its lower "
					"bound (%g)\n",
					i - lp->rows,
					lp->col_name[i - lp->rows],
					(double)lp->solution[i],
					(double)lowbo[i] * lp->scale[i]);
				/* abort(); */
			}

			if (lp->solution[i] > upbo[i] * lp->scale[i]
			    + CHECK_EPS) {
			        fprintf(stderr,
					"Error: variable %d (%s) has a "
					"solution (%g) larger than its upper "
					"bound (%g)\n",
					i - lp->rows,
					lp->col_name[i - lp->rows],
					(double)lp->solution[i],
					(double)upbo[i] * lp->scale[i]);
				/* abort(); */
			}
		} else /* columns not scaled */
		        for (i = lp->rows + 1; i <= lp->sum; i++) {
			        if (lp->solution[i] < lowbo[i] - CHECK_EPS) {
				        fprintf(stderr,
						"Error: variable %d (%s) has "
						"a solution (%g) smaller than "
						"its lower bound (%g)\n",
						i - lp->rows,
						lp->col_name[i - lp->rows],
						(double) lp->solution[i],
						(double)lowbo[i]);
					/* abort(); */
				}
				
				if (lp->solution[i] > upbo[i] + CHECK_EPS) {
				        fprintf(stderr,
						"Error: variable %d (%s) has "
						"a solution (%g) larger than "
						"its upper bound (%g)\n",
						i - lp->rows,
						lp->col_name[i - lp->rows],
						(double) lp->solution[i],
						(double)upbo[i]);
					/* abort(); */
				}
			}
} /* check_solution */
#endif

static int
milpsolve (lprec      *lp,
	   gnum_float *upbo,
	   gnum_float *lowbo,
	   char       *sbasis,
	   char       *slower,
	   int        *sbas,
	   gboolean   recursive)
{
        int          i, j, is_worse;
	int          notint = -1;
	gnum_float   theta, tmpreal;
	SolverStatus failure;

	if (Break_bb)
	        return(BREAK_BB);

	lp_solve_Level++;
	lp->total_nodes++;

	if (lp_solve_Level > lp->max_level)
	        lp->max_level = lp_solve_Level;

	lp_solve_debug_print(lp, "starting solve");

	/* make fresh copies of upbo, lowbo, rh as solving changes them */
	memcpy (lp->upbo,  upbo,    (lp->sum + 1)  * sizeof(gnum_float));
	memcpy (lp->lowbo, lowbo,   (lp->sum + 1)  * sizeof(gnum_float));
	memcpy (lp->rh,    lp->orig_rh, (lp->rows + 1) * sizeof(gnum_float));

	/* make shure we do not do memcpy(lp->basis, lp->basis ...) ! */
	if (recursive) {
	        memcpy (lp->basis, sbasis,  (lp->sum + 1)  * sizeof(char));
		memcpy (lp->lower, slower,  (lp->sum + 1)  * sizeof(char));
		memcpy (lp->bas,   sbas,    (lp->rows + 1) * sizeof(int));
	}

	if (lp->anti_degen) { /* randomly disturb bounds */
	        for (i = 1; i <= lp->columns; i++) {
		        tmpreal = (gnum_float) (rand() % 100 * 0.00001);
			if (tmpreal > lp->epsb)
			        lp->lowbo[i + lp->rows] -= tmpreal;
			tmpreal = (gnum_float) (rand() % 100 * 0.00001);
			if (tmpreal > lp->epsb)
			        lp->upbo[i + lp->rows] += tmpreal;
		}
		lp->eta_valid = FALSE;
	}

	if (!lp->eta_valid) {
 	        /* transform to all lower bounds to zero */
	        for (i = 1; i <= lp->columns; i++)
		        if ((theta = lp->lowbo[lp->rows + i]) != 0) {
			        if (lp->upbo[lp->rows + i] < lp->infinite)
				        lp->upbo[lp->rows + i] -= theta;
				for (j = lp->col_end[i - 1];
				     j < lp->col_end[i]; j++)
				        lp->rh[lp->mat[j].row_nr] -=
					  theta * lp->mat[j].value;
			}
		invert(lp);
		lp->eta_valid = TRUE;
	}

	failure = solvelp(lp);

	if (lp->anti_degen && (failure == SolverOptimal)) {
	        /* restore to original problem, solve again starting from
		 * the basis found for the disturbed problem */

	        /* restore original problem */
	        memcpy (lp->upbo, upbo, (lp->sum + 1) * sizeof(gnum_float));
		memcpy (lp->lowbo, lowbo, (lp->sum + 1) * sizeof(gnum_float));
		memcpy (lp->rh, lp->orig_rh,
			(lp->rows + 1) * sizeof(gnum_float));

		/* transform to all lower bounds zero */
		for (i = 1; i <= lp->columns; i++)
		        if ((theta = lp->lowbo[lp->rows + i]) != 0) {
			        if (lp->upbo[lp->rows + i] < lp->infinite)
				        lp->upbo[lp->rows + i] -= theta;
				for (j = lp->col_end[i - 1];
				     j < lp->col_end[i]; j++)
				        lp->rh[lp->mat[j].row_nr] -=
					  theta * lp->mat[j].value;
			}
		invert(lp);
		lp->eta_valid = TRUE;
		failure = solvelp(lp); /* and solve again */
	}

	if (failure != SolverOptimal)
	        lp_solve_debug_print(lp, "this problem has no solution, it "
				     "is %s",
				     (failure == SolverUnbounded) ?
				     "unbounded" : "infeasible");

	if (failure == SolverInfeasible && lp->verbose)
	        fprintf(stderr, "level %d INF\n", lp_solve_Level);

	if (failure == SolverOptimal) { /* there is a good solution */
	        construct_solution (lp);
		
		/* because of reports of solution > upbo */
		/* check_solution(lp, upbo, lowbo); get too many hits ?? */

		lp_solve_debug_print(lp, "a solution was found");
		lp_solve_debug_print_solution(lp);

		/* if this solution is worse than the best sofar, this
		 * branch must die */

		/* if we can only have integer SolverOF values, we might
		 * consider requiring to be at least 1 better than the
		 * best sofar, MB */

		if (lp->maximise)
		        is_worse = lp->solution[0] <= lp->best_solution[0];
		else /* minimising! */
		        is_worse = lp->solution[0] >= lp->best_solution[0];

		if (is_worse) {
		        if (lp->verbose)
			         fprintf(stderr,
					 "level %d OPT NOB value %g bound "
					 "%g\n",
					 lp_solve_Level, (double)lp->solution[0],
					 (double)lp->best_solution[0]); 
			lp_solve_debug_print(lp, 
					     "but it was worse than the best "
					     "sofar, discarded");
			lp_solve_Level--;
			return (SolverMilpFailure);
		}

		/* check if solution contains enough ints */
		if (lp->bb_rule == FIRST_NI) {
		        for (notint = 0, i = lp->rows + 1;
			     i <= lp->sum && notint == 0;
			     i++) {
			        if (lp->must_be_int[i] && !is_int(lp, i)) {
				        if (lowbo[i] == upbo[i]) { /* this var
								    * is
								    * already
								    * fixed */
					        fprintf(stderr,
							"Warning: integer var "
							"%d is already fixed "
							"at %d, but has "
							"non-integer value "
							"%g\n",
							i - lp->rows,
							(int)lowbo[i],
							(double) lp->solution[i]);
						fprintf(stderr, 
							"Perhaps the -e "
							"option should be "
							"used\n");
					} else
					        notint = i;
				}
			}
		}
		if (lp->bb_rule == RAND_NI) {
		        int nr_not_int, select_not_int;
			nr_not_int = 0;
			
			for (i = lp->rows + 1; i <= lp->sum; i++)
			        if (lp->must_be_int[i] && !is_int(lp, i))
				        nr_not_int++;

			if (nr_not_int == 0)
			        notint = 0;
			else {
			        select_not_int = (rand() % nr_not_int) + 1;
				i = lp->rows + 1;
				while (select_not_int > 0) {
				        if (lp->must_be_int[i]
					    && !is_int(lp, i))
					        select_not_int--;
					i++;
				}
				notint = i - 1;
			}
		}
		
		if (lp->verbose) {
		        if (notint)
			        fprintf(stderr, "level %d OPT     value %g\n",
					lp_solve_Level,
					(double)lp->solution[0]);
			else
			        fprintf(stderr,
					"level %d OPT INT value %g\n",
					lp_solve_Level,
					(double)lp->solution[0]);
		}

		if (notint) { /* there is at least one value not yet int */
		        /* set up two new problems */
		        gnum_float   *new_upbo, *new_lowbo;
			gnum_float   new_bound;
			char         *new_lower, *new_basis;
			int          *new_bas;
			SolverStatus resone, restwo;

			/* allocate room for them */
			new_upbo  = g_new (gnum_float,  lp->sum + 1);
			new_lowbo = g_new (gnum_float,  lp->sum + 1);
			new_lower = g_new (char, lp->sum + 1);
			new_basis = g_new (char, lp->sum + 1);
			new_bas   = g_new (int,   lp->rows + 1);
			memcpy (new_upbo,  upbo,      (lp->sum + 1)
				* sizeof(gnum_float));
			memcpy (new_lowbo, lowbo,     (lp->sum + 1)
				* sizeof(gnum_float));
			memcpy (new_lower, lp->lower, (lp->sum + 1)
				* sizeof(char));
			memcpy (new_basis, lp->basis, (lp->sum + 1)
				* sizeof(char));
			memcpy (new_bas,   lp->bas,   (lp->rows + 1)
				* sizeof(int));
   
			if (lp->names_used)
			        lp_solve_debug_print(lp, "not enough ints. "
						     "Selecting var %s, val: "
						     "%g",
						     lp->col_name[notint -
								 lp->rows],
						     (double) lp->solution[notint]);
			else
			        lp_solve_debug_print(lp,
						     "not enough ints. "
						     "Selecting Var [%d], "
						     "val: %g",
						     notint,
						     (double) lp->solution[notint]);
			lp_solve_debug_print(lp, "current bounds:\n");
			lp_solve_debug_print_bounds(lp, upbo, lowbo);

			if (lp->floor_first) {
			        new_bound = ceilgnum (lp->solution[notint]) - 1;

				/* this bound might conflict */
				if (new_bound < lowbo[notint]) {
				        lp_solve_debug_print(lp,
							     "New upper bound "
							     "value %g "
							     "conflicts with "
							     "old lower bound "
							     "%g\n",
							     (double) new_bound,
							     (double) lowbo[notint]);
					resone = SolverMilpFailure;
				}
				else { /* bound feasible */
				        check_if_less(new_bound, upbo[notint],
						      lp->solution[notint]);
					new_upbo[notint] = new_bound;
					lp_solve_debug_print(lp, "starting "
							     "first subproblem"
							     " with bounds:");
					lp_solve_debug_print_bounds(lp,
								    new_upbo,
								    lowbo);
					lp->eta_valid = FALSE;
					resone = milpsolve(lp, new_upbo, lowbo,
							   new_basis,
							   new_lower,
							   new_bas, TRUE);
					lp->eta_valid = FALSE;
				}
				new_bound += 1;
				if (new_bound > upbo[notint]) {
				        lp_solve_debug_print(lp,
							     "New lower bound "
							     "value %g "
							     "conflicts with "
							     "old upper bound "
							     "%g\n",
							     (double) new_bound,
							     (double) upbo[notint]);
					restwo = SolverMilpFailure;
				}
				else { /* bound feasible */
				        check_if_less (lowbo[notint],
						       new_bound,
						       lp->solution[notint]);
					new_lowbo[notint] = new_bound;
					lp_solve_debug_print(lp,
							     "starting second "
							     "subproblem with "
							     "bounds:");
					lp_solve_debug_print_bounds(lp, upbo,
								    new_lowbo);
					lp->eta_valid = FALSE;
					restwo = milpsolve(lp, upbo, new_lowbo,
							   new_basis,
							   new_lower,
							   new_bas, TRUE);
					lp->eta_valid = FALSE;
				}
			}
			else { /* take ceiling first */
			        new_bound = ceilgnum (lp->solution[notint]);
				/* this bound might conflict */
				if (new_bound > upbo[notint]) {
				        lp_solve_debug_print(lp,
							     "New lower bound "
							     "value %g "
							     "conflicts with "
							     "old upper bound "
							     "%g\n",
							     (double) new_bound,
							     (double) upbo[notint]);
					resone = SolverMilpFailure;
				} else { /* bound feasible */
				        check_if_less(lowbo[notint], new_bound,
						      lp->solution[notint]);
					new_lowbo[notint] = new_bound;
					lp_solve_debug_print(lp,
							     "starting first "
							     "subproblem with "
							     "bounds:");
					lp_solve_debug_print_bounds(lp, upbo,
								    new_lowbo);
					lp->eta_valid = FALSE;
					resone = milpsolve(lp, upbo, new_lowbo,
							   new_basis,
							   new_lower,
							   new_bas, TRUE);
					lp->eta_valid = FALSE;
				}
				new_bound -= 1;
				if (new_bound < lowbo[notint]) {
				        lp_solve_debug_print(lp,
							     "New upper bound "
							     "value %g "
							     "conflicts with "
							     "old lower bound "
							     "%g\n",
							     (double) new_bound,
							     (double) lowbo[notint]);
					restwo = SolverMilpFailure;
				} else { /* bound feasible */
				        check_if_less(new_bound, upbo[notint],
						      lp->solution[notint]);
					new_upbo[notint] = new_bound;
					lp_solve_debug_print(lp,
							     "starting second "
							     "subproblem with "
							     "bounds:");
					lp_solve_debug_print_bounds(lp,
								    new_upbo,
								    lowbo);
					lp->eta_valid = FALSE;
					restwo = milpsolve(lp, new_upbo, lowbo,
							   new_basis,
							   new_lower,
							   new_bas, TRUE);
					lp->eta_valid = FALSE;
				}
			}
			if ((resone != SolverOptimal && resone != SolverMilpFailure)
			    || /* both failed and must have been infeasible */
			    (restwo != SolverOptimal && restwo != SolverMilpFailure))
			        failure = SolverInfeasible;
			else
			        failure = SolverOptimal;
			
			g_free (new_upbo);
			g_free (new_lowbo);
			g_free (new_basis);
			g_free (new_lower);
			g_free (new_bas);
		} else { /* all required values are int */
		        lp_solve_debug_print(lp, "--> valid solution found");
      
			if (lp->maximise)
			        is_worse = lp->solution[0] <
				  lp->best_solution[0];
			else
			        is_worse = lp->solution[0] >
				  lp->best_solution[0];

			if (!is_worse) { /* Current solution better */
			        if (lp->debug || (lp->verbose
						  && !lp->print_sol))
				        fprintf(stderr,
						"*** new best solution: old: "
						"%g, new: %g ***\n",
						(double)lp->best_solution[0],
						(double)lp->solution[0]);
				memcpy (lp->best_solution, lp->solution,
					(lp->sum + 1) * sizeof(gnum_float));
				calculate_duals(lp);
				
				if (lp->print_sol)
				        lp_solve_print_solution (lp); 

				if (lp->break_at_int) {
				        if (lp->maximise &&
					    (lp->best_solution[0] >
					     lp->break_value))
					        Break_bb = TRUE;

					if (!lp->maximise &&
					    (lp->best_solution[0] <
					     lp->break_value))
					        Break_bb = TRUE;
				}
			}
		}
	}

	lp_solve_Level--;

	/* failure can have the values SolverOptimal, SolverUnbounded
	 * and SolverInfeasible. */
	return(failure);
} /* milpsolve */


SolverStatus
lp_solve_solve (lprec *lp)
{
        int result, i;

	lp->total_iter  = 0;
	lp->max_level   = 1;
	lp->total_nodes = 0;

	if (isvalid(lp)) {
	        if (lp->maximise && lp->obj_bound == lp->infinite)
		        lp->best_solution[0] = -lp->infinite;
		else if (!lp->maximise && lp->obj_bound == -lp->infinite)
		        lp->best_solution[0] = lp->infinite;
		else
		  lp->best_solution[0] = lp->obj_bound;

		lp_solve_Level = 0;

		if (!lp->basis_valid) {
		        for (i = 0; i <= lp->rows; i++) {
			        lp->basis[i] = TRUE;
				lp->bas[i]   = i;
			}

			for (i = lp->rows + 1; i <= lp->sum; i++)
			        lp->basis[i] = FALSE;

			for (i = 0; i <= lp->sum; i++)
			        lp->lower[i] = TRUE;

			lp->basis_valid = TRUE;
		}

		lp->eta_valid = FALSE;
		Break_bb      = FALSE;
		result        = milpsolve(lp, lp->orig_upbo, lp->orig_lowbo,
					  lp->basis,
					  lp->lower, lp->bas, FALSE); 
		return(result);
	}

	/* if we get here, isvalid(lp) failed. I suggest we return
	 * SolverFailure
	 * fprintf (stderr, "Error, the current LP seems to be invalid\n");
	 */
	return (SolverFailure);
} /* solve */

SolverStatus
lag_solve (lprec *lp, gnum_float start_bound, int num_iter, gboolean verbose)
{
        int          i, j, citer;
	SolverStatus status, result;
	gboolean     OrigFeas, AnyFeas, same_basis;
	gnum_float   *OrigObj, *ModObj, *SubGrad, *BestFeasSol;
	gnum_float   Zub, Zlb, Ztmp, pie;
	gnum_float   rhsmod, Step, SqrsumSubGrad;
	int          *old_bas;
	char         *old_lower;

	/* allocate mem */  
	OrigObj     = g_new (gnum_float, lp->columns + 1);
	ModObj      = g_new0 (gnum_float, lp->columns + 1);
	SubGrad     = g_new0 (gnum_float, lp->nr_lagrange);
	BestFeasSol = g_new0 (gnum_float, lp->sum + 1);
	old_bas     = (int *) g_memdup (lp->bas, sizeof (int) * lp->rows + 1);
	old_lower   = g_memdup (lp->lower, sizeof (char) * lp->sum + 1);

	lp_solve_get_row (lp, 0, OrigObj);
 
	pie = 2;  

	if (lp->maximise) {
	        Zub = DEF_INFINITE;
		Zlb = start_bound;
	}
	else {
	        Zlb = -DEF_INFINITE;
		Zub = start_bound;
	}
	status   = SolverRunning; 
	Step     = 1;
	OrigFeas = FALSE;
	AnyFeas  = FALSE;
	citer    = 0;

	for (i = 0 ; i < lp->nr_lagrange; i++)
	        lp->lambda[i] = 0;

	while (status == SolverRunning) {
	        citer++;

		for (i = 1; i <= lp->columns; i++) {
		        ModObj[i] = OrigObj[i];
			for (j = 0; j < lp->nr_lagrange; j++) {
			        if (lp->maximise)
				        ModObj[i] -= lp->lambda[j] *
					  lp->lag_row[j][i]; 
				else  
				        ModObj[i] += lp->lambda[j] *
					  lp->lag_row[j][i];
			}
		}
		for (i = 1; i <= lp->columns; i++) {  
		        lp_solve_set_mat(lp, 0, i, ModObj[i]);
		}
		rhsmod = 0;
		for (i = 0; i < lp->nr_lagrange; i++)
		        if (lp->maximise)
			        rhsmod += lp->lambda[i] * lp->lag_rhs[i];
			else
			        rhsmod -= lp->lambda[i] * lp->lag_rhs[i];
 
		if (verbose) {
		        fprintf(stderr, "Zub: %10g Zlb: %10g Step: %10g pie: "
				"%10g Feas %d\n",
				(double) Zub, (double) Zlb, (double) Step,
				(double) pie, OrigFeas);
			for (i = 0; i < lp->nr_lagrange; i++)
			        fprintf(stderr,
					"%3d SubGrad %10g lambda %10g\n", i,
					(double)SubGrad[i],
					(double)lp->lambda[i]);
		}

		if (verbose && lp->sum < 20)
		        lp_solve_print_lp(lp);

		result = lp_solve_solve(lp);

		if (verbose && lp->sum < 20) { 
		        lp_solve_print_solution (lp);
		}

		same_basis = TRUE;
		i = 1;
		while (same_basis && i < lp->rows) {
		        same_basis = (old_bas[i] == lp->bas[i]);
			i++;
		}
		i = 1;
		while (same_basis && i < lp->sum) {
		        same_basis=(old_lower[i] == lp->lower[i]);
			i++;
		}
		if (!same_basis) {
		        memcpy(old_lower, lp->lower, (lp->sum+1) * sizeof(char));
			memcpy(old_bas, lp->bas, (lp->rows+1) * sizeof(int));
			pie *= 0.95;
		}
		
		if (verbose)
		        fprintf(stderr, "result: %d  same basis: %d\n",
				result, same_basis);
      
		if (result == SolverUnbounded) {
		        for (i = 1; i <= lp->columns; i++)
			        fprintf(stderr, "%g ", (double) ModObj[i]);
			exit(EXIT_FAILURE);
		}

		if (result == SolverFailure)
		        status = SolverFailure;

		if (result == SolverInfeasible)
		        status = SolverInfeasible;
      
		SqrsumSubGrad = 0;
		for (i = 0; i < lp->nr_lagrange; i++) {
		        SubGrad[i]= -lp->lag_rhs[i];
			for (j = 1; j <= lp->columns; j++)
			        SubGrad[i] += lp->best_solution[lp->rows + j]
				  * lp->lag_row[i][j];
			SqrsumSubGrad += SubGrad[i] * SubGrad[i];
		}

		OrigFeas = TRUE;
		for (i = 0; i < lp->nr_lagrange; i++)
		        if (lp->lag_con_type[i]) {
			        if (ABS(SubGrad[i]) > lp->epsb)
				        OrigFeas = FALSE;
			}
			else if (SubGrad[i] > lp->epsb)
			        OrigFeas = FALSE;

		if (OrigFeas) {
		        AnyFeas = TRUE;
			Ztmp = 0;
			for (i = 1; i <= lp->columns; i++)
			        Ztmp += lp->best_solution[lp->rows + i]
				  * OrigObj[i];
			if ((lp->maximise) && (Ztmp > Zlb)) {
			        Zlb = Ztmp;
				for (i = 1; i <= lp->sum; i++)
				        BestFeasSol[i] = lp->best_solution[i];
				BestFeasSol[0] = Zlb;
				if (verbose)
				        fprintf(stderr,
						"Best feasible solution: "
						"%g\n", (double)Zlb);
			}
			else if (Ztmp < Zub) {
			        Zub = Ztmp;
				for (i = 1; i <= lp->sum; i++)
				        BestFeasSol[i] = lp->best_solution[i];
				BestFeasSol[0] = Zub;
				if (verbose)
				        fprintf(stderr,
						"Best feasible solution: %g\n",
						(double)Zub);
			}
		}      

		if (lp->maximise)
		        Zub = MIN(Zub, rhsmod + lp->best_solution[0]);
		else
		        Zlb = MAX(Zlb, rhsmod + lp->best_solution[0]);
		
		if (ABS(Zub-Zlb)<0.001) {  
		        status = SolverOptimal;
		}
		Step = pie * ((1.05*Zub) - Zlb) / SqrsumSubGrad;  
		
		for (i = 0; i < lp->nr_lagrange; i++) {
		        lp->lambda[i] += Step * SubGrad[i];
			if (!lp->lag_con_type[i] && lp->lambda[i] < 0)
			        lp->lambda[i] = 0;
		}
 
		if (citer == num_iter && status == SolverRunning) {
		        if (AnyFeas)
			        status = FEAS_FOUND;
			else
			        status = NO_FEAS_FOUND;
		}
	}

	for (i = 0; i <= lp->sum; i++)
	        lp->best_solution[i] = BestFeasSol[i];
 
	for (i = 1; i <= lp->columns; i++)
	        lp_solve_set_mat (lp, 0, i, OrigObj[i]);

	if (lp->maximise)
	        lp->lag_bound = Zub;
	else
	        lp->lag_bound = Zlb;
	g_free (BestFeasSol);
	g_free (SubGrad);
	g_free (OrigObj);
	g_free (ModObj);
	g_free (old_bas);
	g_free (old_lower);
  
	return (status);
}

