
#include "lp_lib.h"
#include "lp_utils.h"
#include "lp_wlp.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif

#include <string.h>

/* ------------------------------------------------------------------------- */
/* Input and output of lp format model files for lp_solve                    */
/* ------------------------------------------------------------------------- */

static void write_lpcomment(FILE *output, char const *string, MYBOOL newlinebefore)
{
  fprintf(output, "%s\\* %s *\\\n", (newlinebefore) ? "\n" : "", string);
}

static void write_lprow(lprec *lp, int rowno, FILE *output)
{
  int     i, ie, j;
  REAL    a;
  MATrec  *mat = lp->matA;

  if(rowno == 0)
    i = 0;
  else
    i = mat->row_end[rowno-1];
  ie = mat->row_end[rowno];
  for(; i < ie; i++) {
    j = ROW_MAT_COL(mat->row_mat[i]);
    if(is_splitvar(lp, j))
      continue;
#if 0
    a = get_mat_byindex(lp, i, TRUE);
#else
    a = ROW_MAT_VALUE(mat->row_mat[i]);
    a = my_chsign(is_chsign(lp, rowno), a);
    a = unscaled_mat(lp, a, rowno, j);
#endif
    if(a == -1)
      fprintf(output, " -");
    else if(a == 1)
      fprintf(output, " +");
    else
      fprintf(output, " %+.12g ", (double)a);
    fprintf(output, "%s", get_col_name(lp, j));
  }
}

MYBOOL __EXPORT_TYPE __WINAPI write_lpt(lprec *lp, char *filename)
{
  int    i, j, b;
  MYBOOL ok = FALSE, ok2;
  REAL   a, constant_term;
  FILE   *output = stdout;
  char   *ptr;

#ifdef Paranoia
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "write_lpt: Cannot write to LP file while in row entry mode.\n");
    return(FALSE);
  }
#endif
  if(!mat_validate(lp->matA)) {
    report(lp, IMPORTANT, "write_lpt: Could not validate the data matrix.\n");
    return(FALSE);
  }

  ok = (MYBOOL) ((filename == NULL) || ((output = fopen(filename,"w")) != NULL));
  if(!ok)
    return(ok);
  if(filename == NULL && lp->outstream != NULL)
    output = lp->outstream;

  /* Write the objective function */
  write_lpcomment(output, "Objective function", FALSE);
  if(is_maxim(lp))
    fprintf(output, "Maximize\n");
  else
    fprintf(output, "Minimize\n");

  if(lp->names_used && (lp->row_name[0] != NULL))
    ptr = get_row_name(lp, 0);
  else
    ptr = NULL;
  if((ptr != NULL) && (*ptr))
    fprintf(output, " %s:", ptr);
  write_lprow(lp, 0, output);
  constant_term = get_rh(lp, 0);
  if(constant_term)
    fprintf(output, " +constant_term");
  fprintf(output, "\n");

  /* Write constraints */
  write_lpcomment(output, "Constraints", TRUE);
  fprintf(output, "Subject To\n");
  for(j = 1; j <= lp->rows; j++) {
    if(lp->names_used && (lp->row_name[j] != NULL))
      ptr = get_row_name(lp, j);
    else
      ptr = NULL;
    if((ptr != NULL) && (*ptr))
      fprintf(output, " %s:", ptr);

    write_lprow(lp, j, output);
    if(lp->orig_upbo[j] == 0)
      fprintf(output, " =");
    else if(is_chsign(lp, j))
      fprintf(output, " >=");
    else
      fprintf(output, " <=");
    if(fabs(get_rh(lp, j) + lp->infinite) < 1)
      fprintf(output, " -Inf;\n");
    else
      fprintf(output, " %.12g\n", get_rh(lp, j));

    /* Write the ranged part of the constraint, if specified */
    if ((lp->orig_upbo[j]) && (lp->orig_upbo[j] < lp->infinite)) {
      if((ptr != NULL) && (*ptr))
        fprintf(output, " %*s ", strlen(ptr), "");
      write_lprow(lp, j, output);
      fprintf(output, " %s %g\n",
                     (is_chsign(lp, j)) ? "<=" : ">=",
                     (lp->orig_upbo[j]-lp->orig_rh[j]) * (is_chsign(lp, j) ? 1.0 : -1.0) / (lp->scaling_used ? lp->scalars[j] : 1.0));
    }
  }

  /* Write bounds on variables */
  ok2 = FALSE;
  if(constant_term) {
    write_lpcomment(output, "Variable bounds", TRUE);
    fprintf(output, "Bounds\n");
    fprintf(output, " constant_term = %.12g\n", constant_term);
    ok2 = TRUE;
  }
  for(i = lp->rows + 1; i <= lp->sum; i++)
    if(!is_splitvar(lp, i - lp->rows)) {
      if(lp->orig_lowbo[i] == lp->orig_upbo[i]) {
	if(!ok2) {
	  write_lpcomment(output, "Variable bounds", TRUE);
	  fprintf(output, "Bounds\n");
	  ok2 = TRUE;
	}
        fprintf(output, " %s = %.12g\n", get_col_name(lp, i - lp->rows), get_upbo(lp, i - lp->rows));
      }
      else {
	if(lp->orig_lowbo[i] != 0) {
	  if(!ok2) {
	    write_lpcomment(output, "Variable bounds", TRUE);
	    fprintf(output, "Bounds\n");
	    ok2 = TRUE;
	  }
	  if(lp->orig_lowbo[i] == -lp->infinite)
	    fprintf(output, " %s >= -Inf\n", get_col_name(lp, i - lp->rows));
	  else
	    fprintf(output, " %s >= %.12g\n", get_col_name(lp, i - lp->rows),
		    (double)lp->orig_lowbo[i] * (lp->scaling_used && (lp->orig_lowbo[i] != -lp->infinite) ? lp->scalars[i] : 1.0));
	}
	if(lp->orig_upbo[i] != lp->infinite) {
	  if(!ok2) {
	    write_lpcomment(output, "Variable bounds", TRUE);
	    fprintf(output, "Bounds\n");
	    ok2 = TRUE;
	  }
	  fprintf(output, " %s <= %.12g\n", get_col_name(lp, i - lp->rows),
		  (double)lp->orig_upbo[i] * (lp->scaling_used ? lp->scalars[i] : 1.0));
	}
      }
    }

  /* Write optional integer section */
  if(lp->int_count > 0) {
    write_lpcomment(output, "Integer definitions", TRUE);
    i = 1;
    while(i <= lp->columns && !is_int(lp, i))
      i++;
    if(i <= lp->columns) {
      fprintf(output, "General\n");
      for(; i <= lp->columns; i++)
        if((!is_splitvar(lp, i)) && (is_int(lp, i)))
  	  fprintf(output, " %s", get_col_name(lp, i));
      fprintf(output, "\n");
    }
  }

  /* Write optional SEC section */
  if(lp->sc_count > 0) {
    write_lpcomment(output, "Semi-continuous variables", TRUE);
    i = 1;
    while(i <= lp->columns && !is_semicont(lp, i))
      i++;
    if(i <= lp->columns) {
      fprintf(output, "Semi-continuous\n");
      for(; i <= lp->columns; i++)
        if((!is_splitvar(lp, i)) && (is_semicont(lp, i)))
  	  fprintf(output, " %s", get_col_name(lp, i));
      fprintf(output, "\n");
    }
  }

  /* Write optional SOS section */
  if(SOS_count(lp) > 0) {
    SOSgroup *SOS = lp->SOS;
    write_lpcomment(output, "SOS definitions", TRUE);
    fprintf(output, "SOS\n");
    for(b = 0, i = 0; i < SOS->sos_count; /* b = lp->sos_list[i]->priority, */ i++) {
      fprintf(output, " S%d:: ", SOS->sos_list[i]->type);

      for(a = 0.0, j = 1; j <= SOS->sos_list[i]->size; /* a = lp->sos_list[i]->weights[j], */ j++)
        if(SOS->sos_list[i]->weights[j] == ++a)
          fprintf(output, "%s%s",
		  (j > 1) ? " " : "",
                  get_col_name(lp, SOS->sos_list[i]->members[j]));
	else
	  fprintf(output, "%s%s:%.12g",
		  (j > 1) ? " " : "",
                  get_col_name(lp, SOS->sos_list[i]->members[j]),
		  SOS->sos_list[i]->weights[j]);
      fprintf(output, "\n");
    }
  }

  fprintf(output, "\nEnd\n");
  ok = TRUE;

  if(filename != NULL)
    fclose(output);
  return(ok);
}

MYBOOL __WINAPI write_LPT(lprec *lp, FILE *output)
{
  set_outputstream(lp, output);
  return(write_lpt(lp, NULL));
}
