
/*
    Mixed integer programming optimization drivers for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Michel Berkelaar (to lp_solve v3.2),
                   Kjell Eikland
    Contact:
    License terms: LGPL.

    Requires:      stdarg.h, lp_lib.h

    Release notes:
    v5.0.0 31 January 2004      New unit isolating B&B routines.
    v5.1.0 01 February 2004     Complete rewrite into non-recursive version.

   ----------------------------------------------------------------------------------
*/

#include <stdio.h>
#include <stdarg.h>
#include "lp_lib.h"
#include "commonlib.h"
#include "lp_report.h"


#ifdef FORTIFY
# include "lp_fortify.h"
#endif

#if defined _MSC_VER
# define vsnprintf _vsnprintf
#endif

/* Various reporting functions for lp_solve                                  */
/* ------------------------------------------------------------------------- */

/* First define general utilties for reporting and output */
void __WINAPI report(lprec *lp, int level, const char *format, ...)
{
  static char buff[DEF_STRBUFSIZE+1];
  static va_list ap;

  if(lp == NULL) {
    va_start(ap, format);
      vfprintf(stderr, format, ap);
    va_end(ap);
  }
  else if(level <= lp->verbose) {
    va_start(ap, format);
    if(lp->writelog != NULL) {
      vsnprintf(buff, DEF_STRBUFSIZE, format, ap);
      lp->writelog(lp, lp->loghandle, buff);
    }
    if(lp->outstream != NULL) {
      vfprintf(lp->outstream, format, ap);
      if(lp->outstream != stdout)
        fflush(lp->outstream);
    }
    va_end(ap);
  }
#ifdef xParanoia
  if(level == CRITICAL)
    raise(SIGSEGV);
#endif
}

STATIC void print_indent(lprec *lp)
{
  int i;

  report(lp, NEUTRAL, "%2d", lp->bb_level);
  if(lp->bb_level < 50) /* useless otherwise */
    for(i = lp->bb_level; i > 0; i--)
      report(lp, NEUTRAL, "--");
  else
    report(lp, NEUTRAL, " *** too deep ***");
  report(lp, NEUTRAL, "> ");
} /* print_indent */

STATIC void debug_print(lprec *lp, char *format, ...)
{
  va_list ap;

  if(lp->bb_trace) {
    print_indent(lp);
    va_start(ap, format);
    if (lp == NULL)
    {
      vfprintf(stderr, format, ap);
      fputc('\n', stderr);
    }
    else if(lp->debuginfo != NULL)
    {
      char buff[DEF_STRBUFSIZE+1];
      vsnprintf(buff, DEF_STRBUFSIZE, format, ap);
      lp->debuginfo(lp, lp->loghandle, buff);
    }
    va_end(ap);
  }
} /* debug_print */

STATIC void debug_print_solution(lprec *lp)
{
  int i;

  if(lp->bb_trace)
    for (i = lp->rows + 1; i <= lp->sum; i++) {
      print_indent(lp);
      report(lp, NEUTRAL, "%s " MPSVALUEMASK "\n",
                 get_col_name(lp, i - lp->rows),
                (double)lp->solution[i]);
    }
} /* debug_print_solution */

STATIC void debug_print_bounds(lprec *lp, REAL *upbo, REAL *lowbo)
{
  int i;

  if(lp->bb_trace)
    for(i = lp->rows + 1; i <= lp->sum; i++) {
      if(lowbo[i] == upbo[i]) {
        print_indent(lp);
        report(lp, NEUTRAL, "%s = " MPSVALUEMASK "\n", get_col_name(lp, i - lp->rows),
                             (double)lowbo[i]);
      }
      else {
        if(lowbo[i] != 0) {
          print_indent(lp);
          report(lp, NEUTRAL, "%s > " MPSVALUEMASK "\n", get_col_name(lp, i - lp->rows),
                               (double)lowbo[i]);
        }
        if(upbo[i] != lp->infinite) {
          print_indent(lp);
          report(lp, NEUTRAL, "%s < " MPSVALUEMASK "\n", get_col_name(lp, i - lp->rows),
                               (double)upbo[i]);
    }
      }
    }
} /* debug_print_bounds */

/* List a vector of LREAL values for the given index range */
void blockWriteLREAL(FILE *output, char *label, LREAL *vector, int first, int last)
{
  int i, k = 0;

  fprintf(output, label);
  fprintf(output, "\n");
  for(i = first; i <= last; i++) {
    fprintf(output, " %18g", vector[i]);
    k++;
    if(my_mod(k, 4) == 0) {
      fprintf(output, "\n");
      k = 0;
    }
  }
  if(my_mod(k, 4) != 0)
    fprintf(output, "\n");
}

/* List the current user data matrix columns over the selected row range */
void blockWriteAMAT(FILE *output, const char *label, lprec* lp, int first, int last)
{
  int    i, j, k = 0;
  int    nzb, nze, jb;
  double hold;
  MATrec *mat = lp->matA;

  if(!mat_validate(mat))
    return;
  if(first < 0)
    first = 0;
  if(last < 0)
    last = lp->rows;

  fprintf(output, label);
  fprintf(output, "\n");

  if(first == 0)
    nze = 0;
  else
    nze = mat->row_end[first-1];
  for(i = first; i <= last; i++) {
    nzb = nze;
    nze = mat->row_end[i];
    if(nzb >= nze)
      jb = lp->columns+1;
    else
      jb = ROW_MAT_COL(mat->row_mat[nzb]);
    for(j = 1; j <= lp->columns; j++) {
      if(j < jb)
        hold = 0;
      else {
        hold = get_mat(lp, i, j);
        nzb++;
        if(nzb < nze)
          jb = ROW_MAT_COL(mat->row_mat[nzb]);
        else
          jb = lp->columns+1;
      }
      fprintf(output, " %18g", hold);
      k++;
      if(my_mod(k, 4) == 0) {
        fprintf(output, "\n");
        k = 0;
      }
    }
    if(my_mod(k, 4) != 0) {
      fprintf(output, "\n");
      k = 0;
    }
  }
  if(my_mod(k, 4) != 0)
    fprintf(output, "\n");
}

/* List the current basis matrix columns over the selected row range */
void blockWriteBMAT(FILE *output, const char *label, lprec* lp, int first, int last)
{
  int    i, j, jb, k = 0;
  double hold;

  if(first < 0)
    first = 0;
  if(last < 0)
    last = lp->rows;

  fprintf(output, label);
  fprintf(output, "\n");

  for(i = first; i <= last; i++) {
    for(j = 1; j <= lp->rows; j++) {
      jb = lp->var_basic[j];
      if(jb <= lp->rows) {
        if(jb == i)
          hold = 1;
        else
          hold = 0;
      }
      else
        hold = get_mat(lp, i, j);
      if(i == 0)
        modifyOF1(lp, jb, &hold, 1);
      hold = unscaled_mat(lp, hold, i, jb);
      fprintf(output, " %18g", hold);
      k++;
      if(my_mod(k, 4) == 0) {
        fprintf(output, "\n");
        k = 0;
      }
    }
    if(my_mod(k, 4) != 0) {
      fprintf(output, "\n");
      k = 0;
    }
  }
  if(my_mod(k, 4) != 0)
    fprintf(output, "\n");
}

/* Do a generic readable data dump of key lp_solve model variables;
   principally for run difference and debugging purposes */
MYBOOL REPORT_debugdump(lprec *lp, char *filename, MYBOOL livedata)
{
  FILE   *output = stdout;
  MYBOOL ok;

  ok = (MYBOOL) ((filename == NULL) || ((output = fopen(filename,"w")) != NULL));
  if(!ok)
    return(ok);
  if((filename == NULL) && (lp->outstream != NULL))
    output = lp->outstream;

  fprintf(output, "\nGENERAL INFORMATION\n-------------------\n\n");
  fprintf(output, "Model size:     %d rows (%d equalities, %d Lagrangean), %d columns (%d integers, %d SC, %d SOS, %d GUB)\n",
                  lp->rows, lp->equalities, get_Lrows(lp), lp->columns,
		  lp->int_count, lp->sc_count, SOS_count(lp), GUB_count(lp));
  fprintf(output, "Data size:      %d model non-zeros, %d invB non-zeros (engine is %s)\n",
                  get_nonzeros(lp), my_if(lp->invB == NULL, 0, lp->bfp_nonzeros(lp, FALSE)), lp->bfp_name());
  fprintf(output, "Internal sizes: %d rows allocated, %d columns allocated, %d columns used, %d eta length\n",
                  lp->rows_alloc, lp->columns_alloc, lp->columns, my_if(lp->invB == NULL, 0, lp->bfp_colcount(lp)));
  fprintf(output, "Memory use:     %d sparse matrix, %d eta\n",
                  lp->matA->mat_alloc, my_if(lp->invB == NULL, 0, lp->bfp_memallocated(lp)));
  fprintf(output, "Parameters:     Maximize=%d, Names used=%d, Scalingmode=%d, Presolve=%d, SimplexPivot=%d\n",
                  is_maxim(lp), lp->names_used, lp->scalemode, lp->do_presolve, lp->piv_strategy);
  fprintf(output, "Precision:      EpsValue=%g, EpsPrimal=%g, EpsDual=%g, EpsPivot=%g, EpsPerturb=%g\n",
                  lp->epsvalue, lp->epsprimal, lp->epsdual, lp->epspivot, lp->epsperturb);
  fprintf(output, "Stability:      AntiDegen=%d, Improvement=%d, Split variables at=%g\n",
                  lp->improve, lp->anti_degen, lp->negrange);
  fprintf(output, "B&B settings:   BB pivot rule=%d, BB branching=%s, BB strategy=%d, Integer precision=%g, MIP gaps=%g,%g\n",
                  lp->bb_rule, my_boolstr(lp->bb_varbranch), lp->bb_floorfirst, lp->epsint, lp->mip_absgap, lp->mip_relgap);

  fprintf(output, "\nCORE DATA\n---------\n\n");
  blockWriteINT(output,  "Column starts", lp->matA->col_end, 0, lp->columns);
  blockWriteINT(output,  "row_type", lp->row_type, 0, lp->rows);
  blockWriteREAL(output, "orig_rh", lp->orig_rh, 0, lp->rows);
  blockWriteREAL(output, "orig_lowbo", lp->orig_lowbo, 0, lp->sum);
  blockWriteREAL(output, "orig_upbo", lp->orig_upbo, 0, lp->sum);
  blockWriteINT(output,  "row_type", lp->row_type, 0, lp->rows);
  blockWriteBOOL(output,  "must_be_int", lp->must_be_int, 0, lp->columns, TRUE);
  blockWriteAMAT(output,  "A", lp, 0, lp->rows);

  if(livedata) {
    fprintf(output, "\nPROCESS DATA\n------------\n\n");
    blockWriteREAL(output,  "Active rhs", lp->rhs, 0, lp->rows);
    blockWriteINT(output,  "Basic variables", lp->var_basic, 0, lp->rows);
    blockWriteBOOL(output, "is_basic", lp->is_basic, 0, lp->sum, TRUE);
    blockWriteREAL(output, "lowbo", lp->lowbo, 0, lp->sum);
    blockWriteREAL(output, "upbo", lp->upbo, 0, lp->sum);
    if(lp->scalars != NULL)
      blockWriteREAL(output, "scalars", lp->scalars, 0, lp->sum);
  }

  if(filename != NULL)
    fclose(output);
  return(ok);
}


/* High level reports for model results */

void REPORT_objective(lprec *lp)
{
  fprintf(lp->outstream, "\nValue of objective function: %g\n",
	  (double)lp->best_solution[0]);
  fflush(lp->outstream);
}

void REPORT_solution(lprec *lp, int columns)
{
  int i, n;
  REAL value;
  MYBOOL NZonly = (MYBOOL) ((lp->print_sol & AUTOMATIC) > 0);

  fprintf(lp->outstream, "\nActual values of the variables:\n");
  if(columns <= 0)
    columns = 2;
  n = 0;
  for(i = 1; i <= lp->orig_columns; i++) {
    value = get_var_primalresult(lp, lp->orig_rows + i);
    if(NZonly && (fabs(value) < lp->epsprimal))
      continue;
    n = (n+1) % columns;
    fprintf(lp->outstream, "%-20s %12g", get_origcol_name(lp, i), (double) value);
    if(n == 0)
      fprintf(lp->outstream, "\n");
    else
      fprintf(lp->outstream, "       ");
  }

  fflush(lp->outstream);
} /* REPORT_solution */

void REPORT_constraints(lprec *lp, int columns)
{
  int i, n;
  REAL value;
  MYBOOL NZonly = (MYBOOL) ((lp->print_sol & AUTOMATIC) > 0);

  if(columns <= 0)
    columns = 2;

  fprintf(lp->outstream, "\nActual values of the constraints:\n");
  n = 0;
  for(i = 1; i <= lp->rows; i++) {
    value = (double)lp->best_solution[i];
    if(NZonly && (fabs(value) < lp->epsprimal))
      continue;
    n = (n+1) % columns;
    fprintf(lp->outstream, "%-20s %12g", get_row_name(lp, i), value);
    if(n == 0)
      fprintf(lp->outstream, "\n");
    else
      fprintf(lp->outstream, "       ");
  }

  fflush(lp->outstream);
}

void REPORT_duals(lprec *lp)
{
  int i;
  REAL *duals, *dualsfrom, *dualstill, *objfrom, *objtill;
  MYBOOL ret;

  ret = get_ptr_sensitivity_obj(lp, &objfrom, &objtill);
  if(ret) {
    fprintf(lp->outstream, "\nObjective function limits:\n");
    fprintf(lp->outstream, "                                 From            Till\n");
    for(i = 1; i <= lp->columns; i++)
      if(!is_splitvar(lp, i))
        fprintf(lp->outstream, "%-20s  %15.7g %15.7g\n", get_col_name(lp, i),
         (double)objfrom[i - 1], (double)objtill[i - 1]);
  }

  ret = get_ptr_sensitivity_rhs(lp, &duals, &dualsfrom, &dualstill);
  if(ret) {
    fprintf(lp->outstream, "\nDual values with from - till limits:\n");
    fprintf(lp->outstream, "                   Dual value      From     Till\n");
    for(i = 1; i <= lp->sum; i++)
      fprintf(lp->outstream, "%-20s %8g  %8g %8g\n",
              (i <= lp->rows) ? get_row_name(lp, i) : get_col_name(lp, i - lp->rows),
              (double)duals[i - 1], (double)dualsfrom[i - 1], (double)dualstill[i - 1]);
    fflush(lp->outstream);
  }
}

/* Printing of sensitivity analysis reports */
void REPORT_extended(lprec *lp)
{
  int  i, j;
  REAL hold;
  REAL *duals, *dualsfrom, *dualstill, *objfrom, *objtill;
  MYBOOL ret;

  ret = get_ptr_sensitivity_obj(lp, &objfrom, &objtill);
  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "Primal objective:\n");
  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "  Column name                      Value   Objective         Min         Max\n");
  report(lp, NORMAL, "  --------------------------------------------------------------------------\n");
  for(j = 1; j <= lp->columns; j++) {
    hold = get_mat(lp,0,j);
    report(lp, NORMAL, "  %-25s " MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK "\n",
           get_col_name(lp,j),
           my_precision(hold,lp->epsprimal),
           my_precision(hold*lp->best_solution[lp->rows+j],lp->epsprimal),
           my_precision((ret) ? objfrom[j - 1] : 0.0,lp->epsprimal),
           my_precision((ret) ? objtill[j - 1] : 0.0,lp->epsprimal));
  }
  report(lp, NORMAL, " \n");

  ret = get_ptr_sensitivity_rhs(lp, &duals, &dualsfrom, &dualstill);
  report(lp, NORMAL, "Primal variables:\n");
  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "  Column name                      Value       Slack         Min         Max\n");
  report(lp, NORMAL, "  --------------------------------------------------------------------------\n");
  for(j = 1; j <= lp->columns; j++)
    report(lp, NORMAL, "  %-25s " MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK "\n",
           get_col_name(lp,j),
           my_precision(lp->best_solution[lp->rows+j],lp->epsprimal),
           my_precision(my_inflimit((ret) ? duals[lp->rows+j-1] : 0.0),lp->epsprimal),
           my_precision((ret) ? dualsfrom[lp->rows+j-1] : 0.0,lp->epsprimal),
           my_precision((ret) ? dualstill[lp->rows+j-1] : 0.0,lp->epsprimal));

  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "Dual variables:\n");
  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "  Row name                         Value       Slack         Min         Max\n");
  report(lp, NORMAL, "  --------------------------------------------------------------------------\n");
  for(i = 1; i <= lp->rows; i++)
    report(lp, NORMAL, "  %-25s " MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK MPSVALUEMASK "\n",
           get_row_name(lp,i),
           my_precision((ret) ? duals[i - 1] : 0.0, lp->epsprimal),
           my_precision(lp->best_solution[i], lp->epsprimal),
           my_precision((ret) ? dualsfrom[i - 1] : 0.0,lp->epsprimal),
           my_precision((ret) ? dualstill[i - 1] : 0.0,lp->epsprimal));

  report(lp, NORMAL, " \n");
}

/* A more readable lp-format report of the model; antiquated and not updated */
void REPORT_lp(lprec *lp)
{
  int  i, j;

#ifdef Paranoia
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "REPORT_lp: Cannot print lp while in row entry mode.\n");
    return;
  }
#endif

  fprintf(lp->outstream, "Model name: %s\n", lp->lp_name);
  fprintf(lp->outstream, "          ");

  for(j = 1; j <= lp->columns; j++)
    fprintf(lp->outstream, "%8s ", get_col_name(lp,j));

  fprintf(lp->outstream, "\n%s  ", (is_maxim(lp) ? "Maximize" : "Minimize"));
  for(j = 1; j <= lp->columns; j++)
      fprintf(lp->outstream, "%8g ", get_mat(lp, 0, j));
  fprintf(lp->outstream, "\n");

  for(i = 1; i <= lp->rows; i++) {
    fprintf(lp->outstream, "%-9s ", get_row_name(lp, i));
    for(j = 1; j <= lp->columns; j++)
	  fprintf(lp->outstream, "%8g ", get_mat(lp, i, j));
    if(is_constr_type(lp, i, GE))
      fprintf(lp->outstream, ">= ");
    else if(is_constr_type(lp, i, LE))
      fprintf(lp->outstream, "<= ");
    else
      fprintf(lp->outstream, " = ");
    fprintf(lp->outstream, "%8g", get_rh(lp, i));

    if(is_constr_type(lp, i, GE)) {
      if(get_rh_upper(lp, i) < lp->infinite)
        fprintf(lp->outstream, "  %s = %8g", "upbo", get_rh_upper(lp, i));
    }
    else if(is_constr_type(lp, i, LE)) {
      if(get_rh_lower(lp, i) > -lp->infinite)
        fprintf(lp->outstream, "  %s = %8g", "lowbo", get_rh_lower(lp, i));
    }
    fprintf(lp->outstream, "\n");
  }

  fprintf(lp->outstream, "Type      ");
  for(i = 1; i <= lp->columns; i++) {
    if(is_int(lp,i))
      fprintf(lp->outstream, "     Int ");
    else
      fprintf(lp->outstream, "    Real ");
  }

  fprintf(lp->outstream, "\nupbo      ");
  for(i = 1; i <= lp->columns; i++)
    if(get_upbo(lp, i) >= lp->infinite)
      fprintf(lp->outstream, "     Inf ");
    else
      fprintf(lp->outstream, "%8g ", get_upbo(lp, i));
  fprintf(lp->outstream, "\nlowbo     ");
  for(i = 1; i <= lp->columns; i++)
    if(get_lowbo(lp, i) <= -lp->infinite)
      fprintf(lp->outstream, "    -Inf ");
    else
      fprintf(lp->outstream, "%8g ", get_lowbo(lp, i));
  fprintf(lp->outstream, "\n");

  fflush(lp->outstream);
}

/* Report the scaling factors used; extremely rarely used */
void REPORT_scales(lprec *lp)
{
  int i, colMax;

  colMax = lp->columns;

  if(lp->scaling_used) {
    fprintf(lp->outstream, "\nScale factors:\n");
    for(i = 0; i <= lp->rows + colMax; i++)
      fprintf(lp->outstream, "%-20s scaled at %g\n",
              (i <= lp->rows) ? get_row_name(lp, i) : get_col_name(lp, i - lp->rows),
	      (double)lp->scalars[i]);
  }
  fflush(lp->outstream);
}

/* Report the traditional tableau corresponding to the current basis */
MYBOOL REPORT_tableau(lprec *lp)
{
  int  j, row_nr;
  REAL *prow = NULL;
  FILE *stream = lp->outstream;

  if(!lp->model_is_valid || !has_BFP(lp) ||
     (get_total_iter(lp) == 0) || (lp->spx_status == NOTRUN)) {
    lp->spx_status = NOTRUN;
    return(FALSE);
  }
  if(!allocREAL(lp, &prow,lp->sum + 1, TRUE)) {
    lp->spx_status = NOMEMORY;
    return(FALSE);
  }

  fprintf(stream, "\n");
  fprintf(stream, "Tableau at iteration %d:\n", get_total_iter(lp));

  for(j = 1; j <= lp->sum; j++)
    if (!lp->is_basic[j])
      fprintf(stream, "%15d", (j <= lp->rows ?
                               (j + lp->columns) * ((lp->orig_upbo[j] == 0) ||
                                                    (is_chsign(lp, j)) ? 1 : -1) : j - lp->rows) *
                              (lp->is_lower[j] ? 1 : -1));
  fprintf(stream, "\n");

  for(row_nr = 1; (row_nr <= lp->rows + 1); row_nr++) {
    if (row_nr <= lp->rows)
      fprintf(stream, "%3d", (lp->var_basic[row_nr] <= lp->rows ?
                              (lp->var_basic[row_nr] + lp->columns) * ((lp->orig_upbo[lp->var_basic [row_nr]] == 0) ||
                                                                       (is_chsign(lp, lp->var_basic[row_nr])) ? 1 : -1) : lp->var_basic[row_nr] - lp->rows) *
                             (lp->is_lower[lp->var_basic [row_nr]] ? 1 : -1));
    else
      fprintf(stream, "   ");
    bsolve(lp, row_nr <= lp->rows ? row_nr : 0, prow, NULL, lp->epsmachine*DOUBLEROUND, 1.0);
    prod_xA(lp, SCAN_USERVARS+USE_NONBASICVARS, prow, NULL, XRESULT_FREE, lp->epsmachine, 1.0,
                                                prow, NULL);

    for(j = 1; j <= lp->rows + lp->columns; j++)
      if (!lp->is_basic[j])
        fprintf(stream, "%15.7f", prow[j] * (lp->is_lower[j] ? 1 : -1) *
                                            (row_nr <= lp->rows ? 1 : -1));
    fprintf(stream, "%15.7f", lp->rhs[row_nr <= lp->rows ? row_nr : 0] *
                              (REAL) ((row_nr <= lp->rows) || (is_maxim(lp)) ? 1 : -1));
    fprintf(stream, "\n");
  }
  FREE(prow);
  return(TRUE);
}
