
/* ----------------------------------------------------------------------------------
   Main library of routines for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Michel Berkelaar (to v3.2),
                   Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      (see below)

    Release notes:
    v5.0.0  1 January 2004      First integrated and repackaged version.
    v5.0.1  8 May 2004          Cumulative update since initial release;
                                overall functionality scope maintained.

   ---------------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------------- */
/* Main library of routines for lp_solve                                              */
/*----------------------------------------------------------------------------------- */
#include <time.h>
#include <signal.h>
/*#include <except.h>*/
#include <string.h>
#include <float.h>
#include <math.h>

#if LoadInverseLib == TRUE
  #ifdef WIN32
    #include <windows.h>
  #else
    #include <dlfcn.h>
  #endif
#endif


/* ---------------------------------------------------------------------------------- */
/* Include core and support modules via headers                                       */
/* ---------------------------------------------------------------------------------- */
#include "lp_lib.h"
#include "commonlib.h"
#include "lp_utils.h"
#include "lp_matrix.h"
#include "lp_SOS.h"
#include "lp_Hash.h"
#include "lp_MPS.h"
#include "lp_wlp.h"
#include "lp_presolve.h"
#include "lp_scale.h"
#include "lp_simplex.h"
#include "lp_mipbb.h"
#include "lp_report.h"

#if INVERSE_ACTIVE==INVERSE_LUMOD
  #include "lp_LUMOD.h"
#elif INVERSE_ACTIVE==INVERSE_LUSOL
  #include "lp_LUSOL.h"
#elif INVERSE_ACTIVE==INVERSE_GLPKLU
  #include "lp_glpkLU.h"
#elif INVERSE_ACTIVE==INVERSE_ETA
  #include "lp_etaPFI.h"
#elif INVERSE_ACTIVE==INVERSE_LEGACY
  #include "lp_etaPFI.h"
#endif

#ifdef __BORLANDC__
  #pragma hdrstop
  #pragma package(smart_init)
#endif

/* ---------------------------------------------------------------------------------- */
/* Include selected basis inverse routines and price norm scalars                     */
/* ---------------------------------------------------------------------------------- */
#ifdef UseLegacyOrdering
/*  #include "lp_noMDO.cpp" */
#else
  #include "lp_MDO.h"
#endif

#include "lp_price.h"
#include "lp_pricePSE.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/* ---------------------------------------------------------------------------------- */
/* Define some globals                                                                */
/* ---------------------------------------------------------------------------------- */
int callcount = 0;

/* Return lp_solve version information */
void __WINAPI lp_solve_version(int *majorversion, int *minorversion, int *release, int *build)
{
  if(majorversion != NULL)
    (*majorversion) = MAJORVERSION;
  if(minorversion != NULL)
    (*minorversion) = MINORVERSION;
  if(release != NULL)
    (*release) = RELEASE;
  if(build != NULL)
    (*build) = BUILD;
}


/* ---------------------------------------------------------------------------------- */
/* Various interaction elements                                                       */
/* ---------------------------------------------------------------------------------- */

MYBOOL __WINAPI userabort(lprec *lp, int message)
{
  int  spx_save;
  MYBOOL abort;
  spx_save = lp->spx_status;
  lp->spx_status = RUNNING;
  if(yieldformessages(lp) != 0) {
    lp->spx_status = USERABORT;
    if(lp->bb_level > 0)
      lp->bb_break = TRUE;
  }
  if((message > 0) && (lp->usermessage != NULL) && (lp->msgmask & message))
    lp->usermessage(lp, lp->msghandle, message);
  abort = (MYBOOL) (lp->spx_status != RUNNING);
  if(!abort)
    lp->spx_status = spx_save;
  return( abort );
}

STATIC int yieldformessages(lprec *lp)
{
  double currenttime = timeNow();

  if((lp->sectimeout > 0) && ((currenttime-lp->timestart)-(REAL)lp->sectimeout>0))
    lp->spx_status = TIMEOUT;

  if(lp->ctrlc != NULL) {
    int retcode = lp->ctrlc(lp, lp->ctrlchandle);
    /* Check for command to restart the B&B */
    if((retcode == ACTION_RESTART) && (lp->bb_level > 1)) {
      lp->bb_break = AUTOMATIC;
      retcode = 0;
    }
    return(retcode);
  }
  else
    return(0);
}

void __WINAPI set_outputstream(lprec *lp, FILE *stream)
{
  if((lp->outstream != NULL) && (lp->outstream != stdout))
    fclose(lp->outstream);
  if(stream == NULL) {
    lp->outstream = stdout;
  }
  else
    lp->outstream = stream;
}

MYBOOL __WINAPI set_outputfile(lprec *lp, char *filename)
{
  MYBOOL ok;
  FILE   *output = stdout;

  ok = (MYBOOL) ((filename == NULL) || ((output = fopen(filename,"w")) != NULL));
  if(ok)
    set_outputstream(lp, output);
  return(ok);
}

REAL __WINAPI time_elapsed(lprec *lp)
{
  if(lp->timeend > 0)
    return(lp->timeend - lp->timestart);
  else
    return(timeNow() - lp->timestart);
}

void __WINAPI put_abortfunc(lprec *lp, ctrlcfunc newctrlc, void *ctrlchandle)
{
  lp->ctrlc = newctrlc;
  lp->ctrlchandle = ctrlchandle;
}
void __WINAPI put_logfunc(lprec *lp, logfunc newlog, void *loghandle)
{
  lp->writelog = newlog;
  lp->loghandle = loghandle;
}
void __WINAPI put_msgfunc(lprec *lp, msgfunc newmsg, void *msghandle, int mask)
{
  lp->usermessage = newmsg;
  lp->msgmask = mask;
  lp->msghandle = msghandle;
}


/* ---------------------------------------------------------------------------------- */
/* DLL exported function                                                              */
/* ---------------------------------------------------------------------------------- */
lprec * __WINAPI read_MPS(char *filename, int verbose)
{
  return(MPS_readfile(filename, MPSFIXED, verbose));
}
lprec * __WINAPI read_mps(FILE *filename, int verbose)
{
  return(MPS_readhandle(filename, MPSFIXED, verbose));
}
lprec * __WINAPI read_freeMPS(char *filename, int verbose)
{
  return(MPS_readfile(filename, MPSFREE, verbose));
}
lprec * __WINAPI read_freemps(FILE *filename, int verbose)
{
  return(MPS_readhandle(filename, MPSFREE, verbose));
}
MYBOOL __WINAPI write_mps(lprec *lp, char *filename)
{
  return(MPS_writefile(lp, MPSFIXED, filename));
}
MYBOOL write_MPS(lprec *lp, FILE *output)
{
  return(MPS_writehandle(lp, MPSFIXED, output));
}

MYBOOL __WINAPI write_freemps(lprec *lp, char *filename)
{
  return(MPS_writefile(lp, MPSFREE, filename));
}
MYBOOL write_freeMPS(lprec *lp, FILE *output)
{
  return(MPS_writehandle(lp, MPSFREE, output));
}

MYBOOL __WINAPI write_lp(lprec *lp, char *filename)
{
  return(LP_writefile(lp, filename));
}
MYBOOL write_LP(lprec *lp, FILE *output)
{
  return(LP_writehandle(lp, output));
}
#ifndef PARSER_LP
lprec* __WINAPI read_lp(FILE *filename, int verbose, char *lp_name)
{
  return(NULL);
}
lprec* __WINAPI read_LP(char *filename, int verbose, char *lp_name)
{
  return(NULL);
}
#endif

#ifndef PARSER_CPLEX
lprec* __WINAPI read_LPT(char *filename, int verbose, char *lp_name)
{
  return(NULL);
}
lprec* __WINAPI read_lpt(FILE *filename, int verbose, char *lp_name)
{
  return(NULL);
}
MYBOOL __WINAPI write_lpt(lprec *lp, char *output)
{
  return(TRUE);
}
MYBOOL write_LPT(lprec *lp, FILE *output)
{
  return(TRUE);
}
#endif

void __WINAPI unscale(lprec *lp)
{
  undoscale(lp);
}
int __WINAPI lp_solve_solve(lprec *lp)
{
  if(has_BFP(lp)) {
    lp->solvecount++;
    if(is_add_rowmode(lp))
      set_add_rowmode(lp, FALSE);
    return(lin_solve(lp));
  }
  else
    return( NOBFP );
}
void __WINAPI lp_solve_print_lp(lprec *lp)
{
  REPORT_lp(lp);
}
void __WINAPI print_tableau(lprec *lp)
{
  REPORT_tableau(lp);
}
void __WINAPI print_objective(lprec *lp)
{
  REPORT_objective(lp);
}
void __WINAPI print_solution(lprec *lp, int columns)
{
  REPORT_solution(lp, columns);
}
void __WINAPI print_constraints(lprec *lp, int columns)
{
  REPORT_constraints(lp, columns);
}
void __WINAPI print_duals(lprec *lp)
{
  REPORT_duals(lp);
}
void __WINAPI print_scales(lprec *lp)
{
  REPORT_scales(lp);
}
MYBOOL __WINAPI print_debugdump(lprec *lp, char *filename)
{
  return(REPORT_debugdump(lp, filename, (MYBOOL) (get_total_iter(lp) > 0)));
}
void __WINAPI print_str(lprec *lp, char *str)
{
  report(lp, lp->verbose, "%s", str);
}



/* ---------------------------------------------------------------------------------- */
/* Parameter setting and retrieval functions                                          */
/* ---------------------------------------------------------------------------------- */

void __WINAPI set_timeout(lprec *lp, long sectimeout)
{
  lp->sectimeout = sectimeout;
}

long __WINAPI get_timeout(lprec *lp)
{
  return(lp->sectimeout);
}

void __WINAPI set_verbose(lprec *lp, int verbose)
{
  lp->verbose = verbose;
}

int __WINAPI get_verbose(lprec *lp)
{
  return(lp->verbose);
}

void __WINAPI set_print_sol(lprec *lp, int print_sol)
{
  lp->print_sol = print_sol;
}

int __WINAPI get_print_sol(lprec *lp)
{
  return(lp->print_sol);
}

void __WINAPI set_debug(lprec *lp, MYBOOL debug)
{
  lp->bb_trace = debug;
}

MYBOOL __WINAPI is_debug(lprec *lp)
{
  return(lp->bb_trace);
}

void __WINAPI set_trace(lprec *lp, MYBOOL trace)
{
  lp->spx_trace = trace;
}

MYBOOL __WINAPI is_trace(lprec *lp)
{
  return(lp->spx_trace);
}

void __WINAPI set_anti_degen(lprec *lp, int anti_degen)
{
  lp->anti_degen = anti_degen;
}

int __WINAPI get_anti_degen(lprec *lp)
{
  return(lp->anti_degen);
}

MYBOOL __WINAPI is_anti_degen(lprec *lp, int testmask)
{
  return((MYBOOL) ((lp->anti_degen == testmask) || ((lp->anti_degen & testmask) != 0)));
}

void __WINAPI set_presolve(lprec *lp, int do_presolve)
{
  lp->do_presolve = do_presolve;
}

int __WINAPI get_presolve(lprec *lp)
{
  return(lp->do_presolve);
}

MYBOOL __WINAPI is_presolve(lprec *lp, int testmask)
{
  return((MYBOOL) ((lp->do_presolve == testmask) || ((lp->do_presolve & testmask) != 0)));
}

void __WINAPI set_maxpivot(lprec *lp, int maxpivot)
{
  lp->max_pivots = maxpivot;
}

int __WINAPI get_maxpivot(lprec *lp)
{
  return( lp->bfp_pivotmax(lp) );
}

void __WINAPI set_bb_rule(lprec *lp, int bb_rule)
{
  lp->bb_rule = bb_rule;
}

int __WINAPI get_bb_rule(lprec *lp)
{
  return(lp->bb_rule);
}

MYBOOL is_bb_rule(lprec *lp, int bb_rule)
{
  return( (MYBOOL) ((lp->bb_rule & NODE_STRATEGYMASK) == bb_rule) );
}

MYBOOL is_bb_mode(lprec *lp, int bb_mask)
{
  return( (MYBOOL) ((lp->bb_rule & bb_mask) > 0) );
}

STATIC MYBOOL is_bb_action(lprec *lp, int testmask)
{
  return((MYBOOL) ((lp->bb_action & testmask) != 0));
}

void __WINAPI set_bb_depthlimit(lprec *lp, int bb_maxlevel)
{
  lp->bb_limitlevel = bb_maxlevel;
}

int __WINAPI get_bb_depthlimit(lprec *lp)
{
  return(lp->bb_limitlevel);
}

void __WINAPI set_obj_bound(lprec *lp, REAL bb_heuristicOF)
{
  lp->bb_heuristicOF = bb_heuristicOF;
}

REAL __WINAPI get_obj_bound(lprec *lp)
{
  return(lp->bb_heuristicOF);
}

void __WINAPI set_mip_gap(lprec *lp, MYBOOL absolute, REAL mip_gap)
{
  if(absolute)
    lp->mip_absgap = mip_gap;
  else
    lp->mip_relgap = mip_gap;
}

REAL __WINAPI get_mip_gap(lprec *lp, MYBOOL absolute)
{
  if(absolute)
    return(lp->mip_absgap);
  else
    return(lp->mip_relgap);
}

MYBOOL __WINAPI set_var_branch(lprec *lp, int column, int branch_mode)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "set_var_branch: Column %d out of range\n", column);
    return( FALSE );
  }
#endif

  if(lp->bb_varbranch == NULL) {
    int i;
    if(branch_mode == BRANCH_DEFAULT)
      return( TRUE );
    allocMYBOOL(lp, &lp->bb_varbranch, lp->columns_alloc, FALSE);
    for(i = 0; i < lp->columns; i++)
      lp->bb_varbranch[i] = BRANCH_DEFAULT;
  }
  lp->bb_varbranch[column-1] = (MYBOOL) branch_mode;
  return( TRUE );
}

int __WINAPI get_var_branch(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "get_var_branch: Column %d out of range\n", column);
    return(lp->bb_floorfirst);
  }
#endif

  if(lp->bb_varbranch == NULL)
    return(lp->bb_floorfirst);
  if(lp->bb_varbranch[column-1] == BRANCH_DEFAULT)
    return(lp->bb_floorfirst);
  else
    return(lp->bb_varbranch[column-1]);
}

static void set_infiniteex(lprec *lp, REAL infinite, MYBOOL init)
{
  int i;

  infinite = fabs(infinite);
  if((init) || (lp->bb_heuristicOF == lp->infinite))
    lp->bb_heuristicOF = infinite;
  if((init) || (lp->break_at_value == -lp->infinite))
    lp->break_at_value = -infinite;
  for(i = 0; i <= lp->sum; i++) {
    if((!init) && (lp->orig_lowbo[i] == lp->infinite))
      lp->orig_lowbo[i] = infinite;
    if((init) || (lp->orig_upbo[i] == lp->infinite))
      lp->orig_upbo[i] = infinite;
  }
  lp->obj_mincost = infinite;
  lp->infinite = infinite;
}


void __WINAPI set_infinite(lprec *lp, REAL infinite)
{
  set_infiniteex(lp, infinite, FALSE);
}

REAL __WINAPI get_infinite(lprec *lp)
{
  return(lp->infinite);
}

void __WINAPI set_epsilon(lprec *lp, REAL epsilon)
{
  lp->epspivot = epsilon;
}

REAL __WINAPI get_epsilon(lprec *lp)
{
  return(lp->epspivot);
}

void __WINAPI set_epsb(lprec *lp, REAL epsb)
{
  lp->epsprimal = epsb;
}

REAL __WINAPI get_epsb(lprec *lp)
{
  return(lp->epsprimal);
}

void __WINAPI set_epsd(lprec *lp, REAL epsd)
{
  lp->epsdual = epsd;
}

REAL __WINAPI get_epsd(lprec *lp)
{
  return(lp->epsdual);
}

void __WINAPI set_epsel(lprec *lp, REAL epsel)
{
  lp->epsvalue = epsel;
}

REAL __WINAPI get_epsel(lprec *lp)
{
  return(lp->epsvalue);
}

void __WINAPI set_scaling(lprec *lp, int scalemode)
{
  lp->scalemode = scalemode;
}

int __WINAPI get_scaling(lprec *lp)
{
  return(lp->scalemode);
}

MYBOOL __WINAPI is_scalemode(lprec *lp, int testmask)
{
  return((MYBOOL) ((lp->scalemode & testmask) != 0));
}

MYBOOL __WINAPI is_scaletype(lprec *lp, int scaletype)
{
  int testtype;

  testtype = lp->scalemode & SCALE_MAXTYPE;
  return((MYBOOL) (scaletype == testtype));
}

void __WINAPI set_scalelimit(lprec *lp, REAL scalelimit)
/* Set the relative scaling convergence criterion for the active scaling mode;
   the integer part specifies the maximum number of iterations (default = 5). */
{
  lp->scalelimit = fabs(scalelimit);
}

REAL __WINAPI get_scalelimit(lprec *lp)
{
  return(lp->scalelimit);
}

MYBOOL __WINAPI is_integerscaling(lprec *lp)
{
  return(is_scalemode(lp, SCALE_INTEGERS));
}

void __WINAPI set_improve(lprec *lp, int improve)
{
  lp->improve = improve;
}

int __WINAPI get_improve(lprec *lp)
{
  return(lp->improve);
}

void __WINAPI set_lag_trace(lprec *lp, MYBOOL lag_trace)
{
  lp->lag_trace = lag_trace;
}

MYBOOL __WINAPI is_lag_trace(lprec *lp)
{
  return(lp->lag_trace);
}

void __WINAPI set_pivoting(lprec *lp, int pivoting)
{
  lp->piv_strategy = pivoting;
}

int __WINAPI get_pivoting(lprec *lp)
{
  return( lp->piv_strategy );
}

int get_piv_rule(lprec *lp)
{
  int piv = lp->piv_strategy;
  piv = my_mod(piv, PRICE_PRIMALFALLBACK);
  piv = my_mod(piv, PRICE_MULTIPLE);
  piv = my_mod(piv, PRICE_ADAPTIVE);
  piv = my_mod(piv, PRICE_HYBRID);
#ifdef EnableRandomizedPricing
  piv = my_mod(piv, PRICE_RANDOMIZE);
#endif
  return(piv);
}

MYBOOL __WINAPI is_piv_rule(lprec *lp, int rule)
{
  return( (MYBOOL) (get_piv_rule(lp) == rule) );
}

MYBOOL __WINAPI is_piv_mode(lprec *lp, int testmask)
{
  return((MYBOOL) (((testmask & PRICE_STRATEGYMASK) != 0) &&
                   ((lp->piv_strategy & testmask) != 0)));
}

void __WINAPI set_break_at_first(lprec *lp, MYBOOL break_at_first)
{
  lp->break_at_first = break_at_first;
}

MYBOOL __WINAPI is_break_at_first(lprec *lp)
{
  return(lp->break_at_first);
}

void __WINAPI set_bb_floorfirst(lprec *lp, int bb_floorfirst)
{
  lp->bb_floorfirst = (MYBOOL) bb_floorfirst;
}

int __WINAPI get_bb_floorfirst(lprec *lp)
{
  return(lp->bb_floorfirst);
}

void __WINAPI set_break_at_value(lprec *lp, REAL break_at_value)
{
  lp->break_at_value = break_at_value;
}

REAL __WINAPI get_break_at_value(lprec *lp)
{
  return(lp->break_at_value);
}

void __WINAPI set_negrange(lprec *lp, REAL negrange)
{
  if(negrange <= 0)
    lp->negrange = negrange;
  else
    lp->negrange = 0.0;
}

REAL __WINAPI get_negrange(lprec *lp)
{
  return(lp->negrange);
}

void __WINAPI set_splitnegvars(lprec *lp, int splitnegvars)
{
  lp->splitnegvars = splitnegvars;
}

int __WINAPI get_splitnegvars(lprec *lp)
{
  return(lp->splitnegvars);
}

void __WINAPI set_epsperturb(lprec *lp, REAL epsperturb)
{
  lp->epsperturb = epsperturb;
}

REAL __WINAPI get_epsperturb(lprec *lp)
{
  return(lp->epsperturb);
}

void __WINAPI set_epspivot(lprec *lp, REAL epspivot)
{
  lp->epspivot = epspivot;
}

REAL __WINAPI get_epspivot(lprec *lp)
{
  return(lp->epspivot);
}

int __WINAPI get_max_level(lprec *lp)
{
  return(lp->bb_maxlevel);
}

int __WINAPI get_total_nodes(lprec *lp)
{
  return(lp->bb_totalnodes);
}

int __WINAPI get_total_iter(lprec *lp)
{
  return(lp->total_iter + lp->current_iter);
}

REAL __WINAPI get_objective(lprec *lp)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_objective: Not a valid basis");
    return(0.0);
  }

  return(*(lp->best_solution));
}

int __WINAPI get_nonzeros(lprec *lp)
{
  return(mat_nonzeros(lp->matA));
}

MYBOOL __WINAPI lp_solve_set_mat(lprec *lp, int row, int column, REAL value)
{
#ifdef Paranoia
  if(row < 0 || row > lp->rows) {
    report(lp, IMPORTANT, "lp_solve_set_mat: Row %d out of range", row);
    return(0);
  }
  if(column < 1 || column > lp->columns) {
    report(lp, IMPORTANT, "lp_solve_set_mat: Column %d out of range", column);
    return(0);
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "lp_solve_set_mat: Cannot set a single matrix value while in row entry mode\n");
    return(FALSE);
  }
#endif
  return( mat_setvalue(lp->matA, row, column, value, lp->scaling_used) );
}

REAL __WINAPI get_working_objective(lprec *lp)
{
  REAL value = 0.0;

  if(!lp->basis_valid)
    report(lp, CRITICAL, "get_working_objective: Not a valid basis");
  else if((lp->spx_status == RUNNING) && (lp->solutioncount == 0))
    value = my_chsign(is_maxim(lp), *(lp->rhs));
  else
    value = *(lp->solution);

  return(value);
}

REAL __WINAPI get_var_primalresult(lprec *lp, int index)
{
  if((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE)
    return(lp->full_solution[index]);
  else
    return(lp->best_solution[index]);
}
REAL __WINAPI get_var_dualresult(lprec *lp, int index)
{
  REAL *duals;

  if(index == 0)
    return( lp->best_solution[0] );
  else if(lp->duals == NULL)
    return( 0.0 );
  else if((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE) {
    int ix;

    ix = lp->orig_to_var[index];
    if(index > lp->orig_rows)
      ix += lp->rows;
    if(ix <= 0)
      return( 0 );
    index = ix;
  }
  if(!get_ptr_sensitivity_rhs(lp, &duals, NULL, NULL))
    return( 0.0 );
  else
    return( duals[index - 1] );
}

MYBOOL __WINAPI get_variables(lprec *lp, REAL *var)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_variables: Not a valid basis");
    return(FALSE);
  }

  MEMCOPY(var, lp->best_solution + (1 + lp->rows), lp->columns);
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_variables(lprec *lp, REAL **var)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_ptr_variables: Not a valid basis");
    return(FALSE);
  }

  if(var != NULL)
   *var = lp->best_solution + (1 + lp->rows);
  return(TRUE);
}

MYBOOL __WINAPI get_constraints(lprec *lp, REAL *constr)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_constraints: Not a valid basis");
    return(FALSE);
  }

  MEMCOPY(constr, lp->best_solution + 1, lp->rows);
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_constraints(lprec *lp, REAL **constr)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_ptr_constraints: Not a valid basis");
    return(FALSE);
  }

  if(constr != NULL)
   *constr = lp->best_solution + 1;
  return(TRUE);
}

MYBOOL __WINAPI get_sensitivity_rhs(lprec *lp, REAL *duals, REAL *dualsfrom, REAL *dualstill)
{
  REAL *duals0, *dualsfrom0, *dualstill0;

  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_sensitivity_rhs: Not a valid basis");
    return(FALSE);
  }

  if(!get_ptr_sensitivity_rhs(lp,
                              (duals != NULL) ? &duals0 : NULL,
			      (dualsfrom != NULL) ? &dualsfrom0 : NULL,
			      (dualstill != NULL) ? &dualstill0 : NULL))
    return(FALSE);

  if(duals != NULL)
    MEMCOPY(duals, duals0, lp->sum);
  if(dualsfrom != NULL)
    MEMCOPY(dualsfrom, dualsfrom0, lp->sum);
  if(dualstill != NULL)
    MEMCOPY(dualstill, dualstill0, lp->sum);
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_sensitivity_rhs(lprec *lp, REAL **duals, REAL **dualsfrom, REAL **dualstill)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_ptr_sensitivity_rhs: Not a valid basis");
    return(FALSE);
  }

  if(duals != NULL) {
    if(lp->duals == NULL) {
      if((MIP_count(lp) > 0) && (lp->bb_totalnodes > 0)) {
        report(lp, CRITICAL, "get_ptr_sensitivity_rhs: Sensitivity unknown");
        return(FALSE);
      }
      calculate_duals(lp);
      if(lp->duals == NULL)
        return(FALSE);
    }
    *duals = lp->duals + 1;
  }

  if((dualsfrom != NULL) || (dualstill != NULL)) {
    if((lp->dualsfrom == NULL) || (lp->dualstill == NULL)) {
      if((MIP_count(lp) > 0) && (lp->bb_totalnodes > 0)) {
        report(lp, CRITICAL, "get_ptr_sensitivity_rhs: Sensitivity unknown");
        return(FALSE);
      }
      calculate_sensitivity_duals(lp);
      if((lp->dualsfrom == NULL) || (lp->dualstill == NULL))
        return(FALSE);
    }
    if(dualsfrom != NULL)
      *dualsfrom = lp->dualsfrom + 1;
    if(dualstill != NULL)
      *dualstill = lp->dualstill + 1;
  }
  return(TRUE);
}

MYBOOL __WINAPI get_sensitivity_obj(lprec *lp, REAL *objfrom, REAL *objtill)
{
  REAL *objfrom0, *objtill0;

  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_sensitivity_obj: Not a valid basis");
    return(FALSE);
  }

  if(!get_ptr_sensitivity_obj(lp, (objfrom != NULL) ? &objfrom0 : NULL, (objtill != NULL) ? &objtill0 : NULL))
    return(FALSE);

  if(objfrom != NULL)
    MEMCOPY(objfrom, objfrom0, lp->columns);
  if(objtill != NULL)
    MEMCOPY(objtill, objtill0, lp->columns);
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_sensitivity_obj(lprec *lp, REAL **objfrom, REAL **objtill)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_ptr_sensitivity_obj: Not a valid basis");
    return(FALSE);
  }

  if((objfrom != NULL) || (objtill != NULL)) {
    if((lp->objfrom == NULL) || (lp->objtill == NULL)) {
      if((MIP_count(lp) > 0) && (lp->bb_totalnodes > 0)) {
        report(lp, CRITICAL, "get_ptr_sensitivity_rhs: sensitivity unknown");
        return(FALSE);
      }
      calculate_sensitivity_obj(lp);
      if((lp->objfrom == NULL) || (lp->objtill == NULL))
        return(FALSE);
    }
    if(objfrom != NULL)
      *objfrom = lp->objfrom + 1;
    if(objtill != NULL)
      *objtill = lp->objtill + 1;
  }

  return(TRUE);
}

void __WINAPI set_solutionlimit(lprec *lp, int limit)
{
  lp->solutionlimit = limit;
}
int __WINAPI get_solutionlimit(lprec *lp)
{
  return(lp->solutionlimit);
}
int __WINAPI get_solutioncount(lprec *lp)
{
  return(lp->solutioncount);
}

int __WINAPI get_Nrows(lprec *lp)
{
  return(lp->rows);
}

int __WINAPI get_Norig_rows(lprec *lp)
{
  if(lp->varmap_locked)
    return(lp->orig_rows);
  else
    return(lp->rows);
}

int __WINAPI get_Lrows(lprec *lp)
{
  if(lp->matL == NULL)
    return( 0 );
  else
    return( lp->matL->rows );
}

int __WINAPI get_Ncolumns(lprec *lp)
{
  return(lp->columns);
}

int __WINAPI get_Norig_columns(lprec *lp)
{
  if(lp->varmap_locked)
    return(lp->orig_columns);
  else
    return(lp->columns);
}


/* ---------------------------------------------------------------------------------- */
/* Core routines for lp_solve                                                         */
/* ---------------------------------------------------------------------------------- */
const char * __WINAPI get_statustext(lprec *lp, int statuscode)
{
  if (statuscode == NOBFP)             return("No basis factorization package");
  else if (statuscode == DATAIGNORED)  return("Invalid input data provided");
  else if (statuscode == NOMEMORY)     return("Not enough memory available");
  else if (statuscode == NOTRUN)       return("Model has not been optimized");
  else if (statuscode == OPTIMAL)      return("OPTIMAL solution");
  else if (statuscode == SUBOPTIMAL)   return("SUB-OPTIMAL solution");
  else if (statuscode == INFEASIBLE)   return("Model is primal INFEASIBLE");
  else if (statuscode == UNBOUNDED)    return("Model is primal UNBOUNDED");
  else if (statuscode == RUNNING)      return("lp_solve is currently running");
  else if (statuscode == NUMFAILURE)   return("NUMERIC FAILURE encountered");
  else if (statuscode == DEGENERATE)   return("DEGENERATE situation");
  else if (statuscode == USERABORT)    return("User-requested termination");
  else if (statuscode == TIMEOUT)      return("Termination due to timeout");
  else if (statuscode == FUTURESTATUS) return("(Future)");
  else if (statuscode == PROCFAIL)     return("B&B routine failed");
  else if (statuscode == PROCBREAK)    return("B&B routine terminated");
  else if (statuscode == FEASFOUND)    return("Feasible B&B solution found");
  else if (statuscode == NOFEASFOUND)  return("No feasible B&B solution found");
  else                                 return("Undefined internal error");
}

lprec * __WINAPI lp_solve_make_lp(int rows, int columns)
{
  lprec *lp;
  int   sum;

  callcount++;
  if(rows < 0 || columns < 0)
    return(NULL);

  /* Initialize randomization engine */
  srand((unsigned) time( NULL ));

  lp = (lprec*) calloc(1, sizeof(*lp));
  if(!lp)
    return(NULL);

  set_lp_name(lp, "Unnamed");
  lp->names_used = FALSE;

  set_callbacks(lp);
  set_BFP(lp, NULL);
  set_XLI(lp, NULL);

  set_outputstream(lp, NULL);  /* Set to default output stream */
  lp->verbose = NORMAL;
  lp->print_sol = FALSE;       /* Can be FALSE, TRUE, AUTOMATIC (only non-zeros printed) */
  lp->spx_trace = FALSE;
  lp->lag_trace = FALSE;
  lp->bb_trace = FALSE;

  lp->source_is_file = FALSE;
  lp->model_is_pure  = TRUE;
  lp->model_is_valid = FALSE;
  lp->spx_status = NOTRUN;
  lp->lag_status = NOTRUN;

  lp->epsmachine = DEF_EPSMACHINE;
  lp->epsvalue = DEF_EPSVALUE;
  lp->epsprimal = DEF_EPSPRIMAL;
  lp->epsdual = DEF_EPSDUAL;
  lp->epsperturb = DEF_PERTURB;
  lp->epspivot = DEF_EPSPIVOT;
  lp->epsint = DEF_EPSINT;
  lp->mip_absgap = DEF_MIP_GAP;
  lp->mip_relgap = DEF_MIP_GAP;
  lp->lag_accept = DEF_LAGACCEPT;
  lp->wasPreprocessed = FALSE;
  lp->wasPresolved = FALSE;

  lp->rows_alloc = 0;
  lp->columns_alloc = 0;
  lp->sum_alloc = 0;

  lp->equalities = 0;
  lp->fixedvars = 0;
  lp->int_count = 0;
  lp->sc_count = 0;

  lp->bb_varactive = NULL;
  lp->bb_floorfirst = BRANCH_FLOOR;
  lp->bb_varbranch = NULL;
  lp->bb_rule = NODE_FIRSTSELECT;
  lp->bb_action = ACTION_NONE;
  lp->bb_limitlevel = DEF_BB_LIMITLEVEL;
  lp->var_priority = NULL;

  lp->bigM = 0.0;
  lp->bb_deltaOF = 0.0;

  lp->sos_ints = 0;
  lp->sos_vars = 0;
  lp->sos_priority = NULL;

  sum = rows + columns;
  lp->rows = rows;
  lp->columns = columns;
  lp->sum = sum;
  varmap_clear(lp);

  lp->matA = mat_create(lp, rows, columns, lp->epsvalue);
  lp->matL = NULL;
  lp->invB = NULL;
  lp->duals = NULL;
  lp->dualsfrom = NULL;
  lp->dualstill = NULL;
  lp->objfrom = NULL;
  lp->objtill = NULL;

  inc_col_space(lp, columns+1);
  inc_row_space(lp, rows+1);

  /* Avoid bound-checker uninitialized variable error */
  lp->orig_lowbo[0] = 0;

  lp->max_pivots = 0;

  lp->crashmode = CRASH_NOTHING;
  lp->rootbounds = NULL;
/*  lp->bb_bounds = NULL; */
  lp->bb_basis = NULL;

  lp->basis_valid = FALSE;
  lp->Extrap = 0;
  lp->Extrad = 0.0;
  lp->current_iter = 0;
  lp->total_iter = 0;
  lp->current_bswap = 0;
  lp->total_bswap = 0;
  allocINT(lp, &lp->rejectpivot, DEF_MAXPIVOTRETRY+1, TRUE);

  lp_solve_set_minim(lp);
  set_infiniteex(lp, DEF_INFINITE, TRUE);

/*  lp->piv_strategy = PRICER_DANTZIG + PRICE_METHODDEFAULT + PRICE_ADAPTIVE; */
  lp->piv_strategy = PRICER_DEVEX + PRICE_METHODDEFAULT + PRICE_ADAPTIVE;
/*  lp->piv_strategy = PRICER_STEEPESTEDGE + PRICE_METHODDEFAULT + PRICE_PRIMALFALLBACK + PRICE_ADAPTIVE; */
/*  lp->piv_strategy = PRICER_STEEPESTEDGE + PRICE_METHODDEFAULT + PRICE_ADAPTIVE; */
/*  lp->piv_strategy = PRICER_FIRSTINDEX + PRICE_METHODDEFAULT; */
#ifdef EnableRandomizedPricing
  lp->piv_strategy = lp->piv_strategy + PRICE_RANDOMIZE;
#endif

  lp->edgeVector = NULL;
  initPricer(lp);

  lp->simplex_strategy = SIMPLEX_DUAL_PRIMAL;
  lp->simplex_mode = SIMPLEX_DYNAMIC;
  lp->tighten_on_set = FALSE;
  lp->negrange = DEF_NEGRANGE;
  lp->splitnegvars = DEF_SPLITNEGVARS;
  lp->do_presolve = PRESOLVE_NONE;
  lp->improve = IMPROVE_NONE;
  lp->anti_degen = ANTIDEGEN_NONE;

  lp->scalemode = SCALE_NONE;
  lp->scalelimit = DEF_SCALINGLIMIT;
  lp->scaling_used = FALSE;
  lp->columns_scaled = FALSE;

  lp->solvecount = 0;

  lp->sectimeout = 0;
  lp->solutioncount = 0;
  lp->solutionlimit = 1;

  /* Call-back routines by KE */
  lp->ctrlc = NULL;
  lp->ctrlchandle = NULL;
  lp->writelog = NULL;
  lp->loghandle = NULL;
  lp->debuginfo = NULL;
  lp->usermessage = NULL;
  lp->msgmask = MSG_NONE;
  lp->msghandle = NULL;

  return(lp);
}

void __WINAPI free_lp(lprec **plp)
{
  lprec *lp;

  lp = *plp;
  if(lp != NULL)
    lp_solve_delete_lp(lp);
  *plp = NULL;
}

void __WINAPI lp_solve_delete_lp(lprec *lp)
{
  FREE(lp->lp_name);
  if(lp->names_used) {
    FREE(lp->row_name);
    FREE(lp->col_name);
    free_hash_table(lp->rowname_hashtab);
    free_hash_table(lp->colname_hashtab);
  }

  mat_free(&lp->matA);
  lp->bfp_free(lp);
#if LoadInverseLib == TRUE
  if(lp->hBFP != NULL)
    set_BFP(lp, NULL);
#endif
#if LoadLanguageLib == TRUE
  if(lp->hXLI != NULL)
    set_XLI(lp, NULL);
#endif

  FREE(lp->orig_rh);
  FREE(lp->rhs);
  FREE(lp->must_be_int);
  set_var_weights(lp, NULL);
  if(lp->bb_varbranch != NULL)
    FREE(lp->bb_varbranch);
  FREE(lp->var_is_sc);
  if(lp->var_is_free != NULL)
    FREE(lp->var_is_free);
  FREE(lp->orig_upbo);
  FREE(lp->orig_lowbo);
  FREE(lp->upbo);
  FREE(lp->lowbo);
  FREE(lp->var_basic);
  FREE(lp->is_basic);
  FREE(lp->is_lower);
  if(lp->bb_PseudoCost != NULL) {
    report(lp, SEVERE, "lp_solve_delete_lp: The B&B pseudo-cost array was not cleared on delete\n");
    free_PseudoCost(lp);
  }
  if(lp->bb_bounds != NULL) {
    report(lp, SEVERE, "lp_solve_delete_lp: The stack of B&B levels was not empty on delete\n");
    unload_BB(lp);
  }
  if(lp->bb_basis != NULL) {
    report(lp, SEVERE, "lp_solve_delete_lp: The stack of saved bases was not empty on delete\n");
    unload_basis(lp, FALSE);
  }

  FREE(lp->rejectpivot);
  partial_freeBlocks(&(lp->rowblocks));
  partial_freeBlocks(&(lp->colblocks));
  FREE(lp->multivar);

  FREE(lp->solution);
  FREE(lp->best_solution);
  if(lp->full_solution != NULL)
    FREE(lp->full_solution);
  FREE(lp->var_to_orig);
  FREE(lp->orig_to_var);

  freePricer(lp);

  if(lp->duals != NULL)
    FREE(lp->duals);
  if(lp->dualsfrom != NULL)
    FREE(lp->dualsfrom);
  if(lp->dualstill != NULL)
    FREE(lp->dualstill);
  if(lp->objfrom != NULL)
    FREE(lp->objfrom);
  if(lp->objtill != NULL)
    FREE(lp->objtill);
  FREE(lp->row_type);

  if(lp->sos_vars > 0)
    FREE(lp->sos_priority);
  free_SOSgroup(&(lp->SOS));
  free_SOSgroup(&(lp->GUB));

  if(lp->scaling_used)
    FREE(lp->scalars);
  if(lp->matL != NULL) {
    FREE(lp->lag_rhs);
    FREE(lp->lambda);
    FREE(lp->lag_con_type);
    mat_free(&lp->matL);
  }

  free(lp);

}

/* Utility routine group for constraint and column deletion/insertion
   mapping in relation to the original set of constraints and columns */
STATIC void varmap_lock(lprec *lp)
{
  int i;

  for(i = 0; i <= lp->rows; i++) {
    lp->var_to_orig[i] = i;
    lp->orig_to_var[i] = i;
  }
  for(i = 1; i <= lp->columns; i++) {
    lp->var_to_orig[lp->rows + i] = i;
    lp->orig_to_var[lp->rows + i] = i;
  }
  lp->orig_columns = lp->columns;
  lp->orig_rows = lp->rows;
  lp->varmap_locked = TRUE;
}
STATIC void varmap_clear(lprec *lp)
{
  lp->orig_columns = 0;
  lp->orig_rows = 0;
  lp->varmap_locked = FALSE;
}
STATIC void varmap_add(lprec *lp, int base, int delta)
{
  int i, ii;

  /* Don't do anything if variables aren't locked yet */
  if(!lp->varmap_locked)
    return;

  /* Set new constraints/columns to have an "undefined" mapping to original
     constraints/columns (assumes that counters have NOT yet been updated) */
  for(i = lp->sum; i >= base; i--) {
    ii = i + delta;
    lp->var_to_orig[ii] = lp->var_to_orig[i];
  }
  ii = base;
  for(i = 0; i < delta; i++) {
    ii = base + i;
    lp->var_to_orig[ii] = 0;
  }
}

STATIC void varmap_delete(lprec *lp, int base, int delta)
{
  int i, ii, j;

  /* Set the model "dirty" if we are deleting row of constraint */
  lp->model_is_pure  = FALSE;

  /* Don't do anything if 1) variables aren't locked yet, or
     2) the constraint was added after the variables were locked */
  if(!lp->varmap_locked)
    return;

  /* We are deleting an original constraint/column;
     1) clear mapping of original to deleted
     2) shift the deleted variable to original mappings left
     3) decrement all subsequent original-to-current pointers
  */
  for(i = base; i < base-delta; i++) {
    ii = lp->var_to_orig[i];
    if(ii > 0)
      lp->orig_to_var[ii] = 0;
  }
  for(i = base; i <= lp->sum+delta; i++) {
    ii = i - delta;
    lp->var_to_orig[i] = lp->var_to_orig[ii];
  }

  j = lp->orig_rows;
  if(base <= lp->rows)
    i = 1;
  else {
    i = lp->orig_rows+1;
    j += lp->orig_columns;
  }
  ii = base-delta;
  for(; i <= j; i++) {
    if(lp->orig_to_var[i] >= ii)
      lp->orig_to_var[i] += delta;
  }

}

/* Utility group for shifting row and column data */
STATIC MYBOOL shift_rowcoldata(lprec *lp, int base, int delta, MYBOOL isrow)
/* Note: Assumes that "lp->sum" and "lp->rows" HAVE NOT been updated to the new counts */
{
  int  i, ii;
  REAL lodefault;

  /* Shift data right/down (insert), and set default values in positive delta-gap */
  if(delta > 0) {

    /* Shift the row/column data */
#if 0
    for(ii = lp->sum; ii >= base; ii--) {
      i = ii + delta;
      lp->upbo[i] = lp->upbo[ii];
      lp->orig_upbo[i] = lp->orig_upbo[ii];
      lp->lowbo[i] = lp->lowbo[ii];
      lp->orig_lowbo[i] = lp->orig_lowbo[ii];
      lp->solution[i] = lp->solution[ii];
      lp->best_solution[i] = lp->best_solution[ii];
      lp->is_lower[i] = lp->is_lower[ii];
    }
#else
    MEMMOVE(lp->upbo + base + delta, lp->upbo + base, lp->sum - base + 1);
    MEMMOVE(lp->orig_upbo + base + delta, lp->orig_upbo + base, lp->sum - base + 1);
    MEMMOVE(lp->lowbo + base + delta, lp->lowbo + base, lp->sum - base + 1);
    MEMMOVE(lp->orig_lowbo + base + delta, lp->orig_lowbo + base, lp->sum - base + 1);
    if(lp->model_is_valid) {
      MEMMOVE(lp->solution + base + delta, lp->solution + base, lp->sum - base + 1);
      MEMMOVE(lp->best_solution + base + delta, lp->best_solution + base, lp->sum - base + 1);
    }
    MEMMOVE(lp->is_lower + base + delta, lp->is_lower + base, lp->sum - base + 1);
#endif

    /* Deal with scalars; the vector can be NULL */
    if(lp->scalars != NULL) {
      for(ii = lp->sum; ii >= base; ii--) {            /*  ****  */
        i = ii + delta;
        lp->scalars[i] = lp->scalars[ii];
      }
      for(ii = base; ii < base + delta; ii++)
        lp->scalars[ii] = 1;
    }

    /* Set defaults */
    if(isrow)
      lodefault = -lp->infinite;
    else
      lodefault = 0;

    for(i = 0; i < delta; i++) {
      ii = base + i;
      lp->upbo[ii] = lp->infinite;
      lp->orig_upbo[ii] = lp->upbo[ii];
      lp->lowbo[ii] = lodefault;
      lp->orig_lowbo[ii] = lp->lowbo[ii];
      lp->is_lower[ii] = TRUE;
    }
  }

  /* Shift data left/up (delete) */
  else if(delta < 0) {

    /* First make sure we don't cross the sum count border */
    if(base-delta-1 > lp->sum)
      delta = base - lp->sum - 1;

    /* Shift the data*/
    for(i = base; i <= lp->sum + delta; i++) {
      ii = i - delta;
      lp->upbo[i] = lp->upbo[ii];
      lp->orig_upbo[i] = lp->orig_upbo[ii];
      lp->lowbo[i] = lp->lowbo[ii];
      lp->orig_lowbo[i] = lp->orig_lowbo[ii];
      lp->solution[i] = lp->solution[ii];
      lp->best_solution[i] = lp->best_solution[ii];
      lp->is_lower[i] = lp->is_lower[ii];
    }

    /* Deal with scalars */
    if(lp->scalars != NULL) {
      for(i = base; i <= lp->sum + delta; i++) {
        ii = i - delta;
        lp->scalars[i] = lp->scalars[ii];
      }
    }

  }

  lp->sum += delta;

  lp->matA->row_end_valid = FALSE;

  return(TRUE);
}

STATIC MYBOOL shift_basis(lprec *lp, int base, int delta, MYBOOL isrow)
/* Note: Assumes that "lp->sum" and "lp->rows" HAVE NOT been updated to the new counts */
{
  int i, ii;
  MYBOOL Ok = TRUE;

  /* Don't bother to shift the basis if it is not yet ready */
  if(!is_BasisReady(lp))
    return( Ok );

  /* Basis adjustments due to insertions (after actual row/column insertions) */
  if(delta > 0) {

    /* Determine if the basis becomes invalidated */
    if(isrow) {
      lp->basis_valid = FALSE;
      lp->doRebase = TRUE;   /* Force rebasing and reinversion */
      lp->doInvert = TRUE;
    }

    /* Shift and fix invalid basis references (increment higher order basic variable index) */
#if 0
    for(ii = lp->sum; ii >= base; ii--) {
      i = ii + delta;
      lp->is_basic[i] = lp->is_basic[ii];
    }
#else
    if(base <= lp->sum)
      MEMMOVE(lp->is_basic + base + delta, lp->is_basic + base, lp->sum - base + 1);
#endif

    /* Prevent CPU-expensive basis updating if this is the initial model creation */
    if(!lp->model_is_pure || (lp->solvecount > 0))
      for(i = 1; i <= lp->rows; i++) {
        ii = lp->var_basic[i];
        if(ii >= base)
          lp->var_basic[i] += delta;
      }

    /* Update the basis (shift and extend) */
    for(i = 0; i < delta; i++) {
      ii = base + i;
      lp->is_basic[ii] = isrow;
      if(isrow)
        lp->var_basic[lp->rows+1+i] = ii;
    }

  }
  /* Basis adjustments due to deletions (after actual row/column deletions) */
  else {
    int j,k;

    /* Fix invalid basis references (decrement high basic slack variable indexes),
       but reset the entire basis if a deleted variable is found in the basis */
    k = 0;
    for(i = 1; i <= lp->rows; i++) {
      ii = lp->var_basic[i];
      lp->is_basic[ii] = FALSE;
      if(ii >= base) {
       /* Skip to next basis variable if this one is to be deleted */
        if(ii < base-delta) {
          lp->doRebase = TRUE;
          continue;
        }
       /* Otherwise, update the index of the basic variable for deleted variables */
        ii += delta;
      }
      k++;
      lp->var_basic[k] = ii;
    }

    /* Set the new basis indicators */
    i = k;
    if(isrow)
      i = MIN(k, lp->rows+delta);
    for(; i > 0; i--) {
      j = lp->var_basic[i];
      lp->is_basic[j] = TRUE;
    }

    /* If a column was deleted from the basis then simply add back a non-basic
       slack variable; do two scans, if necessary to avoid adding equality slacks */
    if(!isrow && (k < lp->rows)) {
      int j;
      for(j = 0; j <= 1; j++)
      for(i = 1; (i <= lp->rows) && (k < lp->rows); i++)
        if(!lp->is_basic[i]) {
          if(!is_constr_type(lp, i, EQ) || (j == 1)) {
            k++;
            lp->var_basic[k] = i;
            lp->is_basic[i] = TRUE;
          }
        }
      k = 0;
    }

    /* We are left with "k" indexes; if no basis variable was deleted, k=rows and the
       inverse is still valid, if k+delta < 0 we do not have a valid
       basis and must create one (in most usage modes this should not happen,
       unless there is a bug) */
    if(k+delta < 0)
      Ok = FALSE;
    if(isrow || (k != lp->rows))
      lp->doInvert = TRUE;

  }
  return(Ok);

}

STATIC MYBOOL shift_rowdata(lprec *lp, int base, int delta)
/* Note: Assumes that "lp->rows" HAS NOT been updated to the new count */
{
  int i, ii;

  /* Shift sparse matrix row data */
  if(lp->matA->is_roworder)
    mat_shiftcols(lp->matA, base+1, delta);
  else
    mat_shiftrows(lp->matA, &base, delta);

  /* Shift data down (insert row), and set default values in positive delta-gap */
  if(delta > 0) {

    /* Shift row data */
    for(ii = lp->rows; ii >= base; ii--) {
      i = ii + delta;
      lp->orig_rh[i] = lp->orig_rh[ii];
      lp->rhs[i] = lp->rhs[ii];
      lp->row_type[i] = lp->row_type[ii];
    }

    /* Set defaults (actual basis set in separate procedure) */
    for(i = 0; i < delta; i++) {
      ii = base + i;
      lp->orig_rh[ii] = 0;
      lp->rhs[ii] = 0;
      lp->row_type[ii] = ROWTYPE_EMPTY;
    }
  }

  /* Shift data up (delete row) */
  else if(delta < 0) {

    /* First make sure we don't cross the row count border */
    if(base-delta-1 > lp->rows)
      delta = base - lp->rows - 1;

    /* Shift row data (don't shift basis indexes here; done in next step) */
    for(i = base; i <= lp->rows + delta; i++) {
      ii = i - delta;
      lp->orig_rh[i] = lp->orig_rh[ii];
      lp->rhs[i] = lp->rhs[ii];
      lp->row_type[i] = lp->row_type[ii];
    }
  }

  shift_basis(lp, base, delta, TRUE);
  shift_rowcoldata(lp, base, delta, TRUE);
  inc_rows(lp ,delta);

  return(TRUE);
}

STATIC MYBOOL shift_coldata(lprec *lp, int base, int delta)
/* Note: Assumes that "lp->columns" HAS NOT been updated to the new count */
{
  int i, ii;

  /* Shift A matrix data */
  if(lp->matA->is_roworder)
    mat_shiftrows(lp->matA, &base, delta);
  else
    mat_shiftcols(lp->matA, base, delta);

  /* Shift data right (insert), and set default values in positive delta-gap */
  if(delta > 0) {

    /* Fix invalid variable priority data */
    if((lp->var_priority != NULL) && (base <= lp->columns)) {
      for(i = 0; i < lp->columns; i++)
        if(lp->var_priority[i] >= base)
          lp->var_priority[i] += delta;
    }

    /* Fix invalid split variable data */
    if((lp->var_is_free != NULL) && (base <= lp->columns)) {
      for(i = 1; i <= lp->columns; i++)
        if(abs(lp->var_is_free[i]) >= base)
          lp->var_is_free[i] += my_chsign(lp->var_is_free[i] < 0, delta);
    }

    /* Shift column data right */
    for(ii = lp->columns; ii >= base; ii--) {         /*  ****  */
      i = ii + delta;
      lp->must_be_int[i] = lp->must_be_int[ii];
      lp->var_is_sc[i] = lp->var_is_sc[ii];
      if(lp->objfrom != NULL)
        lp->objfrom[i] = lp->objfrom[ii];
      if(lp->objtill != NULL)
        lp->objtill[i] = lp->objtill[ii];
      if(lp->var_priority != NULL)
        lp->var_priority[i-1] = lp->var_priority[ii-1];
      if(lp->bb_varbranch != NULL)
        lp->bb_varbranch[i-1] = lp->bb_varbranch[ii-1];
      if(lp->var_is_free != NULL)
        lp->var_is_free[i] = lp->var_is_free[ii];
    }

    /* Set defaults */
    for(i = 0; i < delta; i++) {
      ii = base + i;
      lp->must_be_int[ii] = ISREAL;
      lp->var_is_sc[ii] = 0;
      if(lp->objfrom != NULL)
        lp->objfrom[ii] = 0;
      if(lp->objtill != NULL)
        lp->objtill[ii] = 0;
      if(lp->var_priority != NULL)
        lp->var_priority[ii-1] = ii;
      if(lp->bb_varbranch != NULL)
        lp->bb_varbranch[ii-1] = BRANCH_DEFAULT;
      if(lp->var_is_free != NULL)
        lp->var_is_free[ii] = 0;
    }
  }

  /* Shift data left (delete) */
  else if(delta < 0) {

    /* Fix invalid split variable data */
    if(lp->var_is_free != NULL) {
      for(i = 1; i <= lp->columns; i++)
        if(abs(lp->var_is_free[i]) >= base)
          lp->var_is_free[i] -= my_chsign(lp->var_is_free[i] < 0, delta);
    }

    /* Shift column data (excluding the basis) */
    for(i = base; i < base-delta; i++) {
      if(is_int(lp, i)) {
        lp->int_count--;
        if(SOS_is_member(lp->SOS, 0, i))
          lp->sos_ints--;
      }
      if(is_semicont(lp, i))
        lp->sc_count--;
    }
    for(i = base; i <= lp->columns + delta; i++) {
      ii = i - delta;
      lp->must_be_int[i] = lp->must_be_int[ii];
      lp->var_is_sc[i] = lp->var_is_sc[ii];
      if(lp->objfrom != NULL)
        lp->objfrom[i] = lp->objfrom[ii];
      if(lp->objtill != NULL)
        lp->objtill[i] = lp->objtill[ii];
      if(lp->var_priority != NULL)
        lp->var_priority[i-1] = lp->var_priority[ii-1];
      if(lp->bb_varbranch != NULL)
        lp->bb_varbranch[i-1] = lp->bb_varbranch[ii-1];
      if(lp->var_is_free != NULL)
        lp->var_is_free[i] = lp->var_is_free[ii];
    }

    /* Fix invalid variable priority data */
    if(lp->var_priority != NULL) {
      for(i = 0; i < lp->columns+delta; i++)
        if(lp->var_priority[i] >= base)
          lp->var_priority[i] += delta;
    }
  }

  shift_basis(lp, lp->rows+base, delta, FALSE);
  if(SOS_count(lp) > 0)
    SOS_shift_col(lp->SOS, 0, base, delta, FALSE);
  shift_rowcoldata(lp, lp->rows+base, delta, FALSE);
  inc_columns(lp, delta);

  return(TRUE);
}

/* Utility group for incrementing row and column vector storage space */
STATIC void inc_rows(lprec *lp, int delta)
{
  lp->rows += delta;
  if(lp->matA->is_roworder)
    lp->matA->columns += delta;
  else
    lp->matA->rows += delta;
}

STATIC void inc_columns(lprec *lp, int delta)
{
  lp->columns += delta;
  if(lp->matA->is_roworder)
    lp->matA->rows += delta;
  else
    lp->matA->columns += delta;
  if(get_Lrows(lp) > 0)
    lp->matL->columns += delta;
}

STATIC MYBOOL inc_rowcol_space(lprec *lp, int delta)
{
  int i, oldrowcolalloc, rowcolsum;

  /* Set constants */
  oldrowcolalloc = lp->sum_alloc;
  lp->sum_alloc += delta;
  rowcolsum = lp->sum_alloc + 1;

  /* Reallocate lp memory */
  allocREAL(lp, &lp->upbo, rowcolsum, AUTOMATIC);
  allocREAL(lp, &lp->orig_upbo, rowcolsum, AUTOMATIC);
  allocREAL(lp, &lp->lowbo, rowcolsum, AUTOMATIC);
  allocREAL(lp, &lp->orig_lowbo, rowcolsum, AUTOMATIC);
  allocREAL(lp, &lp->solution, rowcolsum, AUTOMATIC);
  allocREAL(lp, &lp->best_solution, rowcolsum, AUTOMATIC);
  allocMYBOOL(lp, &lp->is_basic, rowcolsum, AUTOMATIC);
  allocMYBOOL(lp, &lp->is_lower, rowcolsum, AUTOMATIC);
  allocINT(lp, &lp->var_to_orig, rowcolsum, AUTOMATIC);
  allocINT(lp, &lp->orig_to_var, rowcolsum, AUTOMATIC);

  /* Fill in default values, where appropriate */
  for(i = oldrowcolalloc+1; i < rowcolsum; i++) {
    lp->upbo[i] = lp->infinite;
    lp->orig_upbo[i] = lp->upbo[i];
    lp->lowbo[i] = 0;
    lp->orig_lowbo[i] = lp->lowbo[i];
    lp->is_basic[i] = FALSE;
    lp->is_lower[i] = TRUE;
    lp->var_to_orig[i] = 0;
    lp->orig_to_var[i] = 0;
  }

  /* Deal with scalars; the vector can be NULL and also contains Lagrangean information */
  if(lp->scalars != NULL) {
    allocREAL(lp, &lp->scalars, rowcolsum, AUTOMATIC);
    for(i = oldrowcolalloc+1; i < rowcolsum; i++)
      lp->scalars[i] = 1;
  }

  resizePricer(lp);

  return(TRUE);
}

STATIC MYBOOL inc_lag_space(lprec *lp, int deltarows, MYBOOL ignoreMAT)
{
  int newsize;

  if(deltarows > 0) {

    newsize = get_Lrows(lp) + deltarows;

    /* Reallocate arrays */
    allocREAL(lp, &lp->lag_rhs, newsize+1, AUTOMATIC);
    allocREAL(lp, &lp->lambda, newsize+1, AUTOMATIC);
    allocINT(lp, &lp->lag_con_type, newsize+1, AUTOMATIC);

    /* Reallocate the matrix (note that the row scalars are stored at index 0) */
    if(!ignoreMAT) {
      if(lp->matL == NULL)
        lp->matL = mat_create(lp, newsize, lp->columns, lp->epsvalue);
      else
        inc_matrow_space(lp->matL, deltarows);
    }
    lp->matL->rows += deltarows;

  }
  /* Handle column count expansion as special case */
  else if(!ignoreMAT) {
    inc_matcol_space(lp->matL, lp->columns_alloc-lp->matL->columns_alloc+1);
  }


  return(TRUE);
}

STATIC MYBOOL inc_row_space(lprec *lp, int deltarows)
{
  int    i, rowsum, oldrowsalloc, rowdelta = DELTAROWALLOC;
  MYBOOL ok = TRUE;

  /* Adjust lp row structures */
  if(lp->matA->is_roworder)
    inc_matcol_space(lp->matA, deltarows);
  else
    inc_matrow_space(lp->matA, deltarows);
  if(lp->rows+deltarows > lp->rows_alloc) {

    deltarows = MAX(deltarows, rowdelta);
    oldrowsalloc = lp->rows_alloc;
    lp->rows_alloc += deltarows;
    rowsum = lp->rows_alloc + 1;

    allocREAL(lp, &lp->orig_rh, rowsum, AUTOMATIC);
    allocLREAL(lp, &lp->rhs, rowsum, AUTOMATIC);
    allocINT(lp, &lp->row_type, rowsum, AUTOMATIC);
    allocINT(lp, &lp->var_basic, rowsum, AUTOMATIC);
    if(oldrowsalloc == 0) {
      lp->var_basic[0] = AUTOMATIC; /*TRUE;*/  /* Indicates default basis */
      lp->orig_rh[0] = 0;
      lp->row_type[0] = ROWTYPE_OFMIN;
    }
    for(i = oldrowsalloc+1; i < rowsum; i++) {
      lp->orig_rh[i] = 0;
      lp->rhs[i] = 0;
      lp->row_type[i] = ROWTYPE_EMPTY;
      lp->var_basic[i] = i;
    }

    /* Adjust hash name structures */
    if(lp->names_used && (lp->row_name != NULL)) {

      /* First check the hash table */
      if(lp->rowname_hashtab->size < lp->rows_alloc) {
        hashtable *ht;

        ht = copy_hash_table(lp->rowname_hashtab, lp->row_name, lp->rows_alloc + 1);
        if(ht != NULL) {
          free_hash_table(lp->rowname_hashtab);
          lp->rowname_hashtab = ht;
        }
      }

      /* Then the string storage (i.e. pointer to the item's hash structure) */
      lp->row_name = (hashelem **) realloc(lp->row_name, (rowsum) * sizeof(*lp->row_name));
      for(i = oldrowsalloc + 1; i < rowsum; i++)
        lp->row_name[i] = NULL;
    }

    ok = inc_rowcol_space(lp, deltarows);

  }
  return(ok);
}

STATIC MYBOOL inc_col_space(lprec *lp, int deltacols)
{
  int i,colsum, oldcolsalloc, newcolcount;

  if(lp->matA->is_roworder)
    inc_matrow_space(lp->matA, deltacols);
  else
    inc_matcol_space(lp->matA, deltacols);

  newcolcount = lp->columns+deltacols;
  if(newcolcount >= lp->columns_alloc) {

    oldcolsalloc = lp->columns_alloc;
    deltacols = MAX(DELTACOLALLOC, newcolcount);
    lp->columns_alloc += deltacols;
    colsum = lp->columns_alloc + 1;

    /* Adjust hash name structures */
    if(lp->names_used && (lp->col_name != NULL)) {

      /* First check the hash table */
      if(lp->colname_hashtab->size < lp->columns_alloc) {
        hashtable *ht;

        ht = copy_hash_table(lp->colname_hashtab, lp->col_name, lp->columns_alloc + 1);
        if(ht != NULL) {
          free_hash_table(lp->colname_hashtab);
          lp->colname_hashtab = ht;
        }
      }

      /* Then the string storage (i.e. pointer to the item's hash structure) */
      lp->col_name = (hashelem **) realloc(lp->col_name, (colsum) * sizeof(*lp->col_name));
      for(i = oldcolsalloc+1; i < colsum; i++)
        lp->col_name[i] = NULL;
    }

    allocMYBOOL(lp, &lp->must_be_int, colsum, AUTOMATIC);
    allocREAL(lp, &lp->var_is_sc, colsum, AUTOMATIC);
    if(lp->var_priority != NULL)
      allocINT(lp, &lp->var_priority, colsum-1, AUTOMATIC);

    /* Make sure that Lagrangean constraints have the same number of columns */
    if(get_Lrows(lp) > 0)
      inc_lag_space(lp, 0, FALSE);

    /* Update column pointers */
    for(i = MIN(oldcolsalloc, lp->columns) + 1; i < colsum; i++) {
      lp->must_be_int[i] = ISREAL;
      lp->var_is_sc[i] = 0;
      if(lp->var_priority != NULL)
        lp->var_priority[i-1] = i;
    }

    if(lp->var_is_free != NULL) {
      allocINT(lp, &lp->var_is_free, colsum, AUTOMATIC);
      for(i = oldcolsalloc+1; i < colsum; i++)
        lp->var_is_free[i] = 0;
    }

    if(lp->bb_varbranch != NULL) {
      allocMYBOOL(lp, &lp->bb_varbranch, colsum-1, AUTOMATIC);
      for(i = oldcolsalloc; i < colsum-1; i++)
        lp->bb_varbranch[i] = BRANCH_DEFAULT;
    }

    inc_rowcol_space(lp, deltacols);

  }
  return(TRUE);
}


/* Problem manipulation routines */

MYBOOL __WINAPI set_obj(lprec *lp, int Column, REAL Value)
{
  if(Column <= 0)
    Column = lp_solve_set_rh(lp, 0, Value);
  else
    Column = lp_solve_set_mat(lp, 0, Column, Value);
  return((MYBOOL) Column);
}

MYBOOL __WINAPI set_obj_fnex(lprec *lp, int count, REAL *row, int *colno)
{
  int i, n;

  if(colno == NULL)
    n = lp->columns;
  else
    n = count;
  if(lp->matA->is_roworder && (mat_nonzeros(lp->matA) == 0)) {
    mat_appendrow(lp->matA, n, row, colno, my_chsign(is_maxim(lp), 1.0));
  }
  else if(colno == NULL) {
    for(i = 1; i <= n; i++)
      if(!lp_solve_set_mat(lp, 0, i, row[i]))
        return(FALSE);
  }
  else
    for(i = 0; i < n; i++)
      if(!lp_solve_set_mat(lp, 0, colno[i], row[i]))
        return(FALSE);

  return(TRUE);
}

MYBOOL __WINAPI set_obj_fn(lprec *lp, REAL *row)
{
  return( set_obj_fnex(lp, 0, row, NULL) );
}

MYBOOL __WINAPI str_set_obj_fn(lprec *lp, char *row_string)
{
  int    i;
  MYBOOL ret = TRUE;
  REAL   *arow;
  char   *p, *newp;

  allocREAL(lp, &arow, lp->columns + 1, FALSE);
  p = row_string;
  for(i = 1; i <= lp->columns; i++) {
    arow[i] = (REAL) strtod(p, &newp);
    if(p == newp) {
      report(lp, IMPORTANT, "str_set_obj_fn: Bad string %s\n", p);
      lp->spx_status = DATAIGNORED;
      ret = FALSE;
      break;
    }
    else
      p = newp;
  }
  if(lp->spx_status != DATAIGNORED)
    ret = set_obj_fn(lp, arow);
  FREE(arow);
  return( ret );
}

STATIC MYBOOL append_columns(lprec *lp, int deltacolumns)
{
  if(!inc_col_space(lp, deltacolumns))
    return( FALSE );
  varmap_add(lp, lp->sum+1, deltacolumns);
  shift_coldata(lp, lp->columns+1, deltacolumns);
  return( TRUE );
}

STATIC MYBOOL append_rows(lprec *lp, int deltarows)
{
  if(!inc_row_space(lp, deltarows))
    return( FALSE );
  varmap_add(lp, lp->rows+1, deltarows);
  shift_rowdata(lp, lp->rows+1, deltarows);
  return( TRUE );
}

MYBOOL __WINAPI set_add_rowmode(lprec *lp, MYBOOL turnon)
{
  MYBOOL status = TRUE;

  if(lp->matA->is_roworder && !turnon)
    mat_transpose(lp->matA, TRUE);
  else if((mat_nonzeros(lp->matA) == 0) && turnon)
    mat_transpose(lp->matA, FALSE);
  else
    status = FALSE;
  return(status);
}

MYBOOL __WINAPI is_add_rowmode(lprec *lp)
{
  return(lp->matA->is_roworder);
}

MYBOOL __WINAPI add_constraintex(lprec *lp, int count, REAL *row, int *colno, int constr_type, REAL rh)
{
  int    n;
  MYBOOL status = FALSE;

#ifdef Paranoia
  if(!(constr_type == LE || constr_type == GE || constr_type == EQ)) {
    report(lp, IMPORTANT, "add_constraintex: Invalid %d constraint type\n", constr_type);
    return( status );
  }
#endif

  /* Prepare for a new row */
  if(!append_rows(lp, 1))
    return( status );

  /* Set constraint parameters, fix the slack */
  if((constr_type & ROWTYPE_CONSTRAINT) == EQ) {
    lp->equalities++;
    lp->orig_upbo[lp->rows] = 0;
    lp->upbo[lp->rows] = 0;
  }
  lp->row_type[lp->rows] = constr_type;

  if(is_chsign(lp, lp->rows) && (rh != 0))
    lp->orig_rh[lp->rows] = -rh;
  else
    lp->orig_rh[lp->rows] = rh;

  /* Insert the non-zero constraint values */
  if(colno == NULL)
    n = lp->columns;
  else
    n = count;
  mat_appendrow(lp->matA, n, row, colno, my_chsign(is_chsign(lp, lp->rows), 1.0));

#ifdef Paranoia
  if(lp->matA->is_roworder)
    n = lp->matA->columns-1;
  else
    n = lp->matA->rows;
  if(lp->rows != n) {
    report(lp, SEVERE, "add_constraintex: Row count mismatch %d vs %d\n",
                       lp->rows, n);
  }
  else if(is_BasisReady(lp) && !verifyBasis(lp))
    report(lp, SEVERE, "add_constraintex: Invalid basis detected for row %d\n", lp->rows);
  else
#endif
  status = TRUE;

  return( status );
}

MYBOOL __WINAPI add_constraint(lprec *lp, REAL *row, int constr_type, REAL rh)
{
  return( add_constraintex(lp, 0, row, NULL, constr_type, rh) );
}

MYBOOL __WINAPI str_add_constraint(lprec *lp, char *row_string, int constr_type, REAL rh)
{
  int    i;
  char   *p, *newp;
  REAL   *aRow;
  MYBOOL status = FALSE;

  allocREAL(lp, &aRow, lp->columns + 1, FALSE);
  p = row_string;

  for(i = 1; i <= lp->columns; i++) {
    aRow[i] = (REAL) strtod(p, &newp);
    if(p == newp) {
      report(lp, IMPORTANT, "str_add_constraint: Bad string %s\n", p);
      lp->spx_status = DATAIGNORED;
      break;
    }
    else
      p = newp;
  }
  if(lp->spx_status != DATAIGNORED)
    status = add_constraint(lp, aRow, constr_type, rh);
  FREE(aRow);

  return(status);
}

MYBOOL __WINAPI del_constraint(lprec *lp, int del_row)
{
  MYBOOL preparecompact = (MYBOOL) (del_row < 0);

  preparecompact = (MYBOOL) (del_row < 0);
  if(preparecompact)
    del_row = -del_row;
#ifdef Paranoia
  if((del_row < 1) || (del_row > lp->rows)) {
    report(lp, IMPORTANT, "del_constraint: Attempt to delete non-existing constraint %d\n", del_row);
    return(FALSE);
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "del_constraint: Cannot delete constraint while in row entry mode.\n");
    return(FALSE);
  }
#endif

  if(is_constr_type(lp, del_row, EQ) && (lp->equalities > 0))
    lp->equalities--;

  varmap_delete(lp, del_row, -1);
  shift_rowdata(lp, my_chsign(preparecompact,del_row), -1);
#ifdef Paranoia
  if(is_BasisReady(lp) && !verifyBasis(lp))
    report(lp, SEVERE, "del_constraint: Invalid basis detected at row %d\n", del_row);
#endif

  return(TRUE);
}

MYBOOL __WINAPI add_lag_con(lprec *lp, REAL *row, int con_type, REAL rhs)
{
  int  k;
  REAL sign;

  if(con_type == LE || con_type == EQ)
    sign = 1;
  else if(con_type == GE)
    sign = -1;
  else {
    report(lp, IMPORTANT, "add_lag_con: Constraint type %d not implemented\n", con_type);
    return(FALSE);
  }

  inc_lag_space(lp, 1, FALSE);

  k = get_Lrows(lp);
  lp->lag_rhs[k] = rhs * sign;
  mat_appendrow(lp->matL, lp->columns, row, NULL, sign);
  lp->lambda[k] = 0;
  lp->lag_con_type[k] = con_type;

  return(TRUE);
}

MYBOOL __WINAPI str_add_lag_con(lprec *lp, char *row_string, int con_type, REAL rhs)
{
  int    i;
  MYBOOL ret = TRUE;
  REAL   *a_row;
  char   *p, *new_p;

  allocREAL(lp, &a_row, lp->columns + 1, FALSE);
  p = row_string;

  for(i = 1; i <= lp->columns; i++) {
    a_row[i] = (REAL) strtod(p, &new_p);
    if(p == new_p) {
      report(lp, IMPORTANT, "str_add_lag_con: Bad string %s\n", p);
      lp->spx_status = DATAIGNORED;
      ret = FALSE;
      break;
    }
    else
      p = new_p;
  }
  if(lp->spx_status != DATAIGNORED)
    ret = add_lag_con(lp, a_row, con_type, rhs);
  FREE(a_row);
  return( ret );
}

STATIC MYBOOL is_splitvar(lprec *lp, int column)
/* Two cases handled by var_is_free:

   1) LB:-Inf / UB:<Inf variables
      No helper column created, sign of var_is_free set negative with index to itself.
   2) LB:-Inf / UB: Inf (free) variables
      Sign of var_is_free set positive with index to new helper column,
      helper column created with negative var_is_free with index to the original column.

   This function helps identify the helper column in 2).
*/
{
   return((MYBOOL) ((lp->var_is_free != NULL) &&
                    (lp->var_is_free[column] < 0) && (-lp->var_is_free[column] != column)));
}

void del_splitvars(lprec *lp)
{
  int j, jj, i;

  if(lp->var_is_free != NULL) {
    for (j = lp->columns; j >= 1; j--)
      if(is_splitvar(lp, j)) {
        /* Check if we need to modify the basis */
        jj = lp->rows+abs(lp->var_is_free[j]);
        if(lp->is_basic[lp->rows+j] && !lp->is_basic[jj]) {
          i = findBasisPos(lp, jj, NULL);
          setBasisVar(lp, i, jj);
        }
        /* Delete the helper column */
        del_column(lp, j);
      }
    FREE(lp->var_is_free);
  }
}

MYBOOL __WINAPI add_columnex(lprec *lp, int count, REAL *column, int *rowno)
/* This function adds a data column to the current model; three cases handled:

    1: Prepare for column data by setting column = NULL
    2: Dense vector indicated by (rowno == NULL) over 0..count+get_Lrows() elements
    3: Sparse vector set over row vectors rowno, over 0..count-1 elements.

   NB! If the column has only one entry, this should be handled as
       a bound, but this currently is not the case  */
{
  MYBOOL status = FALSE;

 /* Prepare and shift column vectors */
  if(!append_columns(lp, 1))
    return( status );

 /* Append sparse regular constraint values */
  if(mat_appendcol(lp->matA, count, column, rowno, 1.0) < 0)
    report(lp, SEVERE, "add_columnex: Data column supplied in non-ascending row index order.\n");

#ifdef Paranoia
  if(lp->columns != lp->matA->columns) {
    report(lp, SEVERE, "add_columnex: Column count mismatch %d vs %d\n",
                       lp->columns, lp->matA->columns);
  }
  else if(is_BasisReady(lp) && (lp->Extrap == 0) && !verifyBasis(lp))
    report(lp, SEVERE, "add_columnex: Invalid basis detected for column %d\n", lp->columns);
#endif
  else
    status = TRUE;

  return( status );
}

MYBOOL __WINAPI add_column(lprec *lp, REAL *column)
{
  del_splitvars(lp);
  return(add_columnex(lp, lp->rows, column, NULL));
}

MYBOOL __WINAPI str_add_column(lprec *lp, char *col_string)
{
  int  i;
  MYBOOL ret = TRUE;
  REAL *aCol;
  char *p, *newp;

  allocREAL(lp, &aCol, lp->rows + 1, FALSE);
  p = col_string;

  for(i = 0; i <= lp->rows; i++) {
    aCol[i] = (REAL) strtod(p, &newp);
    if(p == newp) {
      report(lp, IMPORTANT, "str_add_column: Bad string %s\n", p);
      lp->spx_status = DATAIGNORED;
      ret = FALSE;
      break;
    }
    else
      p = newp;
  }
  if(lp->spx_status != DATAIGNORED)
    ret = add_column(lp, aCol);
  FREE(aCol);
  return( ret );
}

MYBOOL __WINAPI del_column(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "del_column: Column %d out of range\n", column);
    return(FALSE);
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "del_column: Cannot delete column while in row entry mode.\n");
    return(FALSE);
  }
#endif

  if((lp->var_is_free != NULL) && (lp->var_is_free[column] > 0))
    del_column(lp, lp->var_is_free[column]); /* delete corresponding split column (is always after this column) */

  varmap_delete(lp, lp->rows+column, -1);
  shift_coldata(lp, column, -1);
#ifdef Paranoia
  if(is_BasisReady(lp) && (lp->Extrap == 0) && !verifyBasis(lp))
    report(lp, SEVERE, "del_column: Invalid basis detected at column %d (%d)\n", column, lp->columns);
#endif

  return(TRUE);
}

void __WINAPI set_simplextype(lprec *lp, int simplextype)
{
  lp->simplex_strategy = simplextype;
}

int __WINAPI get_simplextype(lprec *lp)
{
  return(lp->simplex_strategy);
}

void __WINAPI set_preferdual(lprec *lp, MYBOOL dodual)
{
  if(dodual & TRUE)
    lp->simplex_strategy = SIMPLEX_DUAL_DUAL;
  else
    lp->simplex_strategy = SIMPLEX_PRIMAL_PRIMAL;
}

void __WINAPI set_bounds_tighter(lprec *lp, MYBOOL tighten)
{
  lp->tighten_on_set = tighten;
}
MYBOOL __WINAPI get_bounds_tighter(lprec *lp)
{
  return(lp->tighten_on_set);
}

MYBOOL __WINAPI lp_solve_set_upbo(lprec *lp, int column, REAL value)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "lp_solve_set_upbo: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

#ifdef DoBorderRounding
  if(fabs(value) < lp->infinite)
    value = roundToPrecision(value, lp->epsvalue);
#endif
  value = scaled_value(lp, value, lp->rows + column);
  if(lp->tighten_on_set) {
    if(value < lp->orig_lowbo[lp->rows + column]) {
      report(lp, IMPORTANT, "lp_solve_set_upbo: Upperbound must be >= lowerbound\n");
      return(FALSE);
    }
    if(value < lp->orig_upbo[lp->rows + column]) {
      lp->doRebase = TRUE;
      lp->orig_upbo[lp->rows + column] = value;
    }
  }
  else
  {
    lp->doRebase = TRUE;
    if(value > lp->infinite)
      value = lp->infinite;
    lp->orig_upbo[lp->rows + column] = value;
  }
  return(TRUE);
}

REAL __WINAPI get_upbo(lprec *lp, int column)
{
  REAL value;

#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "get_upbo: Column %d out of range\n", column);
    return(0);
  }
#endif
  value = lp->orig_upbo[lp->rows + column];
  value = unscaled_value(lp, value, lp->rows + column);
  return(value);
}

MYBOOL __WINAPI lp_solve_set_lowbo(lprec *lp, int column, REAL value)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "lp_solve_set_lowbo: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

#ifdef DoBorderRounding
  if(fabs(value) < lp->infinite)
    value = roundToPrecision(value, lp->epsvalue);
#endif
  value = scaled_value(lp, value, lp->rows + column);
  if(lp->tighten_on_set) {
    if(value > lp->orig_upbo[lp->rows + column]) {
      report(lp, IMPORTANT, "lp_solve_set_lowbo: Upper bound must be >= lower bound\n");
      return(FALSE);
    }
    if((value < 0) || (value > lp->orig_lowbo[lp->rows + column])) {
      lp->doRebase = TRUE;
      lp->orig_lowbo[lp->rows + column] = value;
    }
  }
  else
  {
    lp->doRebase = TRUE;
    if(value < -lp->infinite)
      value = -lp->infinite;
    lp->orig_lowbo[lp->rows + column] = value;
  }
  return(TRUE);
}

REAL __WINAPI get_lowbo(lprec *lp, int column)
{
  REAL value;

#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "get_lowbo: Column %d out of range\n", column);
    return(0);
  }
#endif
  value = lp->orig_lowbo[lp->rows + column];
  value = unscaled_value(lp, value, lp->rows + column);
  return(value);
}

MYBOOL __WINAPI set_bounds(lprec *lp, int column, REAL lower, REAL upper)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "set_bounds: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  if(lp->scaling_used) {
    lower = scaled_value(lp, lower, lp->rows + column);
    upper = scaled_value(lp, upper, lp->rows + column);
  }
  if(lower > upper) {
    report(lp, IMPORTANT, "set_bounds: Column %d upper bound must be >= lower bound\n",
         column);
    return(FALSE);
  }

  lp->doRebase = TRUE;
  if(lower < -lp->infinite)
    lower = -lp->infinite;
  if(upper > lp->infinite)
    upper = lp->infinite;

  lp->orig_lowbo[lp->rows+column] = lower;
  lp->orig_upbo[lp->rows+column] = upper;

  return(TRUE);
}

MYBOOL get_bounds(lprec *lp, int column, REAL *lower, REAL *upper)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "get_bounds: Column %d out of range", column);
    return(FALSE);
  }
#endif

  if(lower != NULL)
    *lower = get_lowbo(lp, column);
  if(upper != NULL)
    *upper = get_upbo(lp, column);

  return(TRUE);
}

MYBOOL __WINAPI lp_solve_set_int(lprec *lp, int column, MYBOOL must_be_int)
{
#ifdef Paranoia
  if((column > lp->columns) || (column < 1)) {
    report(lp, IMPORTANT, "lp_solve_set_int: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  if((lp->must_be_int[column] & ISINTEGER) != 0) {
    lp->int_count--;
    lp->must_be_int[column] &= !ISINTEGER;
  }
  if(must_be_int) {
    lp->must_be_int[column] |= ISINTEGER;
    lp->int_count++;
    if(lp->columns_scaled && !is_integerscaling(lp))
      unscale_columns(lp);
  }
  return(TRUE);
}

MYBOOL __WINAPI is_int(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "is_int: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  return((lp->must_be_int[column] & ISINTEGER) != 0);
}

MYBOOL __WINAPI is_SOS_var(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "is_SOS_var: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  return((lp->must_be_int[column] & ISSOS) != 0);
}

int __WINAPI add_SOS(lprec *lp, char *name, int sostype, int priority, int count, int *sosvars, REAL *weights)
{
  SOSrec *SOS;
  int    k;

#ifdef Paranoia
  if((sostype < 1) || (count < 0)) {
    report(lp, IMPORTANT, "add_SOS: Invalid SOS type definition %d\n", sostype);
    return(FALSE);
  }
#endif
  if(sostype > 3)
    report(lp, DETAILED, "add_SOS: High-order SOS selected (%d)\n", sostype);

  /* Make size in the list to handle another SOS record */
  if(lp->SOS == NULL)
    lp->SOS = create_SOSgroup(lp);

  /* Create and append SOS to list */
  SOS = create_SOSrec(lp->SOS, name, sostype, priority, count, sosvars, weights);
  k = append_SOSgroup(lp->SOS, SOS);

  return(k);
}

STATIC int add_GUB(lprec *lp, char *name, int priority, int count, int *gubvars)
{
  SOSrec *GUB;
  int    k;

#ifdef Paranoia
  if(count < 0) {
    report(lp, IMPORTANT, "add_GUB: Invalid GUB member count %d\n", count);
    return(FALSE);
  }
#endif

  /* Make size in the list to handle another GUB record */
  if(lp->GUB == NULL)
    lp->GUB = create_SOSgroup(lp);

  /* Create and append GUB to list */
  GUB = create_SOSrec(lp->GUB, name, 1, priority, count, gubvars, NULL);
  GUB->isGUB = TRUE;
  k = append_SOSgroup(lp->GUB, GUB);

  return(k);
}

MYBOOL __WINAPI set_binary(lprec *lp, int column, MYBOOL must_be_bin)
{
  MYBOOL status = FALSE;

#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "set_binary: Column %d out of range\n", column);
    return( status );
  }
#endif
  status = lp_solve_set_int(lp, column, must_be_bin);
  if(status && must_be_bin)
    status = set_bounds(lp, column, 0, 1);
  return( status );
}

MYBOOL __WINAPI is_binary(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "is_binary: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  return((MYBOOL) (((lp->must_be_int[column] & ISINTEGER) != 0) &&
                   (get_lowbo(lp, column) == 0) &&
                   (fabs(get_upbo(lp, column) - 1) < lp->epsprimal)));
}

MYBOOL __WINAPI set_free(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "set_free: Column %d out of range\n", column);
    return( FALSE );
  }
#endif
  return( set_bounds(lp, column, -lp->infinite, lp->infinite) );
}

MYBOOL __WINAPI is_free(lprec *lp, int column)
{
  MYBOOL test;

#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "is_free: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  test = is_splitvar(lp, column);
  if(!test) {
    column += lp->rows;
    test = (MYBOOL) ((lp->orig_lowbo[column] <= -lp->infinite) &&
                     (lp->orig_upbo[column] >= lp->infinite));
  }
  return( test );
}

MYBOOL __WINAPI is_negative(lprec *lp, int column)
{

#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "is_negative: Column %d out of range\n", column);
    return( FALSE );
  }
#endif

  column += lp->rows;
  return( (MYBOOL) ((lp->orig_upbo[column] <= 0) &&
                    (lp->orig_lowbo[column] < 0)) );
}

MYBOOL __WINAPI set_var_weights(lprec *lp, REAL *weights)
{
  if(lp->var_priority != NULL) {
    FREE(lp->var_priority);
  }
  if(weights != NULL) {
    int n;
    allocINT(lp, &lp->var_priority, lp->columns_alloc, FALSE);
    for(n = 0; n < lp->columns; n++) {
      lp->var_priority[n] = n+1;
    }
    n = sortByREAL(lp->var_priority, weights, lp->columns, 0, FALSE);
  }
  return(TRUE);
}

MYBOOL set_varpriority(lprec *lp)
/* Template for experimental automatic variable ordering/priority setting */
{
  MYBOOL Status = FALSE;

  if(FALSE &&
     (lp->var_priority == NULL) &&
     (SOS_count(lp) == 0)) {

    REAL *rcost;
    REAL holdval;
    int  i;

    allocREAL(lp, &rcost, lp->columns+1, FALSE);

   /* Measure the distance from being integer */
    for(i = 1; i <= lp->columns; i++) {
      holdval = lp->best_solution[lp->rows+i];
      rcost[i] = -fabs(holdval - (floor(holdval)+ceil(holdval)) / 2);
/*      rcost[i] *= mat_getitem(lp, 0, i); */
/*      rcost[i] *= lp->upbo[lp->rows+i] - lp->lowbo[lp->rows+i]; */
    }

   /* Establish the MIP variable priorities */
    set_var_weights(lp, rcost+1);

    FREE(rcost);
    Status = TRUE;
  }

  return( Status );
}

int __WINAPI get_var_priority(lprec *lp, int column)
{
#ifdef Paranoia
  if(column > lp->columns || column < 1) {
    report(lp, IMPORTANT, "get_var_priority: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  if(lp->var_priority == NULL)
    return(column);
  else
    return(lp->var_priority[column-1]);
}

MYBOOL __WINAPI set_semicont(lprec *lp, int column, MYBOOL must_be_sc)
{
#ifdef Paranoia
  if((column > lp->columns) || (column < 1)) {
    report(lp, IMPORTANT, "set_semicont: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  if(lp->var_is_sc[column] != 0) {
    lp->sc_count--;
    lp->must_be_int[column] &= !ISSEMI;
  }
  lp->var_is_sc[column] = must_be_sc;
  if(must_be_sc) {
    lp->must_be_int[column] |= ISSEMI;
    lp->sc_count++;
  }
  return(TRUE);
}

MYBOOL __WINAPI is_semicont(lprec *lp, int column)
{
#ifdef Paranoia
  if((column > lp->columns) || (column < 1)) {
    report(lp, IMPORTANT, "is_semicont: Column %d out of range\n", column);
    return(FALSE);
  }
#endif

  return((lp->must_be_int[column] & ISSEMI) != 0);
}

MYBOOL __WINAPI lp_solve_set_rh(lprec *lp, int row, REAL value)
{
#ifdef Paranoia
  if((row > lp->rows) || (row < 0)) {
    report(lp, IMPORTANT, "lp_solve_set_rh: Row %d out of range\n", row);
    return(FALSE);
  }
#endif

  if(((row == 0) && (!is_maxim(lp))) ||
     ((row > 0) && is_chsign(lp, row)))    /* setting of RHS of OF IS meaningful */
    value = my_flipsign(value);
  if(fabs(value) > lp->infinite) {
    if (value < 0)
      value = -lp->infinite;
    else
      value = lp->infinite;
  }
#ifdef DoBorderRounding
  else
    value = roundToPrecision(value, lp->epsvalue);
#endif
  value = scaled_value(lp, value, row);
  lp->orig_rh[row] = value;
  lp->doRecompute = TRUE;
  return(TRUE);
}

REAL __WINAPI get_rh(lprec *lp, int row)
{
  REAL value;

#ifdef Paranoia
  if((row > lp->rows) || (row < 0)) {
    report(lp, IMPORTANT, "get_rh: Row %d out of range", row);
    return(0.0);
  }
#endif

  value = lp->orig_rh[row];
  if (((row == 0) && !is_maxim(lp)) ||
      ((row > 0) && is_chsign(lp, row)))    /* setting of RHS of OF IS meaningful */
    value = my_flipsign(value);
  value = unscaled_value(lp, value, row);
  return(value);
}

REAL get_rh_upper(lprec *lp, int row)
{
  REAL value, valueR;

  value = lp->orig_rh[row];
  if(is_chsign(lp, row)) {
    valueR = lp->orig_upbo[row];
    if(valueR >= lp->infinite)
      return(lp->infinite);
    value = my_flipsign(value);
    value += valueR;
  }
  value = unscaled_value(lp, value, row);
  return(value);
}

REAL get_rh_lower(lprec *lp, int row)
{
  REAL value, valueR;

  value = lp->orig_rh[row];
  if(is_chsign(lp, row))
    value = my_flipsign(value);
  else {
    valueR = lp->orig_upbo[row];
    if(valueR >= lp->infinite)
      return(-lp->infinite);
    value -= valueR;
  }
  value = unscaled_value(lp, value, row);
  return(value);
}

MYBOOL set_rh_upper(lprec *lp, int row, REAL value)
{
#ifdef Paranoia
  if(row > lp->rows || row < 1) {
    report(lp, IMPORTANT, "set_rh_upper: Row %d out of range", row);
    return(FALSE);
  }
#endif

 /* First scale the value */
  value = scaled_value(lp, value, row);

 /* orig_rh stores the upper bound assuming a < constraint;
    If we have a > constraint, we must adjust the range instead */
  if(is_chsign(lp, row)) {
    if(fabs(value) >= lp->infinite)
      lp->orig_upbo[row] = lp->infinite;
    else {
#ifdef Paranoia
      if(value + lp->orig_rh[row] < 0)
        report(lp, SEVERE, "set_rh_upper: Invalid negative range in row %d\n",
                           row);
#endif
#ifdef DoBorderRounding
      lp->orig_upbo[row] = roundToPrecision(value + lp->orig_rh[row], lp->epsvalue);
#else
      lp->orig_upbo[row] = value + lp->orig_rh[row];
#endif
    }
  }
  else
    lp->orig_rh[row] = value;
  return(TRUE);
}

MYBOOL set_rh_lower(lprec *lp, int row, REAL value)
{
#ifdef Paranoia
  if(row > lp->rows || row < 1) {
    report(lp, IMPORTANT, "set_rh_lower: Row %d out of range", row);
    return(FALSE);
  }
#endif

 /* First scale the value */
  value = scaled_value(lp, value, row);

 /* orig_rh stores the upper bound assuming a < constraint;
    If we have a < constraint, we must adjust the range instead */
  if(!is_chsign(lp, row)) {
    if(value <= -lp->infinite)
      lp->orig_upbo[row] = lp->infinite;
    else {
#ifdef Paranoia
      if(lp->orig_rh[row] - value < 0)
        report(lp, SEVERE, "set_rh_lower: Invalid negative range in row %d\n",
                           row);
#endif
#ifdef DoBorderRounding
      lp->orig_upbo[row] = roundToPrecision(lp->orig_rh[row] - value, lp->epsvalue);
#else
      lp->orig_upbo[row] = lp->orig_rh[row] - value;
#endif
    }
  }
  else
    lp->orig_rh[row] = my_flipsign(value);
  return(TRUE);
}

MYBOOL __WINAPI set_rh_range(lprec *lp, int row, REAL deltavalue)
{
#ifdef Paranoia
  if(row > lp->rows || row < 1) {
    report(lp, IMPORTANT, "set_rh_range: Row %d out of range", row);
    return(FALSE);
  }
#endif

  deltavalue = scaled_value(lp, deltavalue, row);
  if(deltavalue > lp->infinite)
    deltavalue = lp->infinite;
  else if(deltavalue < -lp->infinite)
    deltavalue = -lp->infinite;
#ifdef DoBorderRounding
  else
    deltavalue = roundToPrecision(deltavalue, lp->epsvalue);
#endif

  if(fabs(deltavalue) < lp->epsprimal) {
    /* Conversion to EQ */
    lp_solve_set_constr_type(lp, row, EQ);
  }
  else if(is_constr_type(lp, row, EQ)) {
    /* EQ with a non-zero range */
    if(deltavalue > 0)
      lp_solve_set_constr_type(lp, row, GE);
    else
      lp_solve_set_constr_type(lp, row, LE);
    lp->orig_upbo[row] = fabs(deltavalue);
  }
  else {
    /* Modify GE/LE ranges */
    lp->orig_upbo[row] = fabs(deltavalue);
  }

  return(TRUE);
}

REAL __WINAPI get_rh_range(lprec *lp, int row)
{
#ifdef Paranoia
  if(row > lp->rows || row < 0) {
    report(lp, IMPORTANT, "get_rh_range: row %d out of range\n", row);
    return(FALSE);
  }
#endif
  if(lp->orig_upbo[row] >= lp->infinite)
    return(lp->orig_upbo[row]);
  else
    return(unscaled_value(lp, lp->orig_upbo[row], row));
}

void __WINAPI set_rh_vec(lprec *lp, REAL *rh)
{
  int  i;
  REAL rhi;

  for(i = 1; i <= lp->rows; i++) {
    rhi = rh[i];
#ifdef DoBorderRounding
    rhi = roundToPrecision(rhi, lp->epsvalue);
#endif
    lp->orig_rh[i] = my_chsign(is_chsign(lp, i), scaled_value(lp, rhi, i));
  }
  lp->doRecompute = TRUE;
}

MYBOOL __WINAPI str_set_rh_vec(lprec *lp, char *rh_string)
{
  int  i;
  MYBOOL ret = TRUE;
  REAL *newrh;
  char *p, *newp;

  allocREAL(lp, &newrh, lp->rows + 1, TRUE);
  p = rh_string;

  for(i = 1; i <= lp->rows; i++) {
    newrh[i] = (REAL) strtod(p, &newp);
    if(p == newp) {
      report(lp, IMPORTANT, "str_set_rh_vec: Bad string %s\n", p);
      lp->spx_status = DATAIGNORED;
      ret = FALSE;
      break;
    }
    else
      p = newp;
  }
  if(!(lp->spx_status == DATAIGNORED))
    set_rh_vec(lp, newrh);
  FREE(newrh);
  return( ret );
}

void __WINAPI set_sense(lprec *lp, MYBOOL maximize)
{
  maximize = (maximize == TRUE);
  if(is_maxim(lp) != maximize) {
    mat_multrow(lp->matA, 0, -1);
    lp->orig_rh[0] = my_flipsign(lp->orig_rh[0]);
    lp->doInvert = TRUE;
    lp->doRecompute = TRUE;
  }
  if(maximize)
    lp->row_type[0] = ROWTYPE_OFMAX;
  else
    lp->row_type[0] = ROWTYPE_OFMIN;
}

void __WINAPI lp_solve_set_maxim(lprec *lp)
{
  set_sense(lp, TRUE);
}

void __WINAPI lp_solve_set_minim(lprec *lp)
{
  set_sense(lp, FALSE);
}

MYBOOL __WINAPI is_maxim(lprec *lp)
{
  return( (MYBOOL) ((lp->row_type[0] & ROWTYPE_CHSIGN) == ROWTYPE_GE) );
}

MYBOOL __WINAPI lp_solve_set_constr_type(lprec *lp, int row, int con_type)
{
  MYBOOL oldchsign;

#ifdef Paranoia
  if(row > lp->rows+1 || row < 1) {
    report(lp, IMPORTANT, "lp_solve_set_constr_type: Row %d out of range\n", row);
    return( FALSE );
  }
#endif
  /* Prepare for a new row */
  if((row > lp->rows) && !append_rows(lp, row-lp->rows))
    return( FALSE );

  /* Update the constraint type data */
  if(is_constr_type(lp, row, EQ))
    lp->equalities--;

  if((con_type & ROWTYPE_CONSTRAINT) == EQ) {
    lp->equalities++;
    lp->orig_upbo[row] = 0;
  }
  else if(((con_type & LE) > 0) || ((con_type & GE) > 0))
    lp->orig_upbo[row] = lp->infinite;
  else {
    report(lp, IMPORTANT, "lp_solve_set_constr_type: Constraint type %d not implemented (row %d)\n",
                          con_type, row);
    return( FALSE );
  }

  /* Change the signs of the row, if necessary */
  oldchsign = is_chsign(lp, row);
  lp->row_type[row] = con_type;
  if(oldchsign != is_chsign(lp, row)) {
    mat_multrow(lp->matA, row, -1);
    if(lp->orig_rh[row] != 0)
      lp->orig_rh[row] *= -1;
    lp->doRecompute = TRUE;
  }
  lp->basis_valid = FALSE;
  lp->doInvert = TRUE;

  return( TRUE );
}

STATIC MYBOOL is_chsign(lprec *lp, int rownr)
{
  return( (MYBOOL) ((lp->row_type[rownr] & ROWTYPE_CONSTRAINT) == ROWTYPE_CHSIGN) );
}

MYBOOL __WINAPI is_constr_type(lprec *lp, int row, int mask)
{
#ifdef Paranoia
  if(row < 0 || row > lp->rows) {
    report(lp, IMPORTANT, "is_constr_type: Row %d out of range\n", row);
    return( FALSE );
  }
#endif
  return( (MYBOOL) ((lp->row_type[row] & ROWTYPE_CONSTRAINT) == mask));
}

int __WINAPI get_constr_type(lprec *lp, int row)
{
#ifdef Paranoia
  if(row < 0 || row > lp->rows) {
    report(lp, IMPORTANT, "get_constr_type: Row %d out of range\n", row);
    return(-1);
  }
#endif
  return( lp->row_type[row] );
}

STATIC REAL get_OF_raw(lprec *lp, int colnr)
{
  REAL holdOF;

  colnr -= lp->rows;
  if(colnr <= 0) {
    holdOF = 0;
#ifdef Paranoia
    if(colnr < 0)
      report(lp, SEVERE, "get_OF_raw: Invalid column index %d supplied\n", colnr);
#endif
  }
  else if(lp->OF_override == NULL) {
    holdOF = mat_getitem(lp->matA, 0, colnr);
    modifyOF1(lp, colnr+lp->rows, &holdOF, 1.0);
  }
  else
    holdOF = lp->OF_override[colnr];

  return( holdOF );
}

STATIC REAL get_OF_contribution(lprec *lp, int colnr, MYBOOL basic)
{
  REAL holdOF, holdQ;

  if(basic) {
    holdQ = lp->rhs[colnr];
    colnr = lp->var_basic[colnr];
    if(!lp->is_lower[colnr])
      holdQ = lp->upbo[colnr] - holdQ;
  }
  else {
    if(lp->is_lower[colnr])
      holdQ = 0;
    else
      holdQ = lp->upbo[colnr];
  }

  holdOF = get_OF_raw(lp, colnr)*holdQ;

  return( holdOF );
}

REAL __WINAPI get_mat(lprec *lp, int row, int column)
{
  REAL value;
  int elmnr;

#ifdef Paranoia
  if(row < 0 || row > lp->rows) {
    report(lp, IMPORTANT, "get_mat: Row %d out of range", row);
    return(0);
  }
  if(column < 1 || column > lp->columns) {
    report(lp, IMPORTANT, "get_mat: Column %d out of range", column);
    return(0);
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "get_mat: Cannot read a matrix value while in row entry mode.\n");
    return(0);
  }
#endif
#ifdef DirectOverrideOF
  if((row == 0) && (lp->OF_override != NULL)) {
    value = lp->OF_override[column];
    value = my_chsign(is_chsign(lp, row), value);
    value = unscaled_mat(lp, value, row, column);
  }
  else
#endif
  {
    elmnr = mat_findelm(lp->matA, row, column);
    if(elmnr >= 0) {
      value = my_chsign(is_chsign(lp, row), lp->matA->col_mat[elmnr].value);
      value = unscaled_mat(lp, value, row, column);
    }
    else
      value = 0;
  }
  return(value);
}

MATitem *get_mat_item(lprec *lp, int matindex, MYBOOL isrow)
{
  if(isrow) {
    MATrec *mat = lp->matA;
    return( ROW_MAT_PTR(mat->row_mat[matindex]) );
  }
  else
    return( &(lp->matA->col_mat[matindex]) );
}

REAL get_mat_byindex(lprec *lp, int matindex, MYBOOL isrow)
/* Note that this function does not adjust for sign-changed GT constraints! */
{
  MATitem *matelem;
  matelem = get_mat_item(lp, matindex, isrow);
  if(lp->scaling_used)
    return( unscaled_mat(lp, (*matelem).value, (*matelem).row_nr, (*matelem).col_nr) );
  else
    return( (*matelem).value );
}


MYBOOL __WINAPI get_row(lprec *lp, int row_nr, REAL *row)
{
  int     i, ie, j;
  MATrec *mat = lp->matA;

#ifdef Paranoia
  if(row_nr <0 || row_nr > lp->rows) {
    report(lp, IMPORTANT, "get_row: Row %d out of range\n", row_nr);
    return( FALSE );
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "get_row: Cannot return a matrix row while in row entry mode.\n");
    return(FALSE);
  }
#endif

  if(!mat_validate(lp->matA)) {
    for(i = 1; i <= lp->columns; i++)
      row[i] = get_mat(lp,row_nr,i);
  }
  else {
    MYBOOL chsign;
    REAL a;

    if(row_nr == 0)
      i = 0;
    else
      i = mat->row_end[row_nr-1];
    ie = mat->row_end[row_nr];
    chsign = is_chsign(lp, row_nr);
    MEMCLEAR(row, lp->columns+1);
    for(; i < ie; i++) {
      j = ROW_MAT_COL(mat->row_mat[i]);
      a = get_mat_byindex(lp, i, TRUE);
      row[j] = my_chsign(chsign, a);
    }
  }
  return( TRUE );
}

MYBOOL __WINAPI get_column(lprec *lp, int col_nr, REAL *column)
{
  int i,ii;

#ifdef Paranoia
  if(col_nr < 1 || col_nr > lp->columns) {
    report(lp, IMPORTANT, "get_column: Column %d out of range\n", col_nr);
    return( FALSE );
  }
  if(lp->matA->is_roworder) {
    report(lp, IMPORTANT, "get_column: Cannot return a column while in row entry mode.\n");
    return(FALSE);
  }
#endif

  MEMCLEAR(column, lp->rows + 1);
  for(i = lp->matA->col_end[col_nr - 1]; i < lp->matA->col_end[col_nr]; i++) {
    ii = lp->matA->col_mat[i].row_nr;
    column[ii] = my_chsign(is_chsign(lp, ii), lp->matA->col_mat[i].value);
    column[ii] = unscaled_mat(lp, column[ii], ii, col_nr);
  }
  return( TRUE );
}

STATIC void set_OF_override(lprec *lp, REAL *ofVector)
/* The purpose of this function is to set, or clear if NULL, the
   ofVector[0..columns] as the active objective function instead of
   the one stored in the A-matrix. See also lag_solve().*/
{
  lp->OF_override = ofVector;
}

STATIC void fill_OF_override(lprec *lp)
{
  int  i;
  REAL *value;

  if(lp->OF_override == NULL)
    return;
  for(i = 1, value = lp->OF_override+1; i <= lp->columns; i++, value++) {
    *value = mat_getitem(lp->matA, 0, i);
    modifyOF1(lp, i, value, 1.0);
  }
}

MYBOOL __WINAPI modifyOF1(lprec *lp, int col_nr, REAL *ofValue, REAL mult)
/* Adjust objective function values for primal/dual phase 1, if appropriate */
{
  static MYBOOL accept;
  static int    Extrap;

  /* Primal simplex: Set user variables to zero or BigM-scaled */
  accept = TRUE;
  Extrap = abs(lp->Extrap);
  if(Extrap > 0) {
#ifndef Phase1EliminateRedundant
    if(lp->Extrap < 0) {
      if(col_nr > lp->sum - Extrap)
        accept = FALSE;
    }
    else
#endif
    if((col_nr <= lp->sum - Extrap) || (mult == 0)) {
      if((mult == 0) || (lp->bigM == 0))
        accept = FALSE;
      else
        (*ofValue) /= lp->bigM;
    }
  }

  /* Dual simplex: Subtract Extrad from objective function values */
  else if((lp->Extrad != 0) && (col_nr > lp->rows))
    *ofValue -= lp->Extrad;

  /* Do scaling and test for zero */
  if(accept) {
    (*ofValue) *= mult;
    if(fabs(*ofValue) < lp->epsmachine) {
      (*ofValue) = 0;
      accept = FALSE;
    }
  }
  else
    (*ofValue) = 0;

  return( accept );
}

STATIC int singleton_column(lprec *lp, int row_nr, REAL *column, int *nzlist, REAL value, int *maxabs)
{
  int nz = 1;

  if(nzlist == NULL) {
    MEMCLEAR(column, lp->rows + 1);
    column[row_nr] = value;
  }
  else {
    column[nz] = value;
    nzlist[nz] = row_nr;
  }

  if(maxabs != NULL)
    *maxabs = row_nr;
  return( nz );
}

STATIC int expand_column(lprec *lp, int col_nr, REAL *column, int *nzlist, REAL mult, int *maxabs)
{
  int     i, ie, j, maxidx, nzcount;
  REAL    value, maxval;
  MATrec  *mat = lp->matA;
  MATitem *matelem;

  /* Retrieve a column from the user data matrix A */
  maxval = 0;
  maxidx = -1;
  if(nzlist == NULL) {
    MEMCLEAR(column, lp->rows + 1);
    i = mat->col_end[col_nr - 1];
    ie = mat->col_end[col_nr];
    nzcount = i;
    for(matelem = mat->col_mat+i; i < ie; i++, matelem++) {
      j = (*matelem).row_nr;
      value = (*matelem).value;
      if(j > 0) {
        value *= mult;
        if(fabs(value) > maxval) {
          maxval = fabs(value);
          maxidx = j;
        }
      }
      column[j] = value;
    }
    nzcount = i - nzcount;

    /* Adjust the objective for phase 1 */
    if(*column != 0)
      nzcount--;    /* The value could possibly become zero after modifyOF1 */
    if(modifyOF1(lp, lp->rows+col_nr, column, mult))
      nzcount++;
  }
  else {
    nzcount = 0;
    i = mat->col_end[col_nr - 1];
    ie = mat->col_end[col_nr];
    matelem = mat->col_mat + i;

    /* Handle possible adjustment to zero OF value */
    if((*matelem).row_nr == 0) {
      value = (*matelem).value;
      matelem++;
      i++;
    }
    else
      value = 0;
    if(modifyOF1(lp, lp->rows+col_nr, &value, mult)) {
      nzcount++;
      nzlist[nzcount] = 0;
      column[nzcount] = value;
    }
    for(; i < ie; i++, matelem++) {
      j = (*matelem).row_nr;
      value = (*matelem).value * mult;
      nzcount++;
      nzlist[nzcount] = j;
      column[nzcount] = value;
      if(fabs(value) > maxval) {
        maxval = fabs(value);
        maxidx = nzcount;
      }
    }
  }

  if(maxabs != NULL)
    *maxabs = maxidx;
  return( nzcount );
}


/* Retrieve a column vector from the data matrix [1..rows, rows+1..rows+columns];
   needs __WINAPI call model since it may be called from BFPs */
int __WINAPI obtain_column(lprec *lp, int varin, REAL *pcol, int *nzlist, int *maxabs)
{
  int  colnz;
  REAL value;

  value = my_chsign(lp->is_lower[varin], -1);
  if(varin > lp->rows) {
    colnz = varin - lp->rows;
    colnz = expand_column(lp, colnz, pcol, nzlist, value, maxabs);
  }
  else
    colnz = singleton_column(lp, varin, pcol, nzlist, value, maxabs);

  return(colnz);
}


/* Collection of wrapper functions for XLI callbacks; these need
   a fixed and known call model, defined to be __WINAPI */
static lprec * __WINAPI _make_lp(int rows, int columns)
{
  return(lp_solve_make_lp(rows, columns));
}

static void __WINAPI _delete_lp(lprec *lp)
{
  lp_solve_delete_lp(lp);
}

static void __WINAPI _set_sense(lprec *lp, MYBOOL maximize)
{
  set_sense(lp, maximize);
}

static MYBOOL __WINAPI _set_lp_name(lprec *lp, char *lpname)
{
  return( set_lp_name(lp, lpname) );
}

static MYBOOL __WINAPI _set_row_name(lprec *lp, int varno, char *varname)
{
  return( set_row_name(lp, varno, varname) );
}

static MYBOOL __WINAPI _set_col_name(lprec *lp, int varno, char *varname)
{
  return( set_col_name(lp, varno, varname) );
}

static MYBOOL __WINAPI _add_columnex(lprec *lp, int count, REAL *column, int *rowno)
{
  return(add_columnex(lp, count, column, rowno));
}

static MYBOOL __WINAPI _set_constr_type(lprec *lp, int row, int con_type)
{
  return(lp_solve_set_constr_type(lp, row, con_type));
}

static MYBOOL __WINAPI _set_rh(lprec *lp, int row, REAL value)
{
  return(lp_solve_set_rh(lp, row, value));
}

static MYBOOL __WINAPI _set_rh_range(lprec *lp, int row, REAL deltavalue)
{
  return(set_rh_range(lp, row, deltavalue));
}

static MYBOOL __WINAPI _set_add_rowmode(lprec *lp, MYBOOL turnon)
{
  return(set_add_rowmode(lp, turnon));
}

static MYBOOL __WINAPI _set_obj_fnex(lprec *lp, int count, REAL *row, int *colno)
{
  return(set_obj_fnex(lp, count, row, colno));
}

static MYBOOL __WINAPI _add_constraintex(lprec *lp, int count, REAL *row, int *rowno, int constr_type, REAL rh)
{
  return(add_constraintex(lp, count, row, rowno, constr_type, rh));
}

static MYBOOL __WINAPI _set_upbo(lprec *lp, int column, REAL value)
{
  return(lp_solve_set_upbo(lp, column, value));
}

static MYBOOL __WINAPI _set_lowbo(lprec *lp, int column, REAL value)
{
  return(lp_solve_set_lowbo(lp, column, value));
}

static MYBOOL __WINAPI _set_free(lprec *lp, int column)
{
  return(set_free(lp, column));
}

static MYBOOL __WINAPI _set_int(lprec *lp, int column, MYBOOL must_be_int)
{
  return(lp_solve_set_int(lp, column, must_be_int));
}

static MYBOOL __WINAPI _set_semicont(lprec *lp, int column, MYBOOL must_be_sc)
{
  return(set_semicont(lp, column, must_be_sc));
}

/* GENERAL INVARIANT CALLBACK FUNCTIONS */
MYBOOL set_callbacks(lprec *lp)
{
  /* Assign API functions to lp structure (mainly for XLIs) */
  lp->add_column              = add_column;
  lp->add_columnex            = add_columnex;
  lp->add_constraint          = add_constraint;
  lp->add_constraintex        = add_constraintex;
  lp->add_lag_con             = add_lag_con;
  lp->add_SOS                 = add_SOS;
  lp->column_in_lp            = column_in_lp;
  lp->default_basis           = default_basis;
  lp->del_column              = del_column;
  lp->del_constraint          = del_constraint;
  lp->delete_lp               = lp_solve_delete_lp;
  lp->free_lp                 = free_lp;
  lp->get_anti_degen          = get_anti_degen;
  lp->get_basis               = get_basis;
  lp->get_basiscrash          = get_basiscrash;
  lp->get_bb_depthlimit       = get_bb_depthlimit;
  lp->get_bb_floorfirst       = get_bb_floorfirst;
  lp->get_bb_rule             = get_bb_rule;
  lp->get_bounds_tighter      = get_bounds_tighter;
  lp->get_break_at_value      = get_break_at_value;
  lp->get_col_name            = get_col_name;
  lp->get_column              = get_column;
  lp->get_constr_type         = get_constr_type;
  lp->get_constraints         = get_constraints;
  lp->get_dual_solution       = get_dual_solution;
  lp->get_epsb                = get_epsb;
  lp->get_epsd                = get_epsd;
  lp->get_epsel               = get_epsel;
  lp->get_epsilon             = get_epsilon;
  lp->get_epsperturb          = get_epsperturb;
  lp->get_epspivot            = get_epspivot;
  lp->get_improve             = get_improve;
  lp->get_infinite            = get_infinite;
  lp->get_lambda              = get_lambda;
  lp->get_lowbo               = get_lowbo;
  lp->get_lp_index            = get_lp_index;
  lp->get_lp_name             = get_lp_name;
  lp->get_Lrows               = get_Lrows;
  lp->get_mat                 = get_mat;
  lp->get_max_level           = get_max_level;
  lp->get_maxpivot            = get_maxpivot;
  lp->get_mip_gap             = get_mip_gap;
  lp->get_multiprice          = get_multiprice;
  lp->get_Ncolumns            = get_Ncolumns;
  lp->get_negrange            = get_negrange;
  lp->get_Norig_columns       = get_Norig_columns;
  lp->get_Norig_rows          = get_Norig_rows;
  lp->get_Nrows               = get_Nrows;
  lp->get_obj_bound           = get_obj_bound;
  lp->get_objective           = get_objective;
  lp->get_orig_index          = get_orig_index;
  lp->get_origcol_name        = get_origcol_name;
  lp->get_origrow_name        = get_origrow_name;
  lp->get_partialprice        = get_partialprice;
  lp->get_pivoting            = get_pivoting;
  lp->get_presolve            = get_presolve;
  lp->get_primal_solution     = get_primal_solution;
  lp->get_print_sol           = get_print_sol;
  lp->get_PseudoCosts         = get_PseudoCosts;
  lp->get_ptr_constraints     = get_ptr_constraints;
  lp->get_ptr_dual_solution   = get_ptr_dual_solution;
  lp->get_ptr_lambda          = get_ptr_lambda;
  lp->get_ptr_primal_solution = get_ptr_primal_solution;
  lp->get_ptr_sensitivity_obj = get_ptr_sensitivity_obj;
  lp->get_ptr_sensitivity_rhs = get_ptr_sensitivity_rhs;
  lp->get_ptr_variables       = get_ptr_variables;
  lp->get_rh                  = get_rh;
  lp->get_rh_range            = get_rh_range;
  lp->get_row                 = get_row;
  lp->get_row_name            = get_row_name;
  lp->get_scalelimit          = get_scalelimit;
  lp->get_scaling             = get_scaling;
  lp->get_sensitivity_obj     = get_sensitivity_obj;
  lp->get_sensitivity_rhs     = get_sensitivity_rhs;
  lp->get_simplextype         = get_simplextype;
  lp->get_solutioncount       = get_solutioncount;
  lp->get_solutionlimit       = get_solutionlimit;
  lp->get_splitnegvars        = get_splitnegvars;
  lp->get_statustext          = get_statustext;
  lp->get_timeout             = get_timeout;
  lp->get_total_iter          = get_total_iter;
  lp->get_total_nodes         = get_total_nodes;
  lp->get_upbo                = get_upbo;
  lp->get_var_branch          = get_var_branch;
  lp->get_var_dualresult      = get_var_dualresult;
  lp->get_var_primalresult    = get_var_primalresult;
  lp->get_var_priority        = get_var_priority;
  lp->get_variables           = get_variables;
  lp->get_verbose             = get_verbose;
  lp->get_working_objective   = get_working_objective;
  lp->has_BFP                 = has_BFP;
  lp->has_XLI                 = has_XLI;
  lp->is_add_rowmode          = is_add_rowmode;
  lp->is_anti_degen           = is_anti_degen;
  lp->is_binary               = is_binary;
  lp->is_break_at_first       = is_break_at_first;
  lp->is_constr_type          = is_constr_type;
  lp->is_debug                = is_debug;
  lp->is_feasible             = is_feasible;
  lp->is_free                 = is_free;
  lp->is_int                  = is_int;
  lp->is_integerscaling       = is_integerscaling;
  lp->is_lag_trace            = is_lag_trace;
  lp->is_maxim                = is_maxim;
  lp->is_nativeBFP            = is_nativeBFP;
  lp->is_nativeXLI            = is_nativeXLI;
  lp->is_negative             = is_negative;
  lp->is_piv_mode             = is_piv_mode;
  lp->is_piv_rule             = is_piv_rule;
  lp->is_presolve             = is_presolve;
  lp->is_scalemode            = is_scalemode;
  lp->is_scaletype            = is_scaletype;
  lp->is_semicont             = is_semicont;
  lp->is_SOS_var              = is_SOS_var;
  lp->is_trace                = is_trace;
  lp->lp_solve_version        = lp_solve_version;
  lp->make_lp                 = lp_solve_make_lp;
  lp->print_constraints       = print_constraints;
  lp->print_debugdump         = print_debugdump;
  lp->print_duals             = print_duals;
  lp->print_lp                = lp_solve_print_lp;
  lp->print_objective         = print_objective;
  lp->print_scales            = print_scales;
  lp->print_solution          = print_solution;
  lp->print_str               = print_str;
  lp->print_tableau           = print_tableau;
  lp->put_abortfunc           = put_abortfunc;
  lp->put_logfunc             = put_logfunc;
  lp->put_msgfunc             = put_msgfunc;
  lp->read_lp                 = read_lp;
  lp->read_LP                 = read_LP;
  lp->read_lpt                = read_lpt;
  lp->read_LPT                = read_LPT;
  lp->read_mps                = read_mps;
  lp->read_MPS                = read_MPS;
  lp->read_freemps            = read_freemps;
  lp->read_freeMPS            = read_freeMPS;
  lp->read_XLI                = read_XLI;
  lp->reset_basis             = reset_basis;
  lp->set_add_rowmode         = set_add_rowmode;
  lp->set_anti_degen          = set_anti_degen;
  lp->set_basis               = set_basis;
  lp->set_basiscrash          = set_basiscrash;
  lp->set_bb_depthlimit       = set_bb_depthlimit;
  lp->set_bb_floorfirst       = set_bb_floorfirst;
  lp->set_bb_rule             = set_bb_rule;
  lp->set_BFP                 = set_BFP;
  lp->set_binary              = set_binary;
  lp->set_bounds              = set_bounds;
  lp->set_bounds_tighter      = set_bounds_tighter;
  lp->set_break_at_first      = set_break_at_first;
  lp->set_break_at_value      = set_break_at_value;
  lp->set_col_name            = set_col_name;
  lp->set_constr_type         = lp_solve_set_constr_type;
  lp->set_debug               = set_debug;
  lp->set_epsb                = set_epsb;
  lp->set_epsd                = set_epsd;
  lp->set_epsel               = set_epsel;
  lp->set_epsilon             = set_epsilon;
  lp->set_epsperturb          = set_epsperturb;
  lp->set_epspivot            = set_epspivot;
  lp->set_free                = set_free;
  lp->set_improve             = set_improve;
  lp->set_infinite            = set_infinite;
  lp->set_int                 = lp_solve_set_int;
  lp->set_lag_trace           = set_lag_trace;
  lp->set_lowbo               = lp_solve_set_lowbo;
  lp->set_lp_name             = set_lp_name;
  lp->set_mat                 = lp_solve_set_mat;
  lp->set_maxim               = lp_solve_set_maxim;
  lp->set_maxpivot            = set_maxpivot;
  lp->set_minim               = lp_solve_set_minim;
  lp->set_mip_gap             = set_mip_gap;
  lp->set_multiprice          = set_multiprice;
  lp->set_negrange            = set_negrange;
  lp->set_obj                 = set_obj;
  lp->set_obj_bound           = set_obj_bound;
  lp->set_obj_fn              = set_obj_fn;
  lp->set_obj_fnex            = set_obj_fnex;
  lp->set_outputfile          = set_outputfile;
  lp->set_outputstream        = set_outputstream;
  lp->set_partialprice        = set_partialprice;
  lp->set_pivoting            = set_pivoting;
  lp->set_preferdual          = set_preferdual;
  lp->set_presolve            = set_presolve;
  lp->set_print_sol           = set_print_sol;
  lp->set_PseudoCosts         = set_PseudoCosts;
  lp->set_rh                  = lp_solve_set_rh;
  lp->set_rh_range            = set_rh_range;
  lp->set_rh_vec              = set_rh_vec;
  lp->set_row_name            = set_row_name;
  lp->set_scalelimit          = set_scalelimit;
  lp->set_scaling             = set_scaling;
  lp->set_semicont            = set_semicont;
  lp->set_sense               = set_sense;
  lp->set_simplextype         = set_simplextype;
  lp->set_solutionlimit       = set_solutionlimit;
  lp->set_splitnegvars        = set_splitnegvars;
  lp->set_timeout             = set_timeout;
  lp->set_trace               = set_trace;
  lp->set_upbo                = lp_solve_set_upbo;
  lp->set_var_branch          = set_var_branch;
  lp->set_var_weights         = set_var_weights;
  lp->set_verbose             = set_verbose;
  lp->set_XLI                 = set_XLI;
  lp->solve                   = lp_solve_solve;
  lp->str_add_column          = str_add_column;
  lp->str_add_constraint      = str_add_constraint;
  lp->str_add_lag_con         = str_add_lag_con;
  lp->str_set_obj_fn          = str_set_obj_fn;
  lp->str_set_rh_vec          = str_set_rh_vec;
  lp->time_elapsed            = time_elapsed;
  lp->unscale                 = unscale;
  lp->write_lp                = write_lp;
  lp->write_lpt               = write_lpt;
  lp->write_mps               = write_mps;
  lp->write_freemps           = write_freemps;
  lp->write_XLI               = write_XLI;

  /* Utility functions (mainly for BFPs) */
  lp->userabort        = userabort;
  lp->report           = report;
  lp->get_nonzeros     = get_nonzeros;
  lp->set_basisvar     = setBasisVar;
  lp->get_lpcolumn     = obtain_column;
  lp->get_basiscolumn  = getBasisColumn;
  lp->modifyOF1        = modifyOF1;
  lp->invert           = invert;

  return( TRUE );
}

/* SUPPORT FUNCTION FOR BASIS FACTORIZATION PACKAGES */
MYBOOL __WINAPI has_BFP(lprec *lp)
{
  return( is_nativeBFP(lp)
#if LoadInverseLib == TRUE
       || (MYBOOL) (lp->hBFP != NULL)
#endif
        );
}

MYBOOL __WINAPI is_nativeBFP(lprec *lp)
{
#ifdef ExcludeNativeInverse
  return( FALSE );
#elif LoadInverseLib == TRUE
  return( (MYBOOL) (lp->hBFP == NULL) );
#else
  return( TRUE );
#endif
}

MYBOOL __WINAPI set_BFP(lprec *lp, char *filename)
/* (Re)mapping of basis factorization variant methods is done here */
{
  MYBOOL result = TRUE;

#if LoadInverseLib == TRUE
  if(lp->hBFP != NULL) {
  #ifdef WIN32
    FreeLibrary(lp->hBFP);
  #else
    dlclose(lp->hBFP);
  #endif
    lp->hBFP = NULL;
  }
#endif

  if(filename == NULL) {
    if(!is_nativeBFP(lp))
      return( FALSE );
#ifndef ExcludeNativeInverse
    lp->bfp_name = bfp_name;
    lp->bfp_compatible = bfp_compatible;
    lp->bfp_free = bfp_free;
    lp->bfp_resize = bfp_resize;
    lp->bfp_nonzeros = bfp_nonzeros;
    lp->bfp_memallocated = bfp_memallocated;
    lp->bfp_restart = bfp_restart;
    lp->bfp_mustrefactorize = bfp_mustrefactorize;
    lp->bfp_preparefactorization = bfp_preparefactorization;
    lp->bfp_factorize = bfp_factorize;
    lp->bfp_finishupdate = bfp_finishupdate;
    lp->bfp_ftran_normal = bfp_ftran_normal;
    lp->bfp_ftran_prepare = bfp_ftran_prepare;
    lp->bfp_btran_normal = bfp_btran_normal;
    lp->bfp_status = bfp_status;
    lp->bfp_indexbase = bfp_indexbase;
    lp->bfp_rowoffset = bfp_rowoffset;
    lp->bfp_pivotmax = bfp_pivotmax;
    lp->bfp_init = bfp_init;
    lp->bfp_pivotalloc = bfp_pivotalloc;
    lp->bfp_colcount = bfp_colcount;
    lp->bfp_canresetbasis = bfp_canresetbasis;
    lp->bfp_finishfactorization = bfp_finishfactorization;
    lp->bfp_prepareupdate = bfp_prepareupdate;
    lp->bfp_pivotRHS = bfp_pivotRHS;
    lp->bfp_btran_double = bfp_btran_double;
    lp->bfp_efficiency = bfp_efficiency;
    lp->bfp_pivotvector = bfp_pivotvector;
    lp->bfp_pivotcount = bfp_pivotcount;
    lp->bfp_refactcount = bfp_refactcount;
    lp->bfp_isSetI = bfp_isSetI;
#endif
  }
  else {
#if LoadInverseLib == TRUE
  #ifdef WIN32
   /* Get a handle to the Windows DLL module. */
    lp->hBFP = LoadLibrary(filename);

   /* If the handle is valid, try to get the function addresses. */
    if(lp->hBFP == NULL) {
      set_BFP(lp, NULL);
      result = FALSE;
    }
    else {
      lp->bfp_compatible           = (BFPbool_lpintint *)
                                      GetProcAddress(lp->hBFP, "bfp_compatible");
      if((lp->bfp_compatible != NULL) && lp->bfp_compatible(lp, BFPVERSION, MAJORVERSION)) {

      lp->bfp_name                 = (BFPchar *)
                                      GetProcAddress(lp->hBFP, "bfp_name");
      lp->bfp_free                 = (BFP_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_free");
      lp->bfp_resize               = (BFP_lpint *)
                                      GetProcAddress(lp->hBFP, "bfp_resize");
      lp->bfp_nonzeros             = (BFPint_lpbool *)
                                      GetProcAddress(lp->hBFP, "bfp_nonzeros");
      lp->bfp_memallocated         = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_memallocated");
      lp->bfp_restart              = (BFPbool_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_restart");
      lp->bfp_mustrefactorize      = (BFPbool_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_mustrefactorize");
      lp->bfp_preparefactorization = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_preparefactorization");
      lp->bfp_factorize            = (BFPint_lpintintbool *)
                                      GetProcAddress(lp->hBFP, "bfp_factorize");
      lp->bfp_finishupdate         = (BFPbool_lpbool *)
                                      GetProcAddress(lp->hBFP, "bfp_finishupdate");
      lp->bfp_ftran_normal         = (BFP_lprealint *)
                                      GetProcAddress(lp->hBFP, "bfp_ftran_normal");
      lp->bfp_ftran_prepare        = (BFP_lprealint *)
                                      GetProcAddress(lp->hBFP, "bfp_ftran_prepare");
      lp->bfp_btran_normal         = (BFP_lprealint *)
                                      GetProcAddress(lp->hBFP, "bfp_btran_normal");
      lp->bfp_status               = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_status");
      lp->bfp_indexbase            = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_indexbase");
      lp->bfp_rowoffset            = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_rowoffset");
      lp->bfp_pivotmax             = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_pivotmax");
      lp->bfp_init                 = (BFPbool_lpintint *)
                                      GetProcAddress(lp->hBFP, "bfp_init");
      lp->bfp_pivotalloc           = (BFP_lpint *)
                                      GetProcAddress(lp->hBFP, "bfp_pivotalloc");
      lp->bfp_colcount             = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_colcount");
      lp->bfp_canresetbasis        = (BFPbool_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_canresetbasis");
      lp->bfp_finishfactorization  = (BFP_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_finishfactorization");
      lp->bfp_prepareupdate        = (BFPlreal_lpintintreal *)
                                      GetProcAddress(lp->hBFP, "bfp_prepareupdate");
      lp->bfp_pivotRHS             = (BFPreal_lplrealreal *)
                                      GetProcAddress(lp->hBFP, "bfp_pivotRHS");
      lp->bfp_btran_double         = (BFP_lprealintrealint *)
                                      GetProcAddress(lp->hBFP, "bfp_btran_double");
      lp->bfp_efficiency           = (BFPreal_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_efficiency");
      lp->bfp_pivotvector          = (BFPrealp_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_pivotvector");
      lp->bfp_pivotcount           = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_pivotcount");
      lp->bfp_refactcount          = (BFPint_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_refactcount");
      lp->bfp_isSetI               = (BFPbool_lp *)
                                      GetProcAddress(lp->hBFP, "bfp_isSetI");
      }
      else
        result = FALSE;
    }
  #else
   /* First standardize UNIX .SO library name format. */
    char bfpname[260], *ptr;

    strcpy(bfpname, filename);
    if((ptr = strrchr(filename, '/')) == NULL)
      ptr = filename;
    else
      ptr++;
    bfpname[(int) (ptr - filename)] = 0;
    if(strncmp(ptr, "lib", 3))
      strcat(bfpname, "lib");
    strcat(bfpname, ptr);
    if(strcmp(bfpname + strlen(bfpname) - 3, ".so"))
      strcat(bfpname, ".so");

   /* Get a handle to the module. */
    lp->hBFP = dlopen(bfpname, RTLD_LAZY);

   /* If the handle is valid, try to get the function addresses. */
    if(lp->hBFP == NULL) {
      set_BFP(lp, NULL);
      result = FALSE;
    }
    else {
      lp->bfp_compatible           = (BFPbool_lpintint *)
                                      dlsym(lp->hBFP, "bfp_compatible");
      if((lp->bfp_compatible != NULL) && lp->bfp_compatible(lp, BFPVERSION, MAJORVERSION)) {

      lp->bfp_name                 = (BFPchar *)
                                      dlsym(lp->hBFP, "bfp_name");
      lp->bfp_free                 = (BFP_lp *)
                                      dlsym(lp->hBFP, "bfp_free");
      lp->bfp_resize               = (BFP_lpint *)
                                      dlsym(lp->hBFP, "bfp_resize");
      lp->bfp_nonzeros             = (BFPint_lpbool *)
                                      dlsym(lp->hBFP, "bfp_nonzeros");
      lp->bfp_memallocated         = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_memallocated");
      lp->bfp_restart              = (BFPbool_lp *)
                                      dlsym(lp->hBFP, "bfp_restart");
      lp->bfp_mustrefactorize      = (BFPbool_lp *)
                                      dlsym(lp->hBFP, "bfp_mustrefactorize");
      lp->bfp_preparefactorization = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_preparefactorization");
      lp->bfp_factorize            = (BFPint_lpintintbool *)
                                      dlsym(lp->hBFP, "bfp_factorize");
      lp->bfp_finishupdate         = (BFPbool_lpbool *)
                                      dlsym(lp->hBFP, "bfp_finishupdate");
      lp->bfp_ftran_normal         = (BFP_lprealint *)
                                      dlsym(lp->hBFP, "bfp_ftran_normal");
      lp->bfp_ftran_prepare        = (BFP_lprealint *)
                                      dlsym(lp->hBFP, "bfp_ftran_prepare");
      lp->bfp_btran_normal         = (BFP_lprealint *)
                                      dlsym(lp->hBFP, "bfp_btran_normal");
      lp->bfp_status               = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_status");
      lp->bfp_indexbase            = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_indexbase");
      lp->bfp_rowoffset            = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_rowoffset");
      lp->bfp_pivotmax             = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_pivotmax");
      lp->bfp_init                 = (BFPbool_lpintint *)
                                      dlsym(lp->hBFP, "bfp_init");
      lp->bfp_pivotalloc           = (BFP_lpint *)
                                      dlsym(lp->hBFP, "bfp_pivotalloc");
      lp->bfp_colcount             = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_colcount");
      lp->bfp_canresetbasis        = (BFPbool_lp *)
                                      dlsym(lp->hBFP, "bfp_canresetbasis");
      lp->bfp_finishfactorization  = (BFP_lp *)
                                      dlsym(lp->hBFP, "bfp_finishfactorization");
      lp->bfp_prepareupdate        = (BFPlreal_lpintintreal *)
                                      dlsym(lp->hBFP, "bfp_prepareupdate");
      lp->bfp_pivotRHS             = (BFPreal_lplrealreal *)
                                      dlsym(lp->hBFP, "bfp_pivotRHS");
      lp->bfp_btran_double         = (BFP_lprealintrealint *)
                                      dlsym(lp->hBFP, "bfp_btran_double");
      lp->bfp_efficiency           = (BFPreal_lp *)
                                      dlsym(lp->hBFP, "bfp_efficiency");
      lp->bfp_pivotvector          = (BFPrealp_lp *)
                                      dlsym(lp->hBFP, "bfp_pivotvector");
      lp->bfp_pivotcount           = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_pivotcount");
      lp->bfp_refactcount          = (BFPint_lp *)
                                      dlsym(lp->hBFP, "bfp_refactcount");
      lp->bfp_isSetI               = (BFPbool_lp *)
                                      dlsym(lp->hBFP, "bfp_isSetI");
      }
      else
        result = FALSE;
    }
  #endif
#endif
    /* Do validation */
    if(result &&
       ((lp->bfp_name == NULL) ||
        (lp->bfp_compatible == NULL) ||
        (lp->bfp_free == NULL) ||
        (lp->bfp_resize == NULL) ||
        (lp->bfp_nonzeros == NULL) ||
        (lp->bfp_memallocated == NULL) ||
        (lp->bfp_restart == NULL) ||
        (lp->bfp_mustrefactorize == NULL) ||
        (lp->bfp_preparefactorization == NULL) ||
        (lp->bfp_factorize == NULL) ||
        (lp->bfp_finishupdate == NULL) ||
        (lp->bfp_ftran_normal == NULL) ||
        (lp->bfp_ftran_prepare == NULL) ||
        (lp->bfp_btran_normal == NULL) ||
        (lp->bfp_status == NULL) ||
        (lp->bfp_indexbase == NULL) ||
        (lp->bfp_rowoffset == NULL) ||
        (lp->bfp_pivotmax == NULL) ||
        (lp->bfp_init == NULL) ||
        (lp->bfp_pivotalloc == NULL) ||
        (lp->bfp_colcount == NULL) ||
        (lp->bfp_canresetbasis == NULL) ||
        (lp->bfp_finishfactorization == NULL) ||
        (lp->bfp_prepareupdate == NULL) ||
        (lp->bfp_pivotRHS == NULL) ||
        (lp->bfp_btran_double == NULL) ||
        (lp->bfp_efficiency == NULL) ||
        (lp->bfp_pivotvector == NULL) ||
        (lp->bfp_pivotcount == NULL) ||
        (lp->bfp_refactcount == NULL) ||
        (lp->bfp_isSetI == NULL)
       )) {
      set_BFP(lp, NULL);
      result = FALSE;
    }
  }
  return( result );
}


/* External language interface routines */
/* DON'T MODIFY */
lprec * __WINAPI read_XLI(char *xliname, char *modelname, char *dataname, char *options, int verbose)
{
  lprec *lp;

  lp = lp_solve_make_lp(0, 0);
  if(lp != NULL) {
    if(!set_XLI(lp, xliname)) {
      free_lp(&lp);
      printf("No valid XLI package selected or available");
    }
    else {
      lp->source_is_file = TRUE;
      lp->verbose = verbose;
      if(!lp->xli_readmodel(lp, modelname, dataname, options, verbose))
        free_lp(&lp);
    }
  }
  return( lp );
}

MYBOOL __WINAPI write_XLI(lprec *lp, char *filename, char *options, MYBOOL results)
{
  return( has_XLI(lp) && lp->xli_writemodel(lp, filename, options, results) );
}

MYBOOL __WINAPI has_XLI(lprec *lp)
{
  return( is_nativeXLI(lp)
#if LoadLanguageLib == TRUE
       || (MYBOOL) (lp->hXLI != NULL)
#endif
        );
}

MYBOOL __WINAPI is_nativeXLI(lprec *lp)
{
#ifdef ExcludeNativeLanguage
  return( FALSE );
#elif LoadLanguageLib == TRUE
  return( (MYBOOL) (lp->hXLI == NULL) );
#else
  return( TRUE );
#endif
}

MYBOOL __WINAPI set_XLI(lprec *lp, char *filename)
/* (Re)mapping of external language interface variant methods is done here */
{
  MYBOOL result = TRUE;

#if LoadLanguageLib == TRUE
  if(lp->hXLI != NULL) {
  #ifdef WIN32
    FreeLibrary(lp->hXLI);
  #else
    dlclose(lp->hXLI);
  #endif
    lp->hXLI = NULL;
  }
#endif

  if(filename == NULL) {
    if(!is_nativeXLI(lp))
      return( FALSE );
#ifndef ExcludeNativeLanguage
    lp->xli_name = xli_name;
    lp->xli_compatible = xli_compatible;
    lp->xli_readmodel = xli_readmodel;
    lp->xli_writemodel = xli_writemodel;
#endif
  }
  else {
#if LoadLanguageLib == TRUE
  #ifdef WIN32
   /* Get a handle to the Windows DLL module. */
    lp->hXLI = LoadLibrary(filename);

   /* If the handle is valid, try to get the function addresses. */
    if(lp->hXLI == NULL) {
      set_XLI(lp, NULL);
      result = FALSE;
    }
    else {
      lp->xli_compatible           = (XLIbool_lpintint *)
                                      GetProcAddress(lp->hXLI, "xli_compatible");
      if((lp->xli_compatible != NULL) && lp->xli_compatible(lp, XLIVERSION, MAJORVERSION)) {

        lp->xli_name                 = (XLIchar *)
                                        GetProcAddress(lp->hXLI, "xli_name");
        lp->xli_readmodel            = (XLIbool_lpcharcharcharint *)
                                        GetProcAddress(lp->hXLI, "xli_readmodel");
        lp->xli_writemodel           = (XLIbool_lpcharcharbool *)
                                        GetProcAddress(lp->hXLI, "xli_writemodel");
      }
      else
        result = FALSE;
    }
  #else
   /* First standardize UNIX .SO library name format. */
    char xliname[260], *ptr;

    strcpy(xliname, filename);
    if((ptr = strrchr(filename, '/')) == NULL)
      ptr = filename;
    else
      ptr++;
    xliname[(int) (ptr - filename)] = 0;
    if(strncmp(ptr, "lib", 3))
      strcat(xliname, "lib");
    strcat(xliname, ptr);
    if(strcmp(xliname + strlen(xliname) - 3, ".so"))
      strcat(xliname, ".so");

   /* Get a handle to the module. */
    lp->hXLI = dlopen(xliname, RTLD_LAZY);

   /* If the handle is valid, try to get the function addresses. */
    if(lp->hXLI == NULL) {
      set_XLI(lp, NULL);
      result = FALSE;
    }
    else {
      lp->xli_compatible           = (XLIbool_lpintint *)
                                      dlsym(lp->hXLI, "xli_compatible");
      if((lp->xli_compatible != NULL) && lp->xli_compatible(lp, XLIVERSION, MAJORVERSION)) {

        lp->xli_name                 = (XLIchar *)
                                        dlsym(lp->hXLI, "xli_name");
        lp->xli_readmodel            = (XLIbool_lpcharcharcharint *)
                                        dlsym(lp->hXLI, "xli_readmodel");
        lp->xli_writemodel           = (XLIbool_lpcharcharbool *)
                                        dlsym(lp->hXLI, "xli_writemodel");
      }
      else
        result = FALSE;
    }
  #endif
#endif
    /* Do validation */
    if(result &&
       ((lp->xli_name == NULL) ||
        (lp->xli_compatible == NULL) ||
        (lp->xli_readmodel == NULL) ||
        (lp->xli_writemodel == NULL)
       )) {
      set_XLI(lp, NULL);
      result = FALSE;
    }
  }
  return( result );
}


int __WINAPI getBasisColumn(lprec *lp, int j, int rn[], double bj[])
/* This routine returns sparse vectors for all basis
   columns, including the OF dummy (index 0) and slack columns */
{
  int   k, matbase = lp->bfp_indexbase(lp);

 /* Convert index of slack and user columns */
  j -= lp->bfp_rowoffset(lp);
  if((j > 0) && !lp->bfp_isSetI(lp))
    j = lp->var_basic[j];

 /* Process OF dummy and slack columns (always at lower bound) */
  if(j <= lp->rows) {
    rn[1] = j + matbase;
    bj[1] = 1.0;
    k = 1;
  }
 /* Process user columns (negated if at lower bound) */
  else {
    k = obtain_column(lp, j, bj, rn, NULL);
    if(matbase != 0)
      for(j = 1; j <= k; j++)
        rn[j] += matbase;
  }

  return( k );
}

MYBOOL __WINAPI get_primal_solution(lprec *lp, REAL *pv)
{
  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_primal_solution: Not a valid basis");
    return(FALSE);
  }

  MEMCOPY(pv, lp->best_solution, lp->sum + 1);
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_primal_solution(lprec *lp, REAL **pv)
{
  *pv = lp->best_solution;
  return(TRUE);
}

MYBOOL __WINAPI get_dual_solution(lprec *lp, REAL *rc)
{
  REAL *duals;
  MYBOOL ret;

  if(!lp->basis_valid) {
    report(lp, CRITICAL, "get_dual_solution: Not a valid basis");
    return(FALSE);
  }

  ret = get_ptr_sensitivity_rhs(lp, &duals, NULL, NULL);

  if(ret)
    MEMCOPY(rc, duals - 1, lp->sum + 1);
  return(ret);
}

MYBOOL __WINAPI get_ptr_dual_solution(lprec *lp, REAL **rc)
{
  MYBOOL ret;

  ret = get_ptr_sensitivity_rhs(lp, rc, NULL, NULL);
  if(ret)
    (*rc)--;
  return(ret);
}

MYBOOL __WINAPI get_lambda(lprec *lp, REAL *lambda)
{
  if(!lp->basis_valid || (get_Lrows(lp) == 0)) {
    report(lp, CRITICAL, "get_lambda: Not a valid basis");
    return(FALSE);
  }

  MEMCOPY(lambda, lp->lambda+1, get_Lrows(lp));
  return(TRUE);
}

MYBOOL __WINAPI get_ptr_lambda(lprec *lp, REAL **lambda)
{
  *lambda = lp->lambda;
  return(TRUE);
}

int __WINAPI get_orig_index(lprec *lp, int lp_index)
{
  return(lp->varmap_locked ? lp->var_to_orig[lp_index] : lp_index);
}
int __WINAPI get_lp_index(lprec *lp, int orig_index)
{
  return(lp->varmap_locked ? lp->orig_to_var[orig_index] : orig_index);
}

MYBOOL __WINAPI is_feasible(lprec *lp, REAL *values, REAL threshold)
/* Recommend to use threshold = lp->epspivot */
{
  int     i, j, elmnr, ie;
  REAL    *this_rhs, dist;
  MATrec  *mat = lp->matA;
  MATitem *matelem;

  for(i = lp->rows + 1; i <= lp->sum; i++) {
    if(values[i - lp->rows] < unscaled_value(lp, lp->orig_lowbo[i], i)
       || values[i - lp->rows] > unscaled_value(lp, lp->orig_upbo[i], i)) {
      if(!((lp->var_is_sc[i - lp->rows]>0) && (values[i - lp->rows]==0)))
        return(FALSE);
    }
  }

  allocREAL(lp, &this_rhs, lp->rows + 1, TRUE);
  for(j = 1; j <= lp->columns; j++) {
    elmnr = mat->col_end[j - 1];
    ie = mat->col_end[j];
    for(matelem = mat->col_mat + elmnr;
        elmnr < ie; elmnr++, matelem++) {
      i = (*matelem).row_nr;
      this_rhs[i] += unscaled_mat(lp, (*matelem).value, i, j);
    }
  }
  for(i = 1; i <= lp->rows; i++) {
    dist = lp->orig_rh[i] - this_rhs[i];
    my_roundzero(dist, threshold); /* Converted to variable by KE 20030722 */
    if((lp->orig_upbo[i] == 0 && dist != 0) || dist < 0) {
      FREE(this_rhs);
      return(FALSE);
    }
  }
  FREE(this_rhs);
  return(TRUE);
}


int __WINAPI column_in_lp(lprec *lp, REAL *testcolumn)
{
  int  i, j, colnr = 0;
  int  nz, ident = 1;
  REAL value;

  for(nz = 0, i = 0; i <= lp->rows; i++)
    if(fabs(testcolumn[i]) > lp->epsvalue) nz++;

  for(i = 1; (i <= lp->columns) && (ident); i++) {
    ident = nz;
    for(j = lp->matA->col_end[i - 1]; (j < lp->matA->col_end[i]) && (ident >= 0); j++, ident--) {
      value = lp->matA->col_mat[j].value;
      if(is_chsign(lp, lp->matA->col_mat[j].row_nr))
        value = my_flipsign(value);
      value = unscaled_mat(lp, value, lp->matA->col_mat[j].row_nr, i);
      value -= testcolumn[lp->matA->col_mat[j].row_nr];
      if(fabs(value) > lp->epsvalue)
        ident = -1;
    }
    if(ident == 0)
      colnr = i;
  }
  return( colnr );
}


MYBOOL __WINAPI set_lp_name(lprec *lp, char *name)
{
  if (name == NULL) {
    FREE(lp->lp_name);
    lp->lp_name = NULL;
  }
  else {
    allocCHAR(lp, &lp->lp_name, (int) (strlen(name) + 1), AUTOMATIC);
    strcpy(lp->lp_name, name);
  }
  return(TRUE);
}

char * __WINAPI get_lp_name(lprec *lp)
{
  return(lp->lp_name);
}

STATIC MYBOOL init_rowcol_names(lprec *lp)
{
  if(!lp->names_used) {
    lp->row_name = (hashelem **) calloc(lp->rows_alloc + 1, sizeof(*lp->row_name));
    lp->col_name = (hashelem **) calloc(lp->columns_alloc + 1, sizeof(*lp->col_name));
    lp->rowname_hashtab = create_hash_table(lp->rows_alloc + 1, 0);
    lp->colname_hashtab = create_hash_table(lp->columns_alloc + 1, 1);
    lp->names_used = TRUE;
  }
  return(TRUE);
}

MYBOOL rename_var(lprec *lp, int varindex, char *new_name, hashelem **list, hashtable **ht)
{
  hashelem *hp;
  MYBOOL   newitem;

  hp = list[varindex];
  newitem = (MYBOOL) (hp == NULL);
  if(newitem)
    hp = puthash(new_name, varindex, list, *ht);
  else {
    hashtable *newht, *oldht;

    allocCHAR(lp, &hp->name, (int) (strlen(new_name) + 1), AUTOMATIC);
    strcpy(hp->name, new_name);
    oldht = *ht;
    newht = copy_hash_table(oldht, list, oldht->size);
    *ht = newht;
    free_hash_table(oldht);
  }
  return(newitem);
}

MYBOOL __WINAPI set_row_name(lprec *lp, int row, char *new_name)
{
#ifdef Paranoia
  if(row < 0 || row > lp->rows+1) {
    report(lp, IMPORTANT, "set_row_name: Row %d out of range", row);
    return(FALSE);
  }
#endif

  /* Prepare for a new row */
  if((row > lp->rows) && !append_rows(lp, row-lp->rows))
    return( FALSE );
  if(!lp->names_used) {
    if(!init_rowcol_names(lp))
      return(FALSE);
  }
  rename_var(lp, row, new_name, lp->row_name, &lp->rowname_hashtab);

  return(TRUE);
}

char * __WINAPI get_row_name(lprec *lp, int row)
{
#ifdef Paranoia
  if(row < 0 || row > lp->rows+1) {
    report(lp, IMPORTANT, "get_row_name: Row %d out of range", row);
    return(NULL);
  }
#endif
  if((lp->var_to_orig != NULL) && lp->wasPresolved) {
    if(lp->var_to_orig[row] == 0)
      row = -row;
    else
      row = lp->var_to_orig[row];
  }
  return( get_origrow_name(lp, row) );
}

char * __WINAPI get_origrow_name(lprec *lp, int row)
{
  MYBOOL newrow;
  static char name[50];
  char   *ptr;

  newrow = (MYBOOL) (row < 0);
  row = abs(row);
#ifdef Paranoia
  if(((lp->var_to_orig == NULL) && newrow) ||
     (row > MAX(lp->rows, lp->orig_rows))) {
    report(lp, IMPORTANT, "get_origrow_name: Row %d out of range", row);
    return(NULL);
  }
#endif

  if(lp->names_used && (lp->row_name[row] != NULL) &&
                            (lp->row_name[row]->name != NULL)) {
#ifdef Paranoia
    if(lp->row_name[row]->index != row)
      report(lp, SEVERE, "get_origrow_name: Inconsistent row ordinal %d vs %d\n",
                         row, lp->row_name[row]->index);
#endif
    ptr = lp->row_name[row]->name;
  }
  else {
    if(newrow)
      sprintf(name, ROWNAMEMASK2, row);
    else
      sprintf(name, ROWNAMEMASK, row);
    ptr = name;
  }
  return(ptr);
}

MYBOOL __WINAPI set_col_name(lprec *lp, int column, char *new_name)
{
#ifdef Paranoia
  if(column < 1 || column > lp->columns+1) {
    report(lp, IMPORTANT, "set_col_name: Column %d out of range", column);
  }
#endif

  if((column > lp->columns) && !append_columns(lp, column-lp->columns))
    return(FALSE);

  if(!lp->names_used)
    init_rowcol_names(lp);
  rename_var(lp, column, new_name, lp->col_name, &lp->colname_hashtab);

  return(TRUE);
}

char * __WINAPI get_col_name(lprec *lp, int column)
{
#ifdef Paranoia
  if((column < 1) || (column > lp->columns+1)) {
    report(lp, IMPORTANT, "get_col_name: Column %d out of range", column);
    return(NULL);
  }
#endif
  if((lp->var_to_orig != NULL) && lp->wasPresolved) {
    if(lp->var_to_orig[lp->rows + column] == 0)
      column = -column;
    else
      column = lp->var_to_orig[lp->rows + column];
  }
  return( get_origcol_name(lp, column) );
}

char * __WINAPI get_origcol_name(lprec *lp, int column)
{
  MYBOOL newcol;
  char   *ptr;
  static char name[50];

  newcol = (MYBOOL) (column < 0);
  column = abs(column);
#ifdef Paranoia
  if(((lp->var_to_orig == NULL) && newcol) ||
     (column > MAX(lp->columns, lp->orig_columns))) {
    report(lp, IMPORTANT, "get_origcol_name: Column %d out of range", column);
    return(NULL);
  }
#endif

  if(lp->names_used && (lp->col_name[column] != NULL) && (lp->col_name[column]->name != NULL)) {
#ifdef Paranoia
    if(lp->col_name[column]->index != column)
      report(lp, SEVERE, "get_origcol_name: Inconsistent column ordinal %d vs %d\n",
                         column, lp->col_name[column]->index);
#endif
    ptr = lp->col_name[column]->name;
  }
  else {
    if(newcol)
      sprintf((char *) name, COLNAMEMASK2, column);
    else
      sprintf((char *) name, COLNAMEMASK, column);
    ptr = name;
  }
  return(ptr);
}

STATIC int MIP_count(lprec *lp)
{
  return( lp->int_count+lp->sc_count+SOS_count(lp) );
}
STATIC int SOS_count(lprec *lp)
{
  if(lp->SOS == NULL)
    return( 0 );
  else
    return( lp->SOS->sos_count );
}
STATIC int GUB_count(lprec *lp)
{
  if(lp->GUB == NULL)
    return( 0 );
  else
    return( lp->GUB->sos_count );
}

STATIC REAL getBoundViolation(lprec *lp, int row_nr)
/* Returns the bound violation of a given basic variable; the return
   value is negative if it is below is lower bound, it is positive
   if it is greater than the upper bound, and zero otherwise. */
{
  REAL value, test;

  value  = lp->rhs[row_nr];
  row_nr = lp->var_basic[row_nr];
  test = value - my_lowbound(lp->lowbo[row_nr]);
  my_roundzero(test, lp->epsprimal);
  if(test > 0) {
    test = value - lp->upbo[row_nr];
    my_roundzero(test, lp->epsprimal);
    if(test < 0)
      test = 0;
  }
  return( test );
}

STATIC REAL compute_feasibilitygap(lprec *lp, MYBOOL isdual)
{
  int  i;
  REAL f;

  f = 0;
  if(isdual) {
    for(i = 1; i <= lp->rows; i++)
      if(lp->rhs[i] < 0)
        f -= lp->rhs[i];
      else if(lp->rhs[i] > lp->upbo[lp->var_basic[i]])
        f += lp->rhs[i] - lp->upbo[lp->var_basic[i]];
  }
  else {
  }
  return( f );
}

REAL minimumImprovementOF(lprec *lp)
/* This function tries to find a non-zero minimum improvement
   if the OF contains all integer variables (logic only applies if we are
   looking for a single solution, not possibly several equal-valued ones).
*/
{
  REAL   test, value, multOF, divOF, minABS = lp->infinite;
  int    OFrow, j, jj, jb, je, pluscount, intcount, OFcount;
  MATrec *mat = lp->matA;

  value = 0.0;
  if((lp->int_count > 0) && (lp->solutionlimit == 1) && mat_validate(mat)) {

    /* Get OF row starting and ending positions, as well as the first column index */
    jb = 0;
    je = mat->row_end[0];
    jj = ROW_MAT_COL(mat->row_mat[0]);

    /* First check if we have an OF hidden in a constraint row;
       (this is often done in models defined via branch-and-cut solvers) */
    if((je == 1) && !is_int(lp, jj) &&
       (mat_collength(mat, jj) == 2) &&
       (get_constr_type(lp, (OFrow = mat->col_mat[mat->col_end[jj]-1].row_nr)) == EQ)) {
      jb = mat->row_end[OFrow-1];
      je = mat->row_end[OFrow];
      multOF = get_mat_byindex(lp, 0, TRUE);
    }
    /* Otherwise it is a normal OF row */
    else {
      OFrow = 0;
      jj = -1;
      multOF = 1.0;
    }
    divOF = 1.0;

    /* Find if our (de-facto) OF contains all integers and
       pick up the smallest absolute OF value */
    minABS = lp->infinite;
    pluscount = 0;
    OFcount = 0;
    intcount = 0;
    for(je--; je >= jb; je--) {
      j = ROW_MAT_COL(mat->row_mat[je]);
      if(j == jj) {
        divOF = get_mat_byindex(lp, je, TRUE);
        continue;
      }
      if(!is_int(lp, j))
        break;
      test = get_mat_byindex(lp, je, TRUE);
      if(test > 0)
        pluscount++;
      OFcount++;
      test = fabs(test);
      minABS = MIN(minABS, test);

      test += test*lp->epsmachine;
      test = modf(test, &value);
      if(test < lp->epsprimal)
        intcount++;
    }
    value = 0.0;

    /* Do further analysis if the objective is all integer */
    if(je < jb) {

      /* We must check for explicit or implicit ordinality of the OF coefficients */
      je = mat->row_end[OFrow];
      for(je--; je >= jb; je--) {
        j = ROW_MAT_COL(mat->row_mat[je]);
        if(j == jj)
          continue;
        test = get_mat_byindex(lp, je, TRUE)/minABS;
        test += my_sign(test)*lp->epsmachine;
        test = modf(test, &value);
        if(fabs(test) > lp->epsprimal)
          break;
      }

      /* Did we get through all right? */
      if(je < jb) {
        if(OFrow == 0)
          value = minABS;
        else
          value = fabs( multOF / divOF * minABS );
      }
      else if(intcount == OFcount)
        value = 1.0;
      else
        value = 0.0;
    }
  }
  return( value );
}

STATIC REAL feasibilityOffset(lprec *lp, MYBOOL isdual)
{
  int    i, j;
  REAL   f, Extra;
  MATrec *mat = lp->matA;

  if(isdual) {
#ifdef UseLegacyExtrad
   /* This is the legacy (v3.2-) Extrad logic */
    int  varnr, rownr, je;
    REAL *drow;
    MATrec *mat = lp->matA;

    allocREAL(lp, &drow, lp->rows+1, FALSE);
    bsolve(lp, 0, drow, lp->epsprimal, 1.0);

    Extra = 0;
    for(i = 1; i <= lp->columns; i++) {
      varnr = lp->rows + i;
      f = 0;
      je = mat->col_end[i];
      for(j = mat->col_end[i - 1]; j < je; j++)
        rownr = mat->col_mat[j].row_nr;
        if(drow[rownr] != 0)
          f += drow[rownr] * mat->col_mat[j].value;
      if(f < Extra)
        Extra = f;
    }
    FREE(drow);
#else
  /* Set Extra to be the most negative of the objective coefficients.
     We effectively subtract Extrad from every element of the objective
     row, thereby making the entire objective row non-negative.  Note
     that this forces dual feasibility!  Although it also alters the
     objective function, we don't really care about that too much
     because we only use the dual algorithm to obtain a primal feasible
     solution that we can start the primal algorithm with.  Virtually
     any non-zero objective function will work with this approach! */
    Extra = lp->obj_mincost;
    if(Extra >= lp->infinite)
    for(i = 1; i <= lp->columns; i++) {
      f = 0;
      j = mat->col_end[i - 1];
     /* Since the A-matrix is sorted with the objective function row
        at the top we do not need to scan the entire matrix - KE fix 20030801 */
      if((j < mat->col_end[i]) && (mat->col_mat[j].row_nr == 0)) {
        f = mat->col_mat[j].value;
        if(f < Extra)
          Extra = f;
      }
    }
#endif

#if 1
    if(Extra >= 0) {
     /* Could argue that an OF-shifting could benefit some models via
        increased OF sparsity and that setting Extra=0 is not necessary */
      Extra = 0;
    }
#endif
    lp->obj_mincost = Extra;
  }
  else {
  /* Set Extra to be the index of the most negative of the net RHS coefficients;
     this approach can be used in the primal phase 1 when there are no equality
     constraints.  When there are equality constraints, additional artificial
     variables should be introduced when the RHS is negative */
    Extra = 0;
    j = 0;
    Extra = lp->infinite;
    for(i = 1; i <= lp->rows; i++) {
      f = lp->rhs[i];
      if(f < Extra) {
        Extra = f;
        j = i;
      }
    }
    Extra = j;
  }

  return(Extra);

}

STATIC MYBOOL add_artificial(lprec *lp, int forrownr)
/* This routine is called for each constraint at the start of
   primloop and the primal problem is infeasible. Its
   purpose is to add artificial variables and associated
   objective function values to populate primal phase 1. */
{
  MYBOOL add;

  /* Make sure we don't add unnecessary artificials, i.e. avoid
     cases where the slack variable is enough */

  add = !isBasisVarFeasible(lp, lp->epspivot, forrownr);

  if(add) {
    int    *rownr, i, bvar, ii;
    REAL   *avalue, rhscoef, acoef;

    /* Check the simple case where a slack is basic */
    for(i = 1; i <= lp->rows; i++) {
      if(lp->var_basic[i] == forrownr)
        break;
    }
    acoef = 1;

    /* If not, look for any basic user variable that has a
       non-zero coefficient in the current constraint row */
    if(i > lp->rows) {
      for(i = 1; i <= lp->rows; i++) {
        ii = lp->var_basic[i] - lp->rows;
        if((ii <= 0) || (ii > (lp->columns-lp->Extrap)))
          continue;
        ii = mat_findelm(lp->matA, forrownr, ii);
        if(ii >= 0) {
          acoef = lp->matA->col_mat[ii].value;
          break;
        }
      }
    }
    bvar = i;

    add = (MYBOOL) (bvar <= lp->rows);
    if(add) {
      rhscoef = lp->rhs[forrownr];

     /* Create temporary sparse array storage */
      allocREAL(lp, &avalue, 2, FALSE);
      allocINT(lp, &rownr, 2, FALSE);

     /* Set the objective coefficient */
      rownr[0]  =  0;
      avalue[0] = my_chsign(is_chsign(lp, 0), 1);

     /* Set the constraint row coefficient */
      rownr[1]  = forrownr;
      avalue[1] = my_chsign(is_chsign(lp, forrownr), my_sign(rhscoef/acoef));

     /* Add the column of artificial variable data to the user data matrix */
      add_columnex(lp, 2, avalue, rownr);

     /* Free the temporary sparse array storage */
      FREE(rownr);
      FREE(avalue);

     /* Now set the artificial variable to be basic */
      lp->is_lower[lp->var_basic[bvar]] = TRUE;
      setBasisVar(lp, bvar, lp->sum);
      lp->basis_valid = TRUE;

      lp->Extrap++;
    }
    else {
      report(lp, CRITICAL, "add_artificial: Could not find replacement basis variable for row %d\n",
                           forrownr);
      lp->basis_valid = FALSE;
    }

  }

  return(add);

}

STATIC int get_artificialRow(lprec *lp, int colnr)
{
  colnr = lp->matA->col_end[colnr-1]+1;
  colnr = lp->matA->col_mat[colnr].row_nr;
  return( colnr );
}

STATIC int findAnti_artificial(lprec *lp, int colnr)
/* Find a basic artificial variable to swap against the non-basic slack variable, if possible */
{
  int i, k, rownr = 0;

  if((lp->Extrap == 0) || (colnr > lp->rows) || !lp->is_basic[colnr])
    return( rownr );

  for(i = 1; i <= lp->rows; i++) {
    k = lp->var_basic[i];
    if((k > lp->sum-abs(lp->Extrap)) && (lp->rhs[i] == 0)) {
      k -= lp->rows;
      k = lp->matA->col_end[k] + 1;
      rownr = lp->matA->col_mat[k].row_nr;
#if 0
     /* Should we find the artificial's slack direct "antibody"? */
      if(rownr == colnr)
        break;
      rownr = 0;
#endif
    }
  }
  return( rownr );
}

STATIC int findBasicArtificial(lprec *lp, int before)
{
  int i = 0;

  if(abs(lp->Extrap) > 0) {
    if(before > lp->rows || before <= 1)
      i = lp->rows;
    else
      i = before;

    while((i > 0) && (lp->var_basic[i] <= lp->sum-abs(lp->Extrap)))
      i--;
  }

  return(i);
}

STATIC int findBasicFixedvar(lprec *lp, int before)
{
  int i;

  if(before > lp->rows || before <= 1)
    i = lp->rows;
  else
    i = before;

  while((i > 0) &&
        (((lp->var_basic[i] <= lp->rows) && !is_constr_type(lp, i, EQ)) ||
         ((lp->var_basic[i] > lp->rows) && !is_fixedvar(lp, lp->var_basic[i]))))
    i--;
  return(i);
}

STATIC int find_artificialReplacement(lprec *lp, int rownr, REAL *prow)
/* The logic in this section generally follows Vacek Chvatal: Linear Programming, p. 130 */
{
  int  i, bestindex;
  REAL bestvalue;

 /* Solve for "local reduced cost" */
  compute_reducedcosts(lp, TRUE, rownr, prow, NULL, NULL, NULL);

 /* Find a suitably non-singular variable to enter ("most orthogonal") */
  bestindex = 0;
  bestvalue = 0;
  for(i = 1; i <= lp->sum-abs(lp->Extrap); i++) {
    if(!lp->is_basic[i] && !is_fixedvar(lp, i) &&
      (fabs(prow[i]) > bestvalue)) {
      bestindex = i;
      bestvalue = fabs(prow[i]);
    }
  }

  /* Prepare to update inverse and pivot/iterate (compute Bw=a) */
  if(i > lp->sum-abs(lp->Extrap))
    i = 0;
  else
    fsolve(lp, i, prow, lp->workrowsINT, lp->epsmachine, 1.0, TRUE);

  return( i );
}
STATIC void eliminate_artificials(lprec *lp, REAL *prow)
{
  int   i, j, colnr, rownr, Extrap;
  LREAL theta;

  Extrap = abs(lp->Extrap);
  for(i = 1; (i <= lp->rows) && (Extrap > 0); i++) {
    j = lp->var_basic[i];
    if(j <= lp->sum-Extrap)
      continue;
    j -= lp->rows;
    rownr = get_artificialRow(lp, j);
    colnr = find_artificialReplacement(lp, rownr, prow);
    theta = 0;
#if 0
    performiteration(lp, rownr, colnr, theta, TRUE);
#else
    setBasisVar(lp, rownr, colnr);
#endif
    del_column(lp, j);
    Extrap--;
  }
  lp->Extrap = 0;
}
STATIC void clear_artificials(lprec *lp)
{
  int i, j, n, Extrap;

  /* Substitute any basic artificial variable for its slack counterpart */
  n = 0;
  Extrap = abs(lp->Extrap);
  for(i = 1; (i <= lp->rows) && (n < Extrap); i++) {
    j = lp->var_basic[i];
    if(j <= lp->sum-Extrap)
      continue;
    j = get_artificialRow(lp, j-lp->rows);
    setBasisVar(lp, i, j);
    n++;
  }
#ifdef Paranoia
  if(n != lp->Extrap)
    report(lp, SEVERE, "clear_artificials: Unable to clear all basic artificial variables\n");
#endif

  /* Delete any remaining non-basic artificial variables */
  while(Extrap > 0) {
    i = lp->sum-lp->rows;
    del_column(lp, i);
    Extrap--;
  }
  lp->Extrap = 0;
  if(n > 0) {
    lp->doInvert = TRUE;
    lp->basis_valid = TRUE;
  }
}

STATIC MYBOOL isPhase1(lprec *lp)
{
#if 0
  return(((lp->simplex_mode & SIMPLEX_PRIMAL_Phase1 != 0) && (lp->Extrap > 0)) ||
         ((lp->simplex_mode & SIMPLEX_DUAL_Phase1 != 0) && (lp->Extrad != 0)));
#else
  return((MYBOOL) ((lp->Extrap > 0) || (lp->Extrad != 0)));
#endif
}

STATIC MYBOOL feasiblePhase1(lprec *lp, REAL epsvalue)
{
  REAL   gap;
  MYBOOL test;

  gap = fabs(lp->rhs[0] - lp->orig_rh[0]);
  test = (MYBOOL) (gap < epsvalue);
  return( test) ;
}

STATIC MYBOOL isDegenerateBasis(lprec *lp, int basisvar)
{
  int varindex;

  varindex = lp->var_basic[basisvar];
  if((fabs(lp->rhs[basisvar]) < lp->epsprimal) ||
     (fabs(lp->upbo[varindex]-lp->rhs[basisvar]) < lp->epsprimal))
    return( TRUE );
  else
    return( FALSE );
}

STATIC MYBOOL isBasisVarFeasible(lprec *lp, REAL tol, int basis_row)
{
  int    col;
  REAL   x;
  MYBOOL Ok = TRUE;
  MYBOOL doSC = FALSE;

  col = lp->var_basic[basis_row];
  x = lp->rhs[basis_row];         /* The current solution of basic variables stored here! */
  if((x < -tol) || (x > lp->upbo[col]+tol))
    Ok = FALSE;
  else if(doSC && (col > lp->rows) && (fabs(lp->var_is_sc[col - lp->rows]) > 0)) {
    if((x > tol) && (x < fabs(lp->var_is_sc[col - lp->rows])-tol))
      Ok = FALSE;
  }
  return( Ok );
}
STATIC MYBOOL isPrimalFeasible(lprec *lp, REAL tol)
{
  int    i;
  MYBOOL feasible = TRUE;

#if 0
  /* Traditional indexing style */
  for(i = 1; (i <= lp->rows) && feasible; i++)
    feasible = isBasisVarFeasible(lp, tol, i);
#else
  /* Fast array pointer style */
  LREAL *rhsptr;
  int  *idxptr;
  MYBOOL doSC = FALSE;

  for(i = 1, rhsptr = lp->rhs+1, idxptr = lp->var_basic+1;
      feasible && (i <= lp->rows); i++, rhsptr++, idxptr++) {
/*    if(((*rhsptr) < lp->lowbo[(*idxptr)]-tol) || ((*rhsptr) > lp->upbo[(*idxptr)]+tol)) */
    if(((*rhsptr) < -tol) || ((*rhsptr) > lp->upbo[(*idxptr)]+tol))
      feasible = FALSE;
    else if(doSC && ((*idxptr) > lp->rows) && (fabs(lp->var_is_sc[(*idxptr)-lp->rows]) > 0)) {
      if(((*rhsptr) > tol) && ((*rhsptr) < fabs(lp->var_is_sc[(*idxptr) - lp->rows])-tol))
        feasible = FALSE;
    }
  }
#endif

  return(feasible);
}

STATIC MYBOOL isDualFeasible(lprec *lp, REAL tol)
{
  int    i;
  MYBOOL feasible = TRUE;
  REAL   *values, test;

  allocREAL(lp, &values, lp->sum+1, TRUE);
  construct_solution(lp, values);
  for(i = 1; feasible && (i <= lp->rows); i++) {
    test = get_rh_lower(lp, i);
    feasible = (MYBOOL) (values[i] > test-tol);
    if(feasible) {
      test = get_rh_upper(lp, i);
      feasible = (MYBOOL) (values[i] < test+tol);
    }
  }
  FREE(values);

  return(feasible);
}

void __WINAPI default_basis(lprec *lp)
{
  int i;

  /* Set the slack variables to be basic; note that the is_basic[] array
     is a helper array filled in presolve() to match var_basic[]. */
  for(i = 1; i <= lp->rows; i++) {
    lp->var_basic[i] = i;
    lp->is_basic[i] = TRUE;
    lp->is_lower[i] = TRUE;
  }
  lp->var_basic[0] = TRUE; /* Set to signal that this is the default basis */

  /* Set user variables at their lower bound, including the
     dummy slack for the objective "constraint" */
  for(; i <= lp->sum; i++) {
    lp->is_basic[i] = FALSE;
    lp->is_lower[i] = TRUE;
  }
  lp->is_lower[0] = TRUE;

  lp->doRebase = TRUE;
  lp->doInvert = TRUE;
  lp->doRecompute = TRUE;
  lp->basis_valid = TRUE;  /* Do not re-initialize basis on entering Solve */
}

MYBOOL __WINAPI set_basis(lprec *lp, int *bascolumn, MYBOOL nonbasic)   /* Added by KE */
{
  int    i,s,k,n;

 /* Initialize (lp->is_basic is set in preprocess); Note that as of v5 and before
    it is an lp_solve convention that basic variables are at their lower bounds!
    This routine provides for the a possible future case that basic variables
    can be upper-bounded. */
  lp->is_lower[0] = TRUE;
  for(i = 1; i <= lp->sum; i++) {
    lp->is_lower[i] = TRUE;
    lp->var_basic[i] = FALSE;
  }

 /* Set basic and optionally non-basic variables;
    negative index means at lower bound, positive at upper bound */
  if(nonbasic)
    n = lp->sum;
  else
    n = lp->rows;
  for(i = 1; i <= n; i++) {
    s = bascolumn[i];
    k = abs(s);
    if(k <= 0 || k > lp->sum)
      return( FALSE );
    if(i <= lp->rows) {
      lp->var_basic[i] = k;
      lp->is_basic[k] = TRUE;
    }
    else     /* Remove this test if basic variables can be upper-bounded */
    if(s > 0)
      lp->is_lower[k] = FALSE;
  }

 /* Invalidate basis */
  lp->doInvert = TRUE;
  lp->doRebase = TRUE;
  lp->doRecompute = TRUE;
  lp->basis_valid = TRUE;   /* Do not re-initialize basis on entering Solve */
  lp->var_basic[0] = FALSE; /* Set to signal that this is a non-default basis */

  return( TRUE );
}

int __WINAPI get_basiscrash(lprec *lp)
{
  return(lp->crashmode);
}

void __WINAPI set_basiscrash(lprec *lp, int mode)
{
  lp->crashmode = mode;
}

void __WINAPI reset_basis(lprec *lp)
{
  lp->basis_valid = FALSE;   /* Causes reinversion at next opportunity */
}

void __WINAPI get_basis(lprec *lp, int *bascolumn, MYBOOL nonbasic)
{
  int    k, i;

  if(!lp->basis_valid)
    return;

  *bascolumn = 0;

  /* First save basic variable indexes */
  for(i = 1; i <= lp->rows; i++) {
    k = lp->var_basic[i];
    bascolumn[i] = my_chsign(lp->is_lower[k], k);
  }

  /* Then optionally save non-basic variable indeces */
  if(nonbasic) {

    for(k = 1; k <= lp->sum; k++) {
      if(lp->is_basic[k])
        continue;
      bascolumn[i] = my_chsign(lp->is_lower[k], k);
      i++;
    }
  }
}

STATIC MYBOOL is_BasisReady(lprec *lp)
{
  return( (MYBOOL) (lp->var_basic[0] != AUTOMATIC) );
}

STATIC MYBOOL verifyBasis(lprec *lp)
{
  int    i, ii, k = 0;
  MYBOOL result = FALSE;

  for(i = 1; i <= lp->rows; i++) {
    ii = lp->var_basic[i];
    if((ii < 1) || (ii > lp->sum) || !lp->is_basic[ii]) {
      k = i;
      ii = 0;
      goto Done;
    }
  }

  ii = lp->rows;
  for(i = 1; i <= lp->sum; i++) {
    if(lp->is_basic[i])
      ii--;
  }
  result = (MYBOOL) (ii == 0);

Done:
#if 0  /* For testing */
  if(!result)
    ii = 0;
#endif
  return(result);
}

STATIC int __WINAPI setBasisVar(lprec *lp, int basisPos, int enteringCol)
{
  int leavingCol;

  leavingCol = lp->var_basic[basisPos];

#ifdef Paranoia
  if((basisPos < 1) || (basisPos > lp->rows))
    report(lp, SEVERE, "setBasisVar: Invalid leaving basis position %d specified at iteration %d\n",
                       basisPos, get_total_iter(lp));
  if((leavingCol < 1) || (leavingCol > lp->sum))
    report(lp, SEVERE, "setBasisVar: Invalid leaving column %d referenced at iteration %d\n",
                       leavingCol, get_total_iter(lp));
  if((enteringCol < 1) || (enteringCol > lp->sum))
    report(lp, SEVERE, "setBasisVar: Invalid entering column %d specified at iteration %d\n",
                       enteringCol, get_total_iter(lp));
#endif

#ifdef ParanoiaXY
  if(!lp->is_basic[leavingCol])
    report(lp, IMPORTANT, "setBasisVar: Leaving variable %d is not basic at iteration %d\n",
                           leavingCol, get_total_iter(lp));
  if(enteringCol > lp->rows && lp->is_basic[enteringCol])
    report(lp, IMPORTANT, "setBasisVar: Entering variable %d is already basic at iteration %d\n",
                           enteringCol, get_total_iter(lp));
#endif

  lp->var_basic[0]          = FALSE;       /* Set to signal that this is a non-default basis */
  lp->var_basic[basisPos]   = enteringCol;
  lp->is_basic[leavingCol]  = FALSE;
  lp->is_basic[enteringCol] = TRUE;

  return(leavingCol);
}

/* Bounds updating and unloading routines; requires that the
   current values for upbo and lowbo are in the original base. */
STATIC int perturb_bounds(lprec *lp, BBrec *perturbed, MYBOOL includeNONBASIC, MYBOOL includeFIXED)
{
  int  i, j, n = 0;
  REAL tmpreal, *upbo, *lowbo;

  if(perturbed == NULL)
    return( n );

 /* Compute expanded variable results for comparison purposes */
  construct_solution(lp, NULL);

 /* Map reference bounds to previous state, i.e. cumulate
    perturbations in case of persistent problems */
  upbo  = perturbed->upbo;
  lowbo = perturbed->lowbo;

 /* Set appropriate target variable range */
  if(includeNONBASIC)
    i = lp->sum;
  else
    i = lp->rows;

 /* Perturb (expand) finite basic variable bounds randomly */
  for(; i > 0; i--) {

    /* Direct to actual target variable */
    if(includeNONBASIC)
      j = i;
    else
      j = lp->var_basic[i];

    /* Don't perturb regular slack variables */
    if((j <= lp->rows) && (lowbo[j] == 0) && (upbo[j] >= lp->infinite))
      continue;

    /* Don't perturb fixed variables if not specified */
    if(!includeFIXED && (upbo[j] == lowbo[j]))
      continue;

    /* Lower bound */
    tmpreal = lp->solution[j]-lowbo[j];
    if(tmpreal < lp->epsperturb*RANDSCALE) {
      tmpreal = (rand() % RANDSCALE) + 1;    /* Added 1 by KE */
      tmpreal *= lp->epsperturb;
      lowbo[j] -= tmpreal;
      n++;
    }
    /* Upper bound */
    tmpreal = lp->solution[j]-upbo[j];
    if(tmpreal > -lp->epsperturb*RANDSCALE) {
      tmpreal = (rand() % RANDSCALE) + 1;  /* Added 1 by KE */
      tmpreal *= lp->epsperturb;
      upbo[j] += tmpreal;
      n++;
    }
  }

 /* Make sure we start from scratch */
  lp->doRebase = TRUE;
  lp->doInvert = TRUE;

  return( n );
}

STATIC MYBOOL restore_bounds(lprec *lp, MYBOOL doupbo, MYBOOL dolowbo)
{
  MYBOOL ok;
  BBrec  *savedbounds = lp->bb_bounds;

  ok = (MYBOOL) (savedbounds != NULL);
  if(ok) {
    if(doupbo)
      MEMCOPY(savedbounds->upbo, savedbounds->UB_base, lp->sum + 1);
    if(dolowbo)
      MEMCOPY(savedbounds->lowbo, savedbounds->LB_base, lp->sum + 1);
    if(doupbo || dolowbo) {
      lp->doRebase = savedbounds->UBzerobased;
      lp->doRecompute = TRUE;
    }
  }
  return( ok );
}

STATIC MYBOOL impose_bounds(lprec *lp, REAL *upbo, REAL *lowbo)
/* Explicitly set working bounds to given vectors without pushing or popping */
{
  MYBOOL ok;

  ok = (MYBOOL) ((upbo != NULL) || (lowbo != NULL));
  if(ok) {
    if((upbo != NULL) && (upbo != lp->upbo))
      MEMCOPY(lp->upbo,  upbo,  lp->sum + 1);
    if((lowbo != NULL) && (lowbo != lp->lowbo))
      MEMCOPY(lp->lowbo, lowbo, lp->sum + 1);
    if(lp->bb_bounds != NULL)
      lp->bb_bounds->UBzerobased = FALSE;
    lp->doRebase = TRUE;
  }
  lp->doRecompute = TRUE;
  return( ok );
}

STATIC MYBOOL validate_bounds(lprec *lp, REAL *upbo, REAL *lowbo)
/* Check if all bounds are Explicitly set working bounds to given vectors without pushing or popping */
{
  MYBOOL ok;
  int    i;

  ok = (MYBOOL) ((upbo != NULL) || (lowbo != NULL));
  if(ok) {
    for(i = 1; i <= lp->sum; i++)
      if((lowbo[i] > upbo[i]) || (lowbo[i] < lp->orig_lowbo[i]) || (upbo[i] > lp->orig_upbo[i]))
        break;
    ok = (MYBOOL) (i > lp->sum);
  }
  return( ok );
}

STATIC int unload_BB(lprec *lp)
{
  int levelsunloaded = 0;

  if(lp->bb_bounds != NULL)
    while(pop_BB(lp->bb_bounds))
      levelsunloaded++;
  return( levelsunloaded );
}

STATIC basisrec *push_basis(lprec *lp, int *basisvar, MYBOOL *isbasic, MYBOOL *islower)
/* Save the ingoing basis and push it onto the stack */
{
  basisrec *newbasis = NULL;

  newbasis = (basisrec *) malloc(sizeof(*newbasis));
  if((newbasis != NULL) &&
    allocMYBOOL(lp, &newbasis->bound_is_lower, lp->sum + 1,  FALSE) &&
    allocMYBOOL(lp, &newbasis->var_is_basic,   lp->sum + 1,  FALSE) &&
    allocINT(lp,    &newbasis->basis_var,      lp->rows + 1, FALSE)) {

    if(islower == NULL)
      islower = lp->is_lower;
    if(isbasic == NULL)
      isbasic = lp->is_basic;
    if(basisvar == NULL)
      basisvar = lp->var_basic;

    MEMCOPY(newbasis->bound_is_lower, islower,  lp->sum + 1);
    MEMCOPY(newbasis->var_is_basic,   isbasic,  lp->sum + 1);
    MEMCOPY(newbasis->basis_var,      basisvar, lp->rows + 1);

    newbasis->previous = lp->bb_basis;
    if(lp->bb_basis == NULL)
      newbasis->level = 0;
    else
      newbasis->level = lp->bb_basis->level + 1;

    lp->bb_basis = newbasis;
  }
  return( newbasis );
}

STATIC MYBOOL compare_basis(lprec *lp)
/* Compares the last pushed basis with the currently active basis */
{
  int i, j;
  MYBOOL same_basis = TRUE;

  if(lp->bb_basis == NULL)
    return( FALSE );

  /* Loop over basis variables until a mismatch (order can be different) */
  i = 1;
  while(same_basis && (i <= lp->rows)) {
    j = 1;
    while(same_basis && (j <= lp->rows)) {
      same_basis = (MYBOOL) (lp->bb_basis->basis_var[i] != lp->var_basic[j]);
      j++;
    }
    same_basis = !same_basis;
    i++;
  }
  /* Loop over bound status indicators until a mismatch */
  i = 1;
  while(same_basis && (i <= lp->sum)) {
    same_basis = (lp->bb_basis->bound_is_lower[i] && lp->is_lower[i]);
    i++;
  }

  return( same_basis );
}

STATIC MYBOOL restore_basis(lprec *lp)
/* Restore values from the previously pushed / saved basis without popping it */
{
  MYBOOL ok;

  ok = (MYBOOL) (lp->bb_basis != NULL);
  if(ok) {
    MEMCOPY(lp->var_basic, lp->bb_basis->basis_var,      lp->rows + 1);
    MEMCOPY(lp->is_basic,  lp->bb_basis->var_is_basic,   lp->sum + 1);
    MEMCOPY(lp->is_lower,  lp->bb_basis->bound_is_lower, lp->sum + 1);
    lp->doRebase = TRUE;
    lp->doInvert = TRUE;
  }
  return( ok );
}

STATIC MYBOOL pop_basis(lprec *lp, MYBOOL restore)
/* Pop / free, and optionally restore the previously "pushed" / saved basis */
{
  MYBOOL ok;
  basisrec *oldbasis;

  ok = (MYBOOL) (lp->bb_basis != NULL);
  if(ok) {
    oldbasis = lp->bb_basis;
    if(oldbasis != NULL) {
      lp->bb_basis = oldbasis->previous;
      FREE(oldbasis->basis_var);
      FREE(oldbasis->var_is_basic);
      FREE(oldbasis->bound_is_lower);
      FREE(oldbasis);
    }
    if(restore && (lp->bb_basis != NULL))
      restore_basis(lp);
  }
  return( ok );
}

STATIC int unload_basis(lprec *lp, MYBOOL restorelast)
{
  int levelsunloaded = 0;

  if(lp->bb_basis != NULL)
    while(pop_basis(lp, restorelast))
      levelsunloaded++;
  return( levelsunloaded );
}

STATIC REAL scaled_floor(lprec *lp, int column, REAL value, REAL epsscale)
{
  value = floor(value);
  if(value != 0)
  if(lp->columns_scaled && is_integerscaling(lp)) {
    value = scaled_value(lp, value, column);
    if(epsscale != 0)
      value += epsscale*lp->epsmachine;
/*      value += epsscale*lp->epsprimal; */
/*    value = restoreINT(value, lp->epsint); */
  }
  return(value);
}

STATIC REAL scaled_ceil(lprec *lp, int column, REAL value, REAL epsscale)
{
  value = ceil(value);
  if(value != 0)
  if(lp->columns_scaled && is_integerscaling(lp)) {
    value = scaled_value(lp, value, column);
    if(epsscale != 0)
      value -= epsscale*lp->epsmachine;
/*      value -= epsscale*lp->epsprimal; */
/*    value = restoreINT(value, lp->epsint); */
  }
  return(value);
}

/* Branch and bound variable selection functions */

STATIC MYBOOL is_sc_violated(lprec *lp, int column)
{
  int  varno;
  REAL tmpreal;

  varno = lp->rows+column;
  tmpreal = unscaled_value(lp, lp->var_is_sc[column], varno);
  return( (MYBOOL) ((tmpreal > 0) &&                    /* it is an (inactive) SC variable...    */
                    (lp->solution[varno] < tmpreal) &&  /* ...and the NZ lower bound is violated */
                    (lp->solution[varno] > 0)) );       /* ...and the Z lowerbound is violated   */
}
STATIC int find_sc_bbvar(lprec *lp, int *count)
{
  int    i, ii, n, bestvar;
  int    firstsc, lastsc;
  REAL   hold, holdINT, bestval, OFval, randval, scval;
  MYBOOL reversemode, greedymode, randomizemode,
         pseudocostmode, pseudocostsel;

  bestvar = 0;
  if((lp->sc_count == 0) || (*count > 0))
    return(bestvar);

  reversemode    = is_bb_mode(lp, NODE_WEIGHTREVERSEMODE);
  greedymode     = is_bb_mode(lp, NODE_GREEDYMODE);
  randomizemode  = is_bb_mode(lp, NODE_RANDOMIZEMODE);
  pseudocostmode = is_bb_mode(lp, NODE_PSEUDOCOSTMODE);
  pseudocostsel  = is_bb_rule(lp, NODE_PSEUDOCOSTSELECT) ||
                   is_bb_rule(lp, NODE_PSEUDONONINTSELECT) ||
                   is_bb_rule(lp, NODE_PSEUDORATIOSELECT);

  bestvar = 0;
  bestval = -lp->infinite;
  hold    = 0;
  randval = 1;
  firstsc = 0;
  lastsc  = lp->columns;

  for(n = 1; n <= lp->columns; n++) {
    ii = get_var_priority(lp, n);
    i = lp->rows + ii;
    if(!lp->bb_varactive[ii] && is_sc_violated(lp, ii) && !SOS_is_marked(lp->SOS, 0, ii)) {

      /* Do tallies */
      (*count)++;
      lastsc = i;
      if(firstsc <= 0)
        firstsc = i;
      scval = get_PseudoRange(lp, ii, BB_SC);

      /* Select default pricing/weighting mode */
      if(pseudocostmode)
        OFval = get_PseudoCost(lp, ii, BB_SC, lp->solution[i]);
      else
        OFval = my_chsign(is_maxim(lp), get_mat(lp, 0, ii));

      if(randomizemode)
        randval = exp(1.0*rand()/RAND_MAX);

      /* Find the maximum pseudo-cost of a variable (don't apply pseudocostmode here) */
      if(pseudocostsel) {
        if(pseudocostmode)
          hold = OFval;
        else
          hold = get_PseudoCost(lp, ii, BB_SC, lp->solution[i]);
        hold *= randval;
        if(greedymode) {
          if(pseudocostmode) /* Override! */
            OFval = my_chsign(is_maxim(lp), get_mat(lp, 0, ii));
          hold *= OFval;
        }
        hold = my_chsign(reversemode, hold);
      }
      else
      /* Find the variable with the largest sc gap (closest to the sc mean) */
      if(is_bb_rule(lp, NODE_FRACTIONSELECT)) {
        hold = modf(lp->solution[i]/scval, &holdINT);
        holdINT = hold-1;
        if(fabs(holdINT) > hold)
          hold = holdINT;
        if(greedymode)
          hold *= OFval;
        hold = my_chsign(reversemode, hold)*scval*randval;
      }
      else
      /* Do first or last violated sc index selection (default) */
      /* if(is_bb_rule(lp, NODE_FIRSTSELECT)) */
      {
        if(reversemode)
          continue;
        else {
          bestvar = i;
          break;
        }
      }

      /* Select better, check for ties, and split by proximity to 0.5*var_is_sc */
      if(hold > bestval) {
        if( (bestvar == 0) ||
            (hold > bestval+lp->epsprimal) ||
            (fabs(modf(lp->solution[i]/scval, &holdINT) - 0.5) <
             fabs(modf(lp->solution[bestvar]/get_PseudoRange(lp, bestvar-lp->rows, BB_SC), &holdINT) - 0.5)) ) {
          bestval = hold;
          bestvar = i;
        }
      }
    }
  }

  if(is_bb_rule(lp, NODE_FIRSTSELECT) && reversemode)
    bestvar = lastsc;

  return(bestvar);
}

STATIC int find_sos_bbvar(lprec *lp, int *count, MYBOOL intsos)
{
  int k, i, j, var;

  var = 0;
  if((lp->SOS == NULL) || (*count > 0))
    return(var);

  /* Check if the SOS'es happen to already be satisified */
  i = SOS_is_satisfied(lp->SOS, 0, lp->solution);
  if(i == 0 || i == -1)
    return(-1);

  /* Otherwise identify a SOS variable to enter B&B */
  for(k = 0; k < lp->sos_vars; k++) {
    i = lp->sos_priority[k];
#ifdef Paranoia
    if((i < 1) || (i > lp->columns))
      report(lp, SEVERE, "find_sos_bbvar: Invalid SOS variable map %d at %d\n",
                         i, k);
#endif
    j = lp->rows + i;
    if(!SOS_is_marked(lp->SOS, 0, i) && !SOS_is_full(lp->SOS, 0, i, FALSE)) {
      if(!intsos || is_int(lp, i)) {
        (*count)++;
        if(var == 0) {
          var = j;
          break;
        }
      }
    }
  }
  return(var);
}

STATIC int find_int_bbvar(lprec *lp, int *count, REAL *lowbo, REAL*upbo, int *firstint, int *lastint)
{
  int    i, ii, n, bestvar;
  REAL   hold, holdINT, bestval, OFval, randval;
  MYBOOL reversemode, greedymode, depthfirstmode, randomizemode,
         pseudocostmode, pseudocostsel;

  bestvar = 0;
  if((lp->int_count == 0) || (*count > 0))
    return(bestvar);

  reversemode    = is_bb_mode(lp, NODE_WEIGHTREVERSEMODE);
  greedymode     = is_bb_mode(lp, NODE_GREEDYMODE);
  randomizemode  = is_bb_mode(lp, NODE_RANDOMIZEMODE);
  depthfirstmode = is_bb_mode(lp, NODE_DEPTHFIRSTMODE) &&
                   (MYBOOL) (lp->bb_level <= lp->int_count);
  pseudocostmode = is_bb_mode(lp, NODE_PSEUDOCOSTMODE);
  pseudocostsel  = is_bb_rule(lp, NODE_PSEUDOCOSTSELECT) ||
                   is_bb_rule(lp, NODE_PSEUDONONINTSELECT) ||
                   is_bb_rule(lp, NODE_PSEUDORATIOSELECT);

  bestvar = 0;
  bestval = -lp->infinite;
  hold    = 0;
  randval = 1;

  for(n = 1; n <= lp->columns; n++) {
    ii = get_var_priority(lp, n);
    i = lp->rows + ii;
    if(is_int(lp,ii) && !solution_is_int(lp, i, FALSE)) {

      /* Check if the variable is already fixed */
      if(lowbo[i] == upbo[i]) {
        report(lp, IMPORTANT,
               "find_int_bbvar: INT var %d is already fixed at %d, but has non-INT value " MPSVALUEMASK "\n",
                ii, (int) lowbo[i], lp->solution[i]);
        continue;
      }

      /* Check if we should skip node candidates that are B&B active already */
      if(depthfirstmode && (lp->bb_varactive[n] != 0))
        continue;

      /* Do the naive detection */
      if((*count) == 0) { /* Pick up the index of the first non-integer INT */
        bestvar = i;
        (*firstint) = n;
      }
      (*count)++;
      (*lastint) = n;     /* Pick up index of the last non-integer INT */

      /* Select default pricing/weighting mode */
      if(pseudocostmode)
        OFval = get_PseudoCost(lp, ii, BB_INT, lp->solution[i]);
      else
        OFval = my_chsign(is_maxim(lp), get_mat(lp, 0, ii));

      if(randomizemode)
        randval = exp(1.0*rand()/RAND_MAX);

      /* Find the maximum pseudo-cost of a variable (don't apply pseudocostmode here) */
      if(pseudocostsel) {
        if(pseudocostmode)
          hold = OFval;
        else
          hold = get_PseudoCost(lp, ii, BB_INT, lp->solution[i]);
        hold *= randval;
        if(greedymode) {
          if(pseudocostmode) /* Override! */
            OFval = my_chsign(is_maxim(lp), get_mat(lp, 0, ii));
          hold *= OFval;
        }
        hold = my_chsign(reversemode, hold);
      }
      else
      /* Find the variable with the largest gap to its bounds (distance from being fixed) */
      if(is_bb_rule(lp, NODE_GAPSELECT)) {
        hold = lp->solution[i];
        holdINT = hold-unscaled_value(lp, upbo[i], i);
        hold -= unscaled_value(lp, lowbo[i], i);
        if(fabs(holdINT) > hold)
          hold = holdINT;
        if(greedymode)
          hold *= OFval;
        hold = my_chsign(reversemode, hold)*randval;
      }
      else
      /* Find the variable with the largest integer gap (closest to 0.5) */
      if(is_bb_rule(lp, NODE_FRACTIONSELECT)) {
        hold = modf(lp->solution[i], &holdINT);
        holdINT = hold-1;
        if(fabs(holdINT) > hold)
          hold = holdINT;
        if(greedymode)
          hold *= OFval;
        hold = my_chsign(reversemode, hold)*randval;
      }
      else
      /* Find the "range", most flexible variable */
      if(is_bb_rule(lp, NODE_RANGESELECT)) {
        hold = unscaled_value(lp, upbo[i]-lowbo[i], i);
        if(greedymode)
          hold *= OFval;
        hold = my_chsign(reversemode, hold)*randval;
      }
      else
      /* Do first or last non-int index selection (default) */
      /* if(is_bb_rule(lp, NODE_FIRSTSELECT)) */
      {
        if(reversemode)
          continue;
        else {
          bestval = 0;
          bestvar = i;
          break;
        }
      }

      /* Select better, check for ties, and split by proximity to 0.5 */
      if(hold > bestval) {
        if( (hold > bestval+lp->epsprimal) ||
            (fabs(modf(lp->solution[i], &holdINT) - 0.5) <
             fabs(modf(lp->solution[bestvar], &holdINT) - 0.5)) ) {
          bestval = hold;
          bestvar = i;
        }
      }
    }
  }

  if(is_bb_rule(lp, NODE_FIRSTSELECT) && reversemode)
    bestvar = (*lastint);

  return(bestvar);
}

STATIC BBPSrec *init_PseudoCost(lprec *lp)
{
  int     i;
  REAL    PSinitUP, PSinitLO;
  BBPSrec *newitem;
  MYBOOL  isPSCount;

  /* Allocate memory */
  newitem = (BBPSrec*) malloc(sizeof(*newitem));
  newitem->LOcost = (MATitem*) malloc((lp->columns+1) * sizeof(*newitem->LOcost));
  newitem->UPcost = (MATitem*) malloc((lp->columns+1) * sizeof(*newitem->UPcost));

  /* Initialize with OF values */
  isPSCount = is_bb_rule(lp, NODE_PSEUDONONINTSELECT);
  for(i = 1; i <= lp->columns; i++) {
    newitem->LOcost[i].row_nr = 1;
    newitem->UPcost[i].row_nr = 1;

    /* Initialize with the plain OF value as conventional usage suggests, or
       override in case of pseudo-nonint count strategy */
    PSinitUP = my_chsign(is_maxim(lp), get_mat(lp, 0, i));
    PSinitLO = -PSinitUP;
    if(isPSCount) {
      /* Set default assumed reduction in the number of non-ints by choosing this variable;
         KE changed from 0 on 30 June 2004 and made two-sided selectable.  Note that the
         typical value range is <0..1>, with a positive bias for an "a priori" assumed
         fast-converging (low "MIP-complexity") model. Very hard models may require
         negative initialized values for one or both. */
      PSinitUP = 0.1*0;
#if 0
      PSinitUP = my_chsign(PSinitUP < 0, PSinitUP);
      PSinitLO = -PSinitUP;
#else
      PSinitLO = PSinitUP;
#endif
    }
    newitem->UPcost[i].value  = PSinitUP;
    newitem->LOcost[i].value  = PSinitLO;
  }
  newitem->updatelimit     = DEF_PSEUDOCOSTUPDATES;
  newitem->updatesfinished = 0;
  newitem->restartlimit    = DEF_PSEUDOCOSTRESTART;

  /* Let the user have an opportunity to initialize pseudocosts */
  if(userabort(lp, MSG_INITPSEUDOCOST))
    lp->spx_status = USERABORT;

  return( newitem );
}

STATIC void free_PseudoCost(lprec *lp)
{
  if((lp != NULL) && (lp->bb_PseudoCost != NULL)) {
    FREE(lp->bb_PseudoCost->LOcost);
    FREE(lp->bb_PseudoCost->UPcost);
    FREE(lp->bb_PseudoCost);
  }
}

MYBOOL __WINAPI set_PseudoCosts(lprec *lp, REAL *clower, REAL *cupper, int *updatelimit)
{
  int i;
  if((lp->bb_PseudoCost == NULL) || ((clower == NULL) && (cupper == NULL)))
    return(FALSE);
  for(i = 1; i <= lp->columns; i++) {
    if(clower != NULL)
      lp->bb_PseudoCost->LOcost[i].value = clower[i];
    if(cupper != NULL)
      lp->bb_PseudoCost->UPcost[i].value = cupper[i];
  }
  if(updatelimit != NULL)
    lp->bb_PseudoCost->updatelimit = *updatelimit;
  return(TRUE);
}

MYBOOL __WINAPI get_PseudoCosts(lprec *lp, REAL *clower, REAL *cupper, int *updatelimit)
{
  int i;
  if((lp->bb_PseudoCost == NULL) || ((clower == NULL) && (cupper == NULL)))
    return(FALSE);
  for(i = 1; i <= lp->columns; i++) {
    if(clower != NULL)
      clower[i] = lp->bb_PseudoCost->LOcost[i].value;
    if(cupper != NULL)
      cupper[i] = lp->bb_PseudoCost->UPcost[i].value;
  }
  if(updatelimit != NULL)
    *updatelimit = lp->bb_PseudoCost->updatelimit;
  return(TRUE);
}

STATIC REAL get_PseudoRange(lprec *lp, int mipvar, int varcode)
{
  if(varcode == BB_SC)
    return( unscaled_value(lp, lp->var_is_sc[mipvar], lp->rows+mipvar) );
  else
    return( 1.0 );
}

STATIC void update_PseudoCost(lprec *lp, int mipvar, int varcode, MYBOOL capupper, REAL varsol)
{
  REAL     OFsol, uplim;
  MATitem  *PS;
  MYBOOL   nonIntSelect = is_bb_rule(lp, NODE_PSEUDONONINTSELECT);

  /* Establish input values;
     Note: The pseudocosts are normalized to the 0-1 range! */
  uplim = get_PseudoRange(lp, mipvar, varcode);
  varsol = modf(varsol/uplim, &OFsol);

  /* Set reference value according to pseudocost mode */
  if(nonIntSelect)
    OFsol = lp->bb_bounds->lastvarcus;    /* The count of MIP infeasibilities */
  else
    OFsol = lp->solution[0];              /* The problem's objective function value */

  if(_isnan(varsol)) {
    lp->bb_parentOF = OFsol;
    return;
  }

  /* Point to the applicable (lower or upper) bound */
  if(capupper) {
    PS = &lp->bb_PseudoCost->LOcost[mipvar];
  }
  else {
    PS = &lp->bb_PseudoCost->UPcost[mipvar];
    varsol = 1-varsol;
  }

  /* Make adjustment to divisor if we are using the ratio pseudo-cost approach */
  if(is_bb_rule(lp, NODE_PSEUDORATIOSELECT))
    varsol *= capupper;

  /* Compute the update (consider weighting in favor of most recent) */
  mipvar = lp->bb_PseudoCost->updatelimit;
  if(((mipvar <= 0) || (PS->row_nr < mipvar)) &&
     (fabs(varsol) > lp->epspivot)) {
    /* We are interested in the change in the MIP measure (contribution to increase
       or decrease, as the case may be) and not its last value alone. */
    PS->value = PS->value*PS->row_nr + (lp->bb_parentOF-OFsol) / (varsol*uplim);
    PS->row_nr++;
    PS->value /= PS->row_nr;
    /* Check if we have enough information to restart */
    if(PS->row_nr == mipvar) {
      lp->bb_PseudoCost->updatesfinished++;
      if(is_bb_mode(lp, NODE_RESTARTMODE) &&
        (lp->bb_PseudoCost->updatesfinished/(2.0*lp->int_count) >
         lp->bb_PseudoCost->restartlimit)) {
        lp->bb_break = AUTOMATIC;
        lp->bb_PseudoCost->restartlimit *= 2.681;  /* Who can figure this one out? */
        if(lp->bb_PseudoCost->restartlimit > 1)
          lp->bb_rule -= NODE_RESTARTMODE;
        report(lp, NORMAL, "update_PseudoCost: Restarting with updated pseudocosts\n");
      }
    }
  }
  lp->bb_parentOF = OFsol;
}

STATIC REAL get_PseudoCost(lprec *lp, int mipvar, int vartype, REAL varsol)
{
  REAL hold, uplim;

  uplim = get_PseudoRange(lp, mipvar, vartype);
  varsol = modf(varsol/uplim, &hold);
  if(_isnan(varsol))
    varsol = 0;

  hold = lp->bb_PseudoCost->LOcost[mipvar].value*varsol +
         lp->bb_PseudoCost->UPcost[mipvar].value*(1-varsol);

  return( hold*uplim );
}

STATIC int compute_theta(lprec *lp, int rownr, LREAL *theta, REAL HarrisScalar, MYBOOL primal)
/* The purpose of this routine is to compute the non-basic
   bound state / value of the leaving variable. */
{
  static LREAL x;
  static REAL  lb, ub, eps;
  static int   colnr;

  colnr = lp->var_basic[rownr];
  x     = lp->rhs[rownr];
  lb    = 0;
  ub    = lp->upbo[colnr];
  eps   = lp->epsprimal;

  /* Compute theta for the primal simplex */
  if(primal) {
    if(*theta > 0)
      x += eps*HarrisScalar;
    else if(ub < lp->infinite)
      x = (x - ub - eps*HarrisScalar);
    else {
      *theta = lp->infinite * my_sign(*theta);
      return( colnr );
    }
  }
  /* Compute theta for the dual simplex */
  else {

    /* Current value is below or equal to its lower bound */
    if(x < lb+eps)
      x = (x - lb + eps*HarrisScalar);

    /* Current value is above or equal to its upper bound */
    else if(x > ub-eps) {
      if(ub >= lp->infinite) {
        *theta = lp->infinite * my_sign(*theta);
        return( colnr );
      }
      else
        x = (x - ub - eps*HarrisScalar);
    }
  }
  my_roundzero(x, eps);
  *theta = x / *theta;

#ifdef EnforcePositiveTheta
  /* Check if we have negative theta due to rounding or an internal error */
  if(*theta < 0) {
    if(primal && (ub == lb))
      lp->rhs[rownr] = lb;
    else
#ifdef Paranoia
    if(*theta < -lp->epspivot) {
      report(lp, DETAILED, "compute_theta: Negative theta (%g) not allowed in base-0 version of lp_solve\n",
                            *theta);
    }
#endif
    *theta = 0;
  }
#endif

  return( colnr );
}

STATIC MYBOOL check_degeneracy(lprec *lp, REAL *pcol, int *degencount)
/* Check if the entering column Pi=Inv(B)*a is likely to produce improvement;
   (cfr. Istvan Maros: CTOTSM p. 233) */
{
  int  i, ndegen;
  REAL *rhs, sdegen, epsmargin = lp->epsprimal;

  sdegen = 0;
  ndegen = 0;
  rhs    = lp->rhs;
  for(i = 1; i <= lp->rows; i++) {
    rhs++;
    pcol++;
    if(fabs(*rhs) < epsmargin) {
      sdegen += *pcol;
      ndegen++;
    }
    else if(fabs((*rhs)-lp->upbo[lp->var_basic[i]]) < epsmargin) {
      sdegen -= *pcol;
      ndegen++;
    }
  }
  if(degencount != NULL)
    *degencount = ndegen;
/*  sdegen += epsmargin*ndegen; */
  return( (MYBOOL) (sdegen <= 0) );
}

STATIC MYBOOL performiteration(lprec *lp, int row_nr, int varin, LREAL theta, MYBOOL primal, REAL *prow, int *nzprow)
{
  static int    varout;
  static REAL   pivot, epsmargin, leavingValue, leavingUB, enteringUB;
  static MYBOOL leavingToUB, enteringFromUB, enteringIsFixed, leavingIsFixed;
  MYBOOL *islower = &(lp->is_lower[varin]);
  MYBOOL minitNow = FALSE, minitStatus = ITERATE_MAJORMAJOR;

  if(userabort(lp, MSG_ITERATION))
    return( minitNow );

#ifdef Paranoia
  if(row_nr > lp->rows) {
    if (lp->spx_trace)
      report(lp, IMPORTANT, "performiteration: Numeric instability encountered!\n");
    lp->spx_status = NUMFAILURE;
    return( FALSE );
  }
#endif
  varout = lp->var_basic[row_nr];
#ifdef Paranoia
  if(!lp->is_lower[varout])
    report(lp, SEVERE, "performiteration: Leaving variable %d was at its upper bound at iteration %d\n",
                        varout, get_total_iter(lp));
#endif

  /* Theta is the largest change possible (strictest constraint) for the entering
     variable (Theta is Chvatal's "t", ref. Linear Programming, pages 124 and 156) */
  lp->current_iter++;

  /* Test if it is possible to do a cheap "minor iteration"; i.e. set entering
     variable to its opposite bound, without entering the basis - which is
     obviously not possible for fixed variables! */
  epsmargin = lp->epsprimal;
  enteringFromUB = !(*islower);
  enteringUB = lp->upbo[varin];
  leavingUB  = lp->upbo[varout];
  enteringIsFixed = (MYBOOL) (fabs(enteringUB) < epsmargin);
  leavingIsFixed  = (MYBOOL) (fabs(leavingUB) < epsmargin);
#ifdef Paranoia
  if(enteringUB < 0)
    report(lp, SEVERE, "performiteration: Negative range for entering variable %d at iteration %d\n",
                        varin, get_total_iter(lp));
  if(leavingUB < 0)
    report(lp, SEVERE, "performiteration: Negative range for leaving variable %d at iteration %d\n",
                        varout, get_total_iter(lp));
#endif

  if(!enteringIsFixed) {
/*#define AvoidDegenerateEntering*/  /* KE development code, not ready for production */
#ifdef AvoidDegenerateEntering
    /* Avoid that the entering variable enters at one of its bounds;
       simply change its bound and find another entering variable. */
    if((theta == 0) || (fabs(enteringUB - theta)<epsmargin)) {
      int i, ix;
      /* Preferably find a full-range slack variable */
      for(i = 1; i <= nzprow[0]; i++) {
        ix = nzprow[i];
        if((ix > lp->rows) || (fabs(lp->upbo[varin]) < epsmargin))
          continue;
        if(prow[ix] < 0)
          break;
      }
      /* Check if we found a valid candidate */
      if(i <= nzprow[0]) {
        /* Swap bound on initial entering variables */
        lp->pivotRHS, lp, theta, NULL);
        *islower = !(*islower);
        /* Then prepare for normal pivoting with alternative entering column */
        varin = ix;
        islower = &(lp->is_lower[varin]);
        enteringFromUB = !(*islower);
        enteringUB = lp->upbo[varin];
        enteringIsFixed = (MYBOOL) (fabs(enteringUB) < epsmargin);
        fsolve(lp, varin, pcol, lp->workrowsINT, lp->epsmachine, 1.0, TRUE);
        theta = lp->bfp_prepareupdate(lp, row_nr, varin, pcol);
        theta = my_chsign(!(*islower), prow[varin]);
        compute_theta(lp, row_nr, &theta, 0, primal);
      }
    }
#endif

/*#define RelativeBoundTest*/  /* Use relative rather than absolute tests */
#ifdef RelativeBoundTest
    if(my_reldiff(enteringUB,theta) < -epsmargin) {
#else
    if(enteringUB - theta < -epsmargin) {
#endif
      minitNow    = TRUE;
      minitStatus = ITERATE_MINORRETRY;
    }
  }

  /* Process for minor iteration */
  if(minitNow) {

   /* Set the new values */
    theta = my_chsign(theta < 0, MIN(fabs(theta), enteringUB));

    /* Update the RHS / variable values and do bound-swap */
    pivot = lp->bfp_pivotRHS(lp, theta, NULL);
    *islower = !(*islower);

    lp->current_bswap++;

  }

  /* Process for major iteration */
  else {

    /* Update the active pricer for the current pivot */
    updatePricer(lp, row_nr, varin, lp->bfp_pivotvector(lp), prow, nzprow);

    /* Update the current basic variable values */
    pivot = lp->bfp_pivotRHS(lp, theta, NULL);

    /* See if the leaving variable goes directly to its upper bound. */
    leavingValue = lp->rhs[row_nr];
    leavingToUB = (MYBOOL) (leavingValue > leavingUB / 2);
    lp->is_lower[varout] = leavingIsFixed || !leavingToUB;

    /* Set the value of the entering varible */
    if(enteringFromUB) {
      lp->rhs[row_nr] = enteringUB - theta;
      *islower = TRUE;
    }
    else
      lp->rhs[row_nr] = theta;
    my_roundzero(lp->rhs[row_nr], epsmargin);

   /* Update basis indeces */
    varout = setBasisVar(lp, row_nr, varin);

   /* Finalize the update in preparation for next major iteration */
    lp->bfp_finishupdate(lp, enteringFromUB);

  }

  /* Show pivot tracking information, if specified */
  if((lp->verbose > NORMAL) && (MIP_count(lp) == 0) &&
     ((lp->current_iter % MAX(2, lp->rows / 10)) == 0))
    report(lp, NORMAL, "Objective value at iteration %7d is %12g\n",
                       get_total_iter(lp), lp->rhs[0]);

#if 0
  if(verify_solution(lp, FALSE, my_if(minitNow, "MINOR", "MAJOR")) >= 0) {
    if(minitNow)
      pivot = get_OF_raw(lp, varin);
    else
      pivot = get_OF_raw(lp, varout);
  }
#endif
#if 0
  if(minitNow)
    report(lp, NORMAL, "I:%5d - minor - %5d ignored,          %5d flips  from %s with THETA=%g and RHS=%g\n",
                       get_total_iter(lp), varout, varin, (enteringFromUB ? "UPPER" : "LOWER"), theta, lp->rhs[0]);
  else
    report(lp, NORMAL, "I:%5d - MAJOR - %5d leaves to %s,  %5d enters from %s with THETA=%g and RHS=%g\n",
                       get_total_iter(lp), varout, (leavingToUB    ? "UPPER" : "LOWER"),
                                           varin,  (enteringFromUB ? "UPPER" : "LOWER"), theta, lp->rhs[0]);
#endif

  if(lp->spx_trace) {
    report(lp, DETAILED, "Theta = " MPSVALUEMASK "\n", theta);
    if(minitNow) {
      if(!lp->is_lower[varin])
        report(lp, DETAILED,
        "performiteration: Variable %d changed to its lower bound at iteration %d (from %g)\n",
        varin, get_total_iter(lp), enteringUB);
      else
        report(lp, DETAILED,
        "performiteration: Variable %d changed to its upper bound at iteration %d (to %g)\n",
        varin, get_total_iter(lp), enteringUB);
    }
    else
      report(lp, NORMAL,
          "performiteration: Variable %d entered basis at iteration %d at " MPSVALUEMASK "\n",
          varin, get_total_iter(lp), lp->rhs[row_nr]);
    if(!primal) {
      pivot = compute_feasibilitygap(lp, (MYBOOL)!primal);
      report(lp, NORMAL, "performiteration: Feasibility gap at iteration %d is " MPSVALUEMASK "\n",
                         get_total_iter(lp), pivot);
    }
    else
      report(lp, NORMAL,
          "performiteration: Current objective function value at iteration %d is " MPSVALUEMASK "\n",
          get_total_iter(lp), lp->rhs[0]);
  }

  return( minitStatus );

} /* performiteration */

STATIC REAL get_refactfrequency(lprec *lp, MYBOOL final)
{
  int iters, refacts;

  /* Get numerator and divisor information */
  iters   = (lp->total_iter+lp->current_iter) - (lp->total_bswap+lp->current_bswap);
  refacts = lp->bfp_refactcount(lp);

  /* Return frequency for different cases:
      1) Actual frequency in case final statistic is desired
      2) Dummy if we are in a B&B process
      3) Frequency with added initialization offsets which
         are diluted in course of the solution process */
  if(final)
    return( (REAL) (iters) / MAX(1,refacts) );
  else if(lp->bb_totalnodes > 0)
    return( (REAL) lp->bfp_pivotmax(lp) );
  else
    return( (REAL) (lp->bfp_pivotmax(lp)+iters) / (1+refacts) );
}

MYBOOL is_fixedvar(lprec *lp, int variable)
{
  return( (MYBOOL) (lp->upbo[variable]-lp->lowbo[variable] < lp->epsprimal) );
} /* is_fixedvar */

STATIC MYBOOL solution_is_int(lprec *lp, int i, MYBOOL checkfixed)
{
#if 1
  return( (MYBOOL) (isINT(lp, lp->solution[i]) && (!checkfixed || is_fixedvar(lp, i))) );
#else
  if(isINT(lp, lp->solution[i])) {
    if(checkfixed)
      return(is_fixedvar(lp, i));
    else
      return(TRUE);
  }
  return(FALSE);
#endif
} /* solution_is_int */


MYBOOL __WINAPI set_multiprice(lprec *lp, int multiblockdiv)
{
  /* See if we are resetting multiply priced column structures */
  if(multiblockdiv != lp->multiblockdiv) {
    if(multiblockdiv < 1)
      multiblockdiv = 1;
    lp->multiblockdiv = multiblockdiv;
    FREE(lp->multivar);
  }
  return( TRUE );
}

int __WINAPI get_multiprice(lprec *lp, MYBOOL getabssize)
{
  if(lp->multiused == 0)
    return( 0 );
  if(getabssize)
    return( lp->multisize );
  else
    return( lp->multiblockdiv );
}

MYBOOL __WINAPI set_partialprice(lprec *lp, int blockcount, int *blockstart, MYBOOL isrow)
{
  int        ne, i, items;
  partialrec **blockdata;

  /* Determine partial target (rows or columns) */
  if(isrow)
    blockdata = &(lp->rowblocks);
  else
    blockdata = &(lp->colblocks);

  /* See if we are resetting partial blocks */
  ne = 0;
  items = IF(isrow, lp->rows, lp->columns);
  if(blockcount == 1)
    partial_freeBlocks(blockdata);

  /* Set a default block count if this was not specified */
  else if(blockcount <= 0) {
    blockstart = NULL;
    if(items < DEF_PARTIALBLOCKS*DEF_PARTIALBLOCKS)
      blockcount = items / DEF_PARTIALBLOCKS + 1;
    else
      blockcount = DEF_PARTIALBLOCKS;
    ne = items / blockcount;
    if(ne * blockcount < items)
      ne++;
  }

  /* Fill partial block arrays;
     Note: These will be modified during preprocess to reflect
           presolved columns and the handling of slack variables. */
  if(blockcount > 1) {

    /* Provide for extra block with slack variables in the column mode */
    i = 0;
    if(!isrow)
      i++;

    /* (Re)-allocate memory */
    if(*blockdata == NULL)
      *blockdata = partial_createBlocks(lp, isrow);
    allocINT(lp, &((*blockdata)->blockend), blockcount+i+1, AUTOMATIC);
    allocINT(lp, &((*blockdata)->blockpos), blockcount+i+1, AUTOMATIC);

    /* Copy the user-provided block start positions */
    if(blockstart != NULL) {
      MEMCOPY((*blockdata)->blockend+i, blockstart, blockcount+i+1);
      if(!isrow) {
        blockcount++;
        (*blockdata)->blockend[0] = 1;
        for(i = 1; i < blockcount; i++)
          (*blockdata)->blockend[i] += lp->rows;
      }
    }

    /* Fill the block ending positions if they were not specified */
    else {
      (*blockdata)->blockpos[0] = 1;
      if(ne == 0) {
        ne = items / blockcount;
        if(ne * blockcount < items)
          ne++;
      }
      i = 1;
      if(!isrow) {
        (*blockdata)->blockend[i] = (*blockdata)->blockend[i-1]+lp->rows;
        blockcount++;
        i++;
        items += lp->rows;
      }
      for(; i < blockcount; i++)
        (*blockdata)->blockend[i] = (*blockdata)->blockend[i-1]+ne;

      /* Handle terminal block as residual */
      (*blockdata)->blockend[blockcount] = items+1;
      if((*blockdata)->blockend[blockcount] >= (*blockdata)->blockend[blockcount-1]) {
        (*blockdata)->blockend[blockcount-1] = (*blockdata)->blockend[blockcount];
        blockcount--;
      }
    }

    /* Fill starting positions */
    for(i = 1; i <= blockcount; i++)
      (*blockdata)->blockpos[i] = (*blockdata)->blockend[i-1];

    /* Reset starting position */
    (*blockdata)->blockcount = blockcount;
    (*blockdata)->blocknow = 1;
  }

  return( TRUE );
} /* set_partialprice */

void __WINAPI get_partialprice(lprec *lp, int *blockcount, int *blockstart, MYBOOL isrow)
{
  partialrec *blockdata;

  /* Determine partial target (rows or columns) */
  if(isrow)
    blockdata = lp->rowblocks;
  else
    blockdata = lp->colblocks;

  *blockcount = partial_countBlocks(lp, isrow);
  if((blockdata != NULL) && (blockstart != NULL)) {
    int i = 0, k = *blockcount;
    if(!isrow)
      i++;
    MEMCOPY(blockstart, blockdata->blockend + i, k - i);
    if(!isrow) {
      k -= i;
      for(i = 0; i < k; i++)
        blockstart[i] -= lp->rows;
    }
  }
}


/* Solution-related function */
STATIC void update_reducedcosts(lprec *lp, MYBOOL isdual, int leave_nr, int enter_nr, REAL *prow, REAL *drow)
{
  /* "Fast" update of the dual reduced cost vector; note that it must be called
     after the pivot operation and only applies to a major "true" iteration */
  int  i;
  REAL hold;

  if(isdual) {
    hold = -drow[enter_nr]/prow[enter_nr];
    for(i=1; i <= lp->sum; i++)
      if(!lp->is_basic[i]) {
        if(i == leave_nr)
          drow[i] = hold;
        else {
          drow[i] += hold*prow[i];
          my_roundzero(drow[i], lp->epsmachine);
        }
      }
  }
  else
    report(lp, SEVERE, "update_reducedcosts: Cannot update primal reduced costs!\n");
}

STATIC void compute_reducedcosts(lprec *lp, MYBOOL isdual, int row_nr, REAL *prow, int *nzprow,
                                                                       REAL *drow, int *nzdrow)
{
/*  REAL epsvalue = lp->epsmachine; */
  REAL epsvalue = lp->epsdual;

  if(isdual) {
    bsolve_xA2(lp, row_nr, prow, epsvalue, nzprow,  /* Calculate net sensitivity given a leaving variable */
                        0, drow, epsvalue, nzdrow); /* Calculate the net objective function values */
  }
  else {
    int rc_range = XRESULT_RC;
    /* Do the phase 1 logic */
    if(lp->Extrap != 0) {
      REAL *vtemp;
      allocREAL(lp, &vtemp, lp->sum+1, FALSE);
      bsolve(lp, 0, vtemp, NULL, epsvalue*DOUBLEROUND, 1.0);
      prod_xA(lp, SCAN_SLACKVARS+SCAN_USERVARS+
/*                  SCAN_PARTIALBLOCK+ */
                  USE_NONBASICVARS+
                  OMIT_FIXED,
                  vtemp, NULL, rc_range, epsvalue, 1.0,
                  drow, nzdrow);
      FREE(vtemp);
    }
    /* Do the phase 2 logic */
    else {
      bsolve(lp, 0, drow, NULL, epsvalue*DOUBLEROUND, 1.0);
#ifdef UseSparseReducedCost
      prod_xA(lp, SCAN_SLACKVARS+SCAN_USERVARS+
#else
      prod_xA(lp,                SCAN_USERVARS+
#endif
/*                  SCAN_PARTIALBLOCK+ */
                  USE_NONBASICVARS+
                  OMIT_FIXED,
                  drow, NULL, rc_range, epsvalue, 1.0,
                  drow, nzdrow);
    }
  }
#ifdef Paranoia
  if(FALSE && lp->spx_trace) {
    if(prow != NULL)
    blockWriteREAL(lp->outstream, "compute_reducedcosts: prow", prow, 1, lp->sum);
    if(drow != NULL)
    blockWriteREAL(lp->outstream, "compute_reducedcosts: drow", drow, 1, lp->sum);
  }
#endif
}

STATIC void construct_solution(lprec *lp, REAL *target)
{
  int     i, j, basi;
  REAL    f, epsvalue = lp->epsprimal;
  REAL    *solution;
  MATitem *matelm;

  if(target == NULL)
    solution = lp->solution;
  else
    solution = target;

  /* Initialize OF and slack variables. */
  for(i = 0; i <= lp->rows; i++) {
#ifdef LegacySlackDefinition
    if(i == 0)
      solution[i] = unscaled_value(lp, -lp->orig_rh[i], i);
    else
      solution[i] = 0;
#else
    solution[i] = lp->orig_rh[i];
    if((i > 0) && !lp->is_basic[i] && !lp->is_lower[i])
      solution[i] -= my_chsign(is_chsign(lp, i), fabs(lp->upbo[i]));
    solution[i] = unscaled_value(lp, -solution[i], i);
#endif
  }

  /* Initialize user variables to their lower bounds. */
  for(i = lp->rows+1; i <= lp->sum; i++)
    solution[i] = lp->lowbo[i];

  /* Add values of user basic variables. */
  for(i = 1; i <= lp->rows; i++) {
    basi = lp->var_basic[i];
    if(basi > lp->rows)
      solution[basi] += lp->rhs[i];
  }

  /* 1. Adjust non-basic variables at their upper bounds,
     2. Unscale all user variables,
     3. Optionally do precision management. */
  for(i = lp->rows + 1; i <= lp->sum; i++) {
    if(!lp->is_basic[i] && !lp->is_lower[i])
      solution[i] += lp->upbo[i];
    solution[i] = unscaled_value(lp, solution[i], i);
#ifdef xImproveSolutionPrecision
    if(is_int(lp, i-lp->rows))
      solution[i] = restoreINT(solution[i], lp->epsint);
    else
      solution[i] = restoreINT(solution[i], lp->epsprimal);
#endif
  }

  /* Compute the OF and slack values "in extentio" */
  for(j = 1; j <= lp->columns; j++) {
    f = solution[lp->rows + j];
    if(f != 0) {
      i = lp->matA->col_end[j-1];
      basi = lp->matA->col_end[j];
      for(matelm = lp->matA->col_mat+i; i < basi; i++, matelm++)
        solution[(*matelm).row_nr] += f * unscaled_mat(lp, (*matelm).value, (*matelm).row_nr, j);
    }
  }

  /* Do slack precision management and sign reversal if necessary */
  for(i = 0; i <= lp->rows; i++) {
#ifdef ImproveSolutionPrecision
    my_roundzero(solution[i], epsvalue);
#endif
    if(is_chsign(lp, i))
      solution[i] = my_flipsign(solution[i]);
  }

 /* Record the best real-valued solution and compute a simple MIP solution limit */
  if(target == NULL) {
    if(fabs(lp->real_solution) >= lp->infinite) {
      lp->real_solution = solution[0];
      lp->bb_limitOF = lp->real_solution;

      /* Do MIP-related tests and computations */
      if((lp->int_count > 0) && mat_validate(lp->matA)) {
        REAL   fixedOF = unscaled_value(lp, lp->orig_rh[0], 0);
        MATrec *mat = lp->matA;

        /* Check if we have an all-integer OF */
        basi = mat->row_end[0];
        for(i = 0; i < basi; i++) {
          j = ROW_MAT_COL(mat->row_mat[i]);
          f = get_mat_byindex(lp, i, TRUE)+lp->epsint/2;
          f = fabs(f-floor(f));
          if(!is_int(lp, j) || (f > lp->epsint))
            break;
        }

        /* If so, we can round up the fractional OF */
        if(i == basi) {
          f = my_chsign(is_maxim(lp), lp->real_solution) + fixedOF;
          f = floor(f+(1-epsvalue));
          lp->bb_limitOF = my_chsign(is_maxim(lp), f - fixedOF);
        }

        /* Check that a user limit on the OF is feasible */
        if(my_chsign(is_maxim(lp), my_reldiff(lp->best_solution[0],lp->bb_limitOF)) < -epsvalue) {
          lp->spx_status = INFEASIBLE;
          lp->bb_break = TRUE;
        }
      }
    }
  }

} /* construct_solution */

STATIC int check_solution(lprec *lp, int  lastcolumn, REAL *solution,
                          REAL *upbo, REAL *lowbo, REAL tolerance)
{
  REAL  test, value, diff, maxerr = 0.0;
  int   i,n, errlevel = IMPORTANT, errlimit = 10;

  report(lp, NORMAL, " \n");
  report(lp, NORMAL, "lp_solve solution " MPSVALUEMASK " final at iteration %7d, %6d nodes explored\n",
         solution[0], lp->total_iter, lp->bb_totalnodes);

 /* Check if solution values are within the bounds; allowing a margin for numeric errors */
  n = 0;
  for(i = lp->rows + 1; i <= lp->rows+lastcolumn; i++) {

    value = solution[i];

    /* Check for case where we are testing an intermediate solution
       (variables shifted to the origin) */
    if(lowbo == NULL)
      test = 0;
    else
      test = unscaled_value(lp, lowbo[i], i);

    diff = my_reldiff(value, test);
    if((diff < -tolerance) && !(fabs(lp->var_is_sc[i - lp->rows]) > 0))  {
      maxerr = MAX(maxerr, fabs(value-test));
      if(n < errlimit)
      report(lp, errlevel,
        "check_solution: Variable   %s = " MPSVALUEMASK " is below its lower bound " MPSVALUEMASK "\n",
         get_col_name(lp, i-lp->rows), (double)value, (double)test);
      n++;
    }

    test = unscaled_value(lp, upbo[i], i);
    diff = my_reldiff(value, test);
    if(diff > tolerance) {
      maxerr = MAX(maxerr, fabs(value-test));
      if(n < errlimit)
      report(lp, errlevel,
         "check_solution: Variable   %s = " MPSVALUEMASK " is above its upper bound " MPSVALUEMASK "\n",
         get_col_name(lp, i-lp->rows), (double)value, (double)test);
      n++;
    }
  }

 /* Check if constraint values are within the bounds; allowing a margin for numeric errors */
  for(i = 1; i <= lp->rows; i++) {

    value = solution[i];

    test = lp->orig_rh[i];
    if(is_chsign(lp, i)) {
      test = my_flipsign(test);
      test += fabs(upbo[i]);
    }
    test = unscaled_value(lp, test, i);
#ifndef LegacySlackDefinition
    value += test;
#endif
    diff = my_reldiff(value, test);
    if(diff > tolerance) {
      maxerr = MAX(maxerr, fabs(value-test));
      if(n < errlimit)
      report(lp, errlevel,
        "check_solution: Constraint %s = " MPSVALUEMASK " is above its %s " MPSVALUEMASK "\n",
        get_row_name(lp, i), (double)value,
        (is_constr_type(lp, i, EQ) ? "equality of" : "upper bound"), (double)test);
      n++;
    }

    value = solution[i];

    if(is_chsign(lp, i)) {
      test = lp->orig_rh[i];
      test = my_flipsign(test);
    }
    else {
      test = lp->orig_rh[i]-fabs(upbo[i]);
#ifndef LegacySlackDefinition
      value = fabs(upbo[i]) - value;
#endif
    }
    test = unscaled_value(lp, test, i);
#ifndef LegacySlackDefinition
    value += test;
#endif
    diff = my_reldiff(value, test);
    if(diff < -tolerance) {
      maxerr = MAX(maxerr, fabs(value-test));
      if(n < errlimit)
      report(lp, errlevel,
        "check_solution: Constraint %s = " MPSVALUEMASK " is below its %s " MPSVALUEMASK "\n",
        get_row_name(lp, i), (double)value,
        (is_constr_type(lp, i, EQ) ? "equality of" : "lower bound"), (double)test);
      n++;
    }
  }

  if(n > 0) {
    report(lp, IMPORTANT, "check_solution: %d accuracy errors (|max|=%g, invpivot=%d)\n",
               n, maxerr, lp->bfp_pivotcount(lp));
    return(NUMFAILURE);
  }
  else
    return(OPTIMAL);

} /* check_solution */

STATIC void transfer_solution_var(lprec *lp, int uservar)
{
  if(lp->varmap_locked && (MYBOOL) ((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE)) {
    uservar += lp->rows;
    lp->full_solution[lp->orig_rows+lp->var_to_orig[uservar]] = lp->best_solution[uservar];
  }
}
STATIC void transfer_solution(lprec *lp, MYBOOL dofinal)
{
  int i;

  MEMCOPY(lp->best_solution, lp->solution, lp->sum + 1);

  /* Round integer solution values to actual integers */
  if(is_integerscaling(lp))
  for(i = 1; i <= lp->columns; i++)
    if(is_int(lp, i))
      lp->best_solution[lp->rows + i] = floor(lp->best_solution[lp->rows + i] + 0.5);

  /* Transfer to full solution vector in the case of presolved eliminations */
  if(dofinal && lp->varmap_locked &&
     (MYBOOL) ((lp->do_presolve & PRESOLVE_LASTMASKMODE) != PRESOLVE_NONE)) {
    lp->full_solution[0] = lp->best_solution[0];
    for(i = 1; i <= lp->rows; i++)
      lp->full_solution[lp->var_to_orig[i]] = lp->best_solution[i];
    for(i = 1; i <= lp->columns; i++)
      lp->full_solution[lp->orig_rows+lp->var_to_orig[lp->rows+i]] = lp->best_solution[lp->rows+i];
  }

}

STATIC MYBOOL calculate_duals(lprec *lp)
{
  int i;
  REAL scale0;

  if(lp->duals != NULL) {
    FREE(lp->duals);
  }

  if(lp->doRebase || lp->doInvert || (!lp->basis_valid))
    return(FALSE);

  allocREAL(lp, &lp->duals, lp->sum + 1, AUTOMATIC);
  if(lp->duals == NULL) {
    lp->spx_status = NOMEMORY;
    return(FALSE);
  }

  /* Initialize */
  bsolve(lp, 0, lp->duals, NULL, lp->epsmachine*DOUBLEROUND, 1.0);
  prod_xA(lp, SCAN_USERVARS+USE_NONBASICVARS, lp->duals, NULL, XRESULT_FREE, lp->epsmachine, 1.0,
                                              lp->duals, NULL);

  /* The dual values are the reduced costs of the slacks */
  /* When the slack is at its upper bound, change the sign. */
  for(i = 1; i <= lp->rows; i++) {
    if(lp->is_basic[i])
      lp->duals[i] = 0;
    /* Added a test if variable is different from 0 because sometime you get -0 and this
       is different from 0 on for example INTEL processors (ie 0 != -0 on INTEL !) PN */
    else if((is_chsign(lp, 0) == is_chsign(lp, i)) && lp->duals[i])
      lp->duals[i] = my_flipsign(lp->duals[i]);
  }
  if (is_maxim(lp))
    for(i = lp->rows + 1; i <= lp->sum; i++)
      lp->duals[i] = my_flipsign(lp->duals[i]);

  /* Scaling-related changes for v4.0 */
  if (lp->scaling_used)
    scale0 = lp->scalars[0];
  else
    scale0 = 1;
  for(i = 1; i <= lp->sum; i++) {
    lp->duals[i] /= scale0;
    lp->duals[i] = scaled_value(lp, lp->duals[i], i);
    my_roundzero(lp->duals[i], lp->epsprimal);
  }

  return(TRUE);
} /* calculate_duals */

/* Calculate sensitivity duals */
STATIC MYBOOL calculate_sensitivity_duals(lprec *lp)
{
  int k,varnr, ok = TRUE;
  REAL *pcol,a,infinite,epsvalue,from,till;

  /* one column of the matrix */
  allocREAL(lp, &pcol, lp->rows + 1, TRUE);
  if(lp->dualsfrom != NULL) {
    FREE(lp->dualsfrom);
  }
  if(lp->dualstill != NULL) {
    FREE(lp->dualstill);
  }
  allocREAL(lp, &lp->dualsfrom, lp->sum + 1, AUTOMATIC);
  allocREAL(lp, &lp->dualstill, lp->sum + 1, AUTOMATIC);
  if((pcol == NULL) ||
     (lp->dualsfrom == NULL) ||
     (lp->dualstill == NULL)) {
    if(pcol != NULL)
      FREE(pcol);
    if(lp->dualsfrom != NULL)
      FREE(lp->dualsfrom);
    if(lp->dualstill != NULL)
      FREE(lp->dualstill);
    lp->spx_status = NOMEMORY;
    ok = FALSE;
  }
  else {
    infinite=lp->infinite;
    epsvalue=lp->epsmachine;
    for(varnr=1; varnr<=lp->sum; varnr++) {
      from=infinite;
      till=infinite;
      if ((!lp->is_basic[varnr]) /* && ((varnr<=lp->rows) || (fabs(lp->solution[varnr])>epsvalue)) */) {
        if (!fsolve(lp, varnr, pcol, lp->workrowsINT, epsvalue, 1.0, FALSE)) {  /* construct one column of the tableau */
          ok = FALSE;
          break;
        }
        /* Search for the rows(s) which first result in further iterations */
        for (k=1; k<=lp->rows; k++) {
          if (fabs(pcol[k])>epsvalue) {
            a = unscaled_value(lp, lp->rhs[k]/pcol[k], varnr);
            if ((a<=0.0) && (pcol[k]<0.0) && (-a<from)) from=my_flipsign(a);
            if ((a>=0.0) && (pcol[k]>0.0) && ( a<till)) till= a;
            if (lp->upbo[lp->var_basic[k]] < infinite) {
              a = (REAL) ((lp->rhs[k]-lp->upbo[lp->var_basic[k]])/pcol[k]);
              a = unscaled_value(lp, a, varnr);
              if ((a<=0.0) && (pcol[k]>0.0) && (-a<from)) from=my_flipsign(a);
              if ((a>=0.0) && (pcol[k]<0.0) && ( a<till)) till= a;
            }
          }
        }
        if (!lp->is_lower[varnr]) {
          a=from;
          from=till;
          till=a;
        }
        if ((varnr<=lp->rows) && (!is_chsign(lp, varnr))) {
          a=from;
          from=till;
          till=a;
        }
      }
      if (from!=infinite)
        lp->dualsfrom[varnr]=lp->solution[varnr]-from;
      else
        lp->dualsfrom[varnr]=-infinite;
      if (till!=infinite)
        lp->dualstill[varnr]=lp->solution[varnr]+till;
      else
        lp->dualstill[varnr]=infinite;
    }
    FREE(pcol);
  }
  return((MYBOOL) ok);
} /* calculate_sensitivity_duals */

/* Calculate sensitivity objective function */
STATIC MYBOOL calculate_sensitivity_obj(lprec *lp)
{
  int i,l,varnr,row_nr,ok = TRUE;
  REAL *OrigObj = NULL,*drow = NULL,*prow = NULL,a,min1,min2,infinite,epsvalue,from,till;

  /* objective function */
  allocREAL(lp, &drow, lp->sum + 1, TRUE);
  allocREAL(lp, &OrigObj, lp->columns + 1, FALSE);
  allocREAL(lp, &prow, lp->sum + 1, TRUE);
  if(lp->objfrom != NULL) {
    FREE(lp->objfrom);
  }
  if(lp->objtill != NULL) {
    FREE(lp->objtill);
  }
  allocREAL(lp, &lp->objfrom, lp->columns + 1, AUTOMATIC);
  allocREAL(lp, &lp->objtill, lp->columns + 1, AUTOMATIC);
  if ((drow == NULL) ||
      (OrigObj == NULL) ||
      (prow == NULL)) {
    if(drow != NULL)
      FREE(drow);
    if(OrigObj != NULL)
      FREE(OrigObj);
    if(prow != NULL)
      FREE(prow);
    if(lp->objfrom != NULL)
      FREE(lp->objfrom);
    if(lp->objtill != NULL)
      FREE(lp->objtill);
    lp->spx_status = NOMEMORY;
    ok = FALSE;
  }
  else {
    infinite=lp->infinite;
    epsvalue=lp->epsmachine;

    bsolve(lp, 0, drow, NULL, epsvalue*DOUBLEROUND, 1.0);
    prod_xA(lp, SCAN_USERVARS+USE_NONBASICVARS, drow, NULL, XRESULT_FREE, epsvalue, 1.0,
                                                drow, NULL);

    /* original (unscaled) objective function */
    get_row(lp, 0, OrigObj);
    for(i = 1; i <= lp->columns; i++) {
      from=-infinite;
      till= infinite;
      varnr = lp->rows + i;
      if (!lp->is_basic[varnr]) {
      /* only the coeff of the objective function of column i changes. */
        a = unscaled_mat(lp, drow[varnr], 0, i);
        if (lp->upbo[varnr] == 0.0)
          /* ignore, because this case doesn't results in further iterations */ ;
        else if (lp->is_lower[varnr])
          from = OrigObj[i] - a; /* less than this value gives further iterations */
        else
          till = OrigObj[i] - a; /* bigger than this value gives further iterations */
      }
      else {
      /* all the coeff of the objective function change. Search the minimal change needed for further iterations */
        for (row_nr=1;
             (row_nr<=lp->rows) && (lp->var_basic[row_nr]!=varnr); row_nr++)
          /* Search on which row the variable exists in the basis */ ;
        if (row_nr<=lp->rows) {       /* safety test; should always be found ... */
          /* Construct one row of the tableau */
          bsolve(lp, row_nr, prow, NULL, epsvalue*DOUBLEROUND, 1.0);
          prod_xA(lp, SCAN_USERVARS+USE_NONBASICVARS, prow, NULL, XRESULT_FREE, epsvalue, 1.0,
                                                      prow, NULL);

          min1=infinite;
          min2=infinite;
          for (l=1; l<=lp->sum; l++)   /* search for the column(s) which first results in further iterations */
            if ((!lp->is_basic[l]) && (lp->upbo[l]>0.0) &&
                (fabs(prow[l])>epsvalue) && (drow[l]*(lp->is_lower[l] ? -1 : 1)<epsvalue)) {
              a = unscaled_mat(lp, fabs(drow[l] / prow[l]), 0, i);
              if (prow[l]*(lp->is_lower[l] ? 1 : -1)<0.0) {
                if (a<min1) min1 = a;
              }
              else {
                if (a<min2) min2 = a;
              }
            }
          if (!lp->is_lower[varnr]) {
            a = min1;
            min1 = min2;
            min2 = a;
          }
          if (min1<infinite)
            from = OrigObj[i]-min1;
          if (min2<infinite)
            till = OrigObj[i]+min2;
          if (lp->solution[varnr]==0.0)
            till = 0.0; /* if value is 0 then there can't be an upper range */
        }
      }
      lp->objfrom[i]=from;
      lp->objtill[i]=till;
    }
  }
  if (prow !=NULL) FREE(prow);
  if (OrigObj != NULL) FREE(OrigObj);
  if (drow != NULL) FREE(drow);
  return((MYBOOL) ok);
} /* calculate_sensitivity_obj */

STATIC MYBOOL check_if_less(lprec *lp, REAL x, REAL y, int variable)
{
  if(y < x-scaled_value(lp, lp->epsint, variable)) {
    if(lp->bb_trace)
      report(lp, NORMAL, "check_if_less: Invalid new bound %g should be < %g for %s\n",
                         x, y, get_col_name(lp, variable));
    return(FALSE);
  }
  else
    return(TRUE);
}

/* Various basis utility routines */

STATIC int findNonBasicSlack(lprec *lp, MYBOOL *is_basic)
{
  int i;

  for(i = lp->rows; i > 0; i--)
    if(!is_basic[i])
      break;
  return( i );
}

STATIC int findBasisPos(lprec *lp, int notint, int *var_basic)
{
  int i;

  if(var_basic == NULL)
    var_basic = lp->var_basic;
  for(i = lp->rows; i > 0; i--)
    if(var_basic[i] == notint)
      break;
  return( i );
}

STATIC void replaceBasisVar(lprec *lp, int rownr, int var, int *var_basic, MYBOOL *is_basic)
{
  int out;

  out = var_basic[rownr];
  var_basic[rownr] = var;
  is_basic[out] = FALSE;
  is_basic[var] = TRUE;
}

/* Transform RHS by adjusting for the bound state of variables;
   optionally rebase upper bound, and account for this in later calls */
STATIC void initialize_solution(lprec *lp, MYBOOL shiftbounds)
{
  int     i, k1, k2, rownr, varnr;
  REAL    theta, value, loB, upB;
  MATitem *matentry;

  /* Set bounding status indicators */
  if(lp->bb_bounds != NULL) {
    if(shiftbounds == INITSOL_SHIFTZERO) {
#ifdef Paranoia
      if(lp->bb_bounds->UBzerobased)
        report(lp, SEVERE, "initialize_solution: The upper bounds are already zero-based at refactorization %d\n",
                           lp->bfp_refactcount(lp));
#endif
      lp->bb_bounds->UBzerobased = TRUE;
    }
#ifdef Paranoia
    else if(!lp->bb_bounds->UBzerobased)
        report(lp, SEVERE, "initialize_solution: The upper bounds are not zero-based at refactorization %d\n",
                           lp->bfp_refactcount(lp));
#endif
  }

  if(sizeof(*lp->rhs) == sizeof(*lp->orig_rh)) {
    MEMCOPY(lp->rhs, lp->orig_rh, lp->rows+1);
  }
  else
    for(i = 0; i <= lp->rows; i++)
      lp->rhs[i] = lp->orig_rh[i];

/* Adjust active RHS for variables at their active upper/lower bounds */
  for(i = 1; i <= lp->sum; i++) {

    upB = lp->upbo[i];
    loB = lp->lowbo[i];

    /* Do user and artificial variables */
    if(i > lp->rows) {

      varnr = i - lp->rows;

      /* Shift to "ranged" upper bound, tantamount to defining zero-based variables */
      if(shiftbounds == INITSOL_SHIFTZERO) {
        if((loB > -lp->infinite) && (upB < lp->infinite))
          lp->upbo[i] -= loB;
        if(lp->upbo[i] < 0)
          report(lp, SEVERE, "initialize_solution: Invalid rebounding; variable %d at refactorization %d\n",
                             i, lp->bfp_refactcount(lp));
      }

      /* Use "ranged" upper bounds */
      else if (shiftbounds == INITSOL_USEZERO) {
        if((loB > -lp->infinite) && (upB < lp->infinite))
          upB += loB;
      }

      /* Shift upper bound back to original value */
      else if(shiftbounds == INITSOL_ORIGINAL) {
        if((loB > -lp->infinite) && (upB < lp->infinite)) {
          lp->upbo[i] += loB;
          upB += loB;
        }
        continue;
      }
      else
        report(lp, SEVERE, "initialize_solution: Invalid option value '%d'\n",
                           shiftbounds);

      /* Set the applicable adjustment */
      if(lp->is_lower[i])
        theta = loB;
      else
        theta = upB;


      /* Check if we need to pass through the matrix;
         remember that basis variables are always lower-bounded */
      if(theta == 0)
        continue;

      /* Get starting and ending indeces in the NZ vector */
      k1 = lp->matA->col_end[varnr - 1];
      k2 = lp->matA->col_end[varnr];

      /* Handle simplex Phase 1 offsets */
      if(k1 < k2) {
        matentry = lp->matA->col_mat + k1;
        rownr = (*matentry).row_nr;
        value = (*matentry).value;
        if(rownr > 0) {
          value = 0;
          if(modifyOF1(lp, i, &value, theta))
            lp->rhs[0] -= value;
        }
        else
          k1++;
      }
      else {
        rownr = 0;
        value = 0;
      }
      if((rownr == 0) && modifyOF1(lp, i, &value, theta))
        lp->rhs[rownr] -= value;

      /* Do the normal case */
      for(matentry = lp->matA->col_mat + k1; k1 < k2; k1++, matentry++) {
        rownr = (*matentry).row_nr;
        value = (*matentry).value;
        lp->rhs[rownr] -= theta * value;
      }
    }
    else {
      /* Note that finite-bounded constraints are "natively" stored in the range format */
      if(!lp->is_lower[i])
        lp->rhs[i] -= upB;
    }
  }
  if(shiftbounds == INITSOL_SHIFTZERO)
    lp->doRebase = FALSE;

}

/* This routine recomputes the basic variables using the full inverse */
STATIC void recompute_solution(lprec *lp, MYBOOL shiftbounds)
{
  REAL roundzero = lp->epsmachine;

  /* Compute RHS = b - A(n)*x(n) */
  initialize_solution(lp, shiftbounds);

  /* Compute x(b) = Inv(B)*RHS (Ref. lp_solve inverse logic and Chvatal p. 121) */
  lp->bfp_ftran_normal(lp, lp->rhs, NULL);

 /* Round the values (should not be greater than the factor used in inv_pivotRHS) */
  roundVector(lp->rhs, lp->rows, roundzero);

  lp->doRecompute = FALSE;
}

/* This routine compares an existing basic solution to a recomputed one;
   Note that the routine must provide for the possibility that the order of the
   basis variables can be changed by the inversion engine. */
STATIC int verify_solution(lprec *lp, MYBOOL reinvert, char *info)
{
  int  i, ii, n, *oldmap, *newmap, *refmap = NULL;
  REAL *oldrhs, err, errmax;

  allocINT(lp, &oldmap, lp->rows+1, FALSE);
  allocINT(lp, &newmap, lp->rows+1, FALSE);
  allocREAL(lp, &oldrhs, lp->rows+1, FALSE);

  /* Get sorted mapping of the old basis */
  for(i = 0; i <= lp->rows; i++)
    oldmap[i] = i;
  if(reinvert) {
    allocINT(lp, &refmap, lp->rows+1, FALSE);
    MEMCOPY(refmap, lp->var_basic, lp->rows+1);
    sortByINT(oldmap, refmap, lp->rows, 1, TRUE);
  }

  /* Save old and calculate the new RHS vector */
  MEMCOPY(oldrhs, lp->rhs, lp->rows+1);
  if(reinvert) {
    lp->doInvert = TRUE;
    invert(lp, INITSOL_USEZERO);
  }
  else
    recompute_solution(lp, INITSOL_USEZERO);

  /* Get sorted mapping of the new basis */
  for(i = 0; i <= lp->rows; i++)
    newmap[i] = i;
  if(reinvert) {
    MEMCOPY(refmap, lp->var_basic, lp->rows+1);
    sortByINT(newmap, refmap, lp->rows, 1, TRUE);
  }

  /* Identify any gap */
  errmax = 0;
  ii = -1;
  n = 0;
  for(i = lp->rows; i > 0; i--) {
    err = fabs(my_reldiff(oldrhs[oldmap[i]], lp->rhs[newmap[i]]));
    if(err > lp->epsprimal) {
      n++;
      if(err > errmax) {
        ii = i;
        errmax = err;
      }
    }
  }
  err = fabs(my_reldiff(oldrhs[i], lp->rhs[i]));
  if(err < lp->epspivot) {
    i--;
    err = 0;
  }
  else {
    n++;
    if(ii < 0) {
      ii = 0;
      errmax = err;
    }
  }
  if(n > 0) {
    report(lp, IMPORTANT, "verify_solution: Iteration %d %s - %d errors; OF %g, Max @row %d %g\n",
                           get_total_iter(lp), my_if(info == NULL, "", info), n, err, newmap[ii], errmax);
  }
  /* Copy old results back (not possible for inversion) */
  if(!reinvert)
    MEMCOPY(lp->rhs, oldrhs, lp->rows+1);

  FREE(oldmap);
  FREE(newmap);
  FREE(oldrhs);
  if(reinvert)
    FREE(refmap);

  return( ii );

}

/* Preprocessing and postprocessing functions */
STATIC int identify_GUB(lprec *lp, MYBOOL mark)
{
  int    i, j, jb, je, k, knint;
  REAL   rh, mv, tv, bv;
  MATrec *mat = lp->matA;

  if((lp->equalities == 0) || !mat_validate(mat))
    return( 0 );

  k = 0;
  for(i = 1; i <= lp->rows; i++) {

    /* Check if it is an equality constraint */
    if(get_constr_type(lp, i) != EQ)
      continue;

    rh = get_rh(lp, i);
    knint = 0;
    je = mat->row_end[i];
    for(jb = mat->row_end[i-1]; jb < je; jb++) {
      j = ROW_MAT_COL(mat->row_mat[jb]);

      /* Check for validity of the equation elements */
      if(!is_int(lp, j))
        knint++;
      if(knint > 1)
        break;

      mv = get_mat_byindex(lp, jb, TRUE);
      if(fabs(my_reldiff(mv, rh)) > lp->epsprimal)
        break;

      tv = get_upbo(lp, j);
      bv = get_lowbo(lp, j);
      if((fabs(my_reldiff(mv*tv, rh)) > lp->epsprimal) || (bv != 0))
        break;
    }

    /* Update GUB count and optionally mark the GUB */
    if(jb == je) {
      k++;
      if(mark)
        lp->row_type[i] |= ROWTYPE_GUB;
    }

  }
  return( k );
}

STATIC int prepare_GUB(lprec *lp)
{
  int    i, j, jb, je, k, *members = NULL;
  REAL   rh;
  char   GUBname[16];
  MATrec *mat = lp->matA;

  if((lp->equalities == 0) ||
     !allocINT(lp, &members, lp->columns+1, TRUE) ||
     !mat_validate(mat))
    return( 0 );

  for(i = 1; i <= lp->rows; i++) {

    /* Check if it has been marked as a GUB */
    if(!(lp->row_type[i] & ROWTYPE_GUB))
      continue;

    /* Pick up the GUB column indeces */
    k = 0;
    je = mat->row_end[i];
    for(jb = mat->row_end[i-1], k = 0; jb < je; jb++) {
      members[k] = ROW_MAT_COL(mat->row_mat[jb]);
      k++;
    }

    /* Add the GUB */
    j = GUB_count(lp) + 1;
    sprintf(GUBname, "GUB_%d", i);
    add_GUB(lp, GUBname, j, k, members);

    /* Standardize coefficients to 1 if necessary */
    rh = get_rh(lp, i);
    if(fabs(my_reldiff(rh, 1)) > lp->epsprimal) {
      lp_solve_set_rh(lp, i, 1);
      for(jb = mat->row_end[i-1]; jb < je; jb++) {
        j = ROW_MAT_COL(mat->row_mat[jb]);
        lp_solve_set_mat(lp, i,j, 1);
      }
    }

  }
  FREE(members);
  return(GUB_count(lp));
}

int preprocess(lprec *lp)
{
  int    i, j, k = 0, ok=TRUE;
  REAL   *new_column, hold;
  MYBOOL scaled;

 /* do not process if already preprocessed */
  if(lp->wasPreprocessed)
    return(ok);

  /* Write model statistics */
  if(lp->lag_status != RUNNING) {
    report(lp, NORMAL, "Model run %4d for name:  %s\n", lp->solvecount, lp->lp_name);

#ifdef EnableBranchingOnGUB
    if(is_bb_mode(lp, NODE_GUBMODE))
      k = identify_GUB(lp, TRUE);
#endif

    j = 0;
    for(i = 1; i <= lp->rows; i++)
      if(is_constr_type(lp,i, EQ))
        j++;

    report(lp, NORMAL, "Objective:   %simize(%s)\n",
           my_if(is_maxim(lp), "Max", "Min"), get_row_name(lp, 0));
    report(lp, NORMAL, "\nModel size:  %5d constraints, %5d variables,   %8d non-zeros.\n",
           lp->rows, lp->columns, get_nonzeros(lp));
      report(lp, NORMAL, "Constraints: %5d equality,    %5d GUB,            %5d SOS.\n",
           j, k, SOS_count(lp));
      report(lp, NORMAL, "Variables:   %5d integer,     %5d semi-cont.,     %5d SOS.\n",
           lp->int_count, lp->sc_count, lp->sos_vars);
    report(lp, NORMAL, " \n");

#ifdef EnablePartialOptimization
    if(is_piv_mode(lp, PRICE_AUTOPARTIALCOLS) && (partial_findBlocks(lp, TRUE, FALSE) > 1)) {
      i = partial_countBlocks(lp, FALSE);
      if(i > 1)
        report(lp, NORMAL, "The model is estimated to have %d column blocks/stages.\n",
                           i);
    }
    if(is_piv_mode(lp, PRICE_AUTOPARTIALROWS) && (partial_findBlocks(lp, TRUE, TRUE) > 1)) {
      i = partial_countBlocks(lp, TRUE);
      if(i > 1)
        report(lp, NORMAL, "The model is estimated to have %d row blocks/stages.\n",
                           i);
    }
#endif
    i = (lp->simplex_strategy & SIMPLEX_Phase1_DUAL);
    j = (lp->simplex_strategy & SIMPLEX_Phase2_DUAL);
    report(lp, NORMAL, "Using %s simplex for phase 1 and %s simplex for phase 2.\n",
                       my_if(i == 0, "PRIMAL", "DUAL"), my_if(j == 0, "PRIMAL", "DUAL"));
    report(lp, NORMAL, " \n");
  }

  /* Compute a minimum step improvement step requirement */
  new_column = NULL;
  lp->bb_deltaOF = minimumImprovementOF(lp);

 /* First create extra columns for FR variables or flip MI variables */
  for (j = 1; j <= lp->columns; j++) {

#ifdef Paranoia
    if((lp->rows != lp->matA->rows) || (lp->columns != lp->matA->columns))
      report(lp, SEVERE, "preprocess: Inconsistent variable counts found\n");
#endif

   /* First handle variables with a negative Inf-bound by changing signs (multiply column by -1) */
    i = lp->rows + j;
    hold = lp->orig_upbo[i];
    if((hold <= 0) || ((hold < lp->infinite) &&
                       (lp->splitnegvars == AUTOMATIC) &&
                       (lp->orig_lowbo[i] == -lp->infinite)) ) {
      /* See if this should be handled via normal bound shifting */
      if((hold < 0) && (lp->orig_lowbo[i] >= lp->negrange))
        continue;
      /* Delete split sibling variable if one existed from before */
      if((lp->var_is_free != NULL) && (lp->var_is_free[j] > 0))
        del_column(lp, lp->var_is_free[j]);
      mat_multcol(lp->matA, j, -1);
      if(lp->var_is_free == NULL) {
        allocINT(lp, &lp->var_is_free, MAX(lp->columns,lp->columns_alloc) + 1, TRUE);
        if (lp->var_is_free == NULL)
          return(FALSE);
      }
      lp->var_is_free[j] = -j; /* Indicator UB and LB are switched, with no helper variable added */
      lp->orig_upbo[i] = my_flipsign(lp->orig_lowbo[i]);
      lp->orig_lowbo[i] = my_flipsign(hold);
      /* Check for presence of negative ranged SC variable */
      if(lp->var_is_sc[j] > 0) {
        lp->var_is_sc[j] = lp->orig_lowbo[i];
        lp->orig_lowbo[i] = 0;
      }
    }
   /* Then deal with -+, full-range/FREE variables */
    else if(((lp->splitnegvars != AUTOMATIC) || (lp->orig_lowbo[i] == -lp->infinite)) &&
            (lp->orig_lowbo[i] < lp->negrange)) {
      if(lp->var_is_free == NULL) {
        allocINT(lp, &lp->var_is_free, MAX(lp->columns,lp->columns_alloc) + 1, TRUE);
        if (lp->var_is_free == NULL)
          return(FALSE);
      }
      if(lp->var_is_free[j] <= 0) { /* If this variable wasn't split yet ... */
        if(SOS_is_member(lp->SOS, 0, i - lp->rows)) {   /* Added */
          report(lp, IMPORTANT, "preprocess: Converted negative bound for SOS variable %d to zero",
          i - lp->rows);
          lp->orig_lowbo[i] = 0;
          continue;
        }
        if(new_column == NULL) {
          allocREAL(lp, &new_column, lp->rows + 1, FALSE);
          if (new_column == NULL) {
            lp->spx_status = NOMEMORY;
            ok = FALSE;
            break;
          }
        }
       /* Avoid precision loss by turning off unscaling and rescaling */
       /* in get_column and add_column operations; also make sure that */
       /* full scaling information is preserved */
        scaled = lp->scaling_used;
        lp->scaling_used = FALSE;
        get_column(lp, j, new_column);
        if(!add_columnex(lp, lp->rows, new_column, NULL)) {
          ok = FALSE;
          break;
        }
        mat_multcol(lp->matA, lp->columns, -1);
        if(scaled)
          lp->scalars[lp->rows+lp->columns] = lp->scalars[i];
        lp->scaling_used = (MYBOOL) scaled;
        if(lp->names_used) {
          char fieldn[50];

          sprintf(fieldn, "__AntiBodyOf(%d)__", j);
          if(!set_col_name(lp, lp->columns, fieldn)) {
/*          if (!set_col_name(lp, lp->columns, get_col_name(lp, j))) { */
            ok = FALSE;
            break;
          }
        }
        /* Set (positive) index to the original column's split / helper and back */
        lp->var_is_free[j] = lp->columns;
      }
      lp->orig_upbo[lp->rows + lp->var_is_free[j]] = my_flipsign(lp->orig_lowbo[i]);
      lp->orig_lowbo[i] = 0;

      /* Negative index indicates x is split var and -var_is_free[x] is index of orig var */
      lp->var_is_free[lp->var_is_free[j]] = -j;
      lp->must_be_int[lp->var_is_free[j]] = lp->must_be_int[j];
    }
   /* Check for positive ranged SC variables */
    else if(lp->var_is_sc[j] > 0) {
      lp->var_is_sc[j] = lp->orig_lowbo[i];
      lp->orig_lowbo[i] = 0;
    }

   /* Tally integer variables in SOS'es */
    if(SOS_is_member(lp->SOS, 0, j) && is_int(lp, j))
      lp->sos_ints++;
  }

  if(new_column != NULL)
    FREE(new_column);

  /* Fill lists of GUB constraints, if appropriate */
#ifdef EnableBranchingOnGUB
  if(k > 0)
    prepare_GUB(lp);
#endif

  lp->wasPreprocessed = TRUE;

  return(ok);
}

void postprocess(lprec *lp)
{
  int i,ii,j;
  REAL hold;

 /* Check if the problem actually was preprocessed */
  if(!lp->wasPreprocessed)
    return;

 /* Must compute duals here in case we have free variables; note that in
    this case sensitivity analysis is not possible unless done here */
  if((MIP_count(lp) == 0) &&
     (is_presolve(lp, PRESOLVE_DUALS) || (lp->var_is_free != NULL)))
    calculate_duals(lp);
  if(is_presolve(lp, PRESOLVE_SENSDUALS)) {
    if(!calculate_sensitivity_duals(lp) || !calculate_sensitivity_obj(lp))
      report(lp, IMPORTANT, "postprocess: Unable to allocate working memory for duals.\n");
  }

 /* Loop over all columns */
  for (j = 1; j <= lp->columns; j++) {
    i = lp->rows + j;
   /* Reconstruct strictly negative values */
    if((lp->var_is_free != NULL) && (lp->var_is_free[j] < 0)) {
      /* Check if we have the simple case where the UP and LB are negated and switched */
      if(-lp->var_is_free[j] == j) {
        mat_multcol(lp->matA, j, -1);
        hold = lp->orig_upbo[i];
        lp->orig_upbo[i] = my_flipsign(lp->orig_lowbo[i]);
        lp->orig_lowbo[i] = my_flipsign(hold);
        lp->best_solution[i] = my_flipsign(lp->best_solution[i]);
        transfer_solution_var(lp, j);

        /* hold = lp->objfrom[j];
        lp->objfrom[j] = my_flipsign(lp->objtill[j]);
        lp->objtill[j] = my_flipsign(hold); */ /* under investigation <peno> */

        /* lp->duals[i] = my_flipsign(lp->duals[i]);
        hold = lp->dualsfrom[i];
        lp->dualsfrom[i] = my_flipsign(lp->dualstill[i]);
        lp->dualstill[i] = my_flipsign(hold); */ /* under investigation <peno> */
       /* Bound switch undone, so clear the status */
        lp->var_is_free[j] = 0;
       /* Adjust negative ranged SC */
        if(lp->var_is_sc[j] > 0)
          lp->orig_lowbo[lp->rows + j] = -lp->var_is_sc[j];
      }
      /* Ignore the split / helper columns (will be deleted later) */
    }
   /* Condense values of extra columns of quasi-free variables split in two */
    else if((lp->var_is_free != NULL) && (lp->var_is_free[j] > 0)) {
      ii = lp->var_is_free[j]; /* Index of the split helper var */
      /* if(lp->objfrom[j] == -lp->infinite)
        lp->objfrom[j] = -lp->objtill[ii];
      lp->objtill[ii] = lp->infinite;
      if(lp->objtill[j] == lp->infinite)
        lp->objtill[j] = my_flipsign(lp->objfrom[ii]);
      lp->objfrom[ii] = -lp->infinite; */ /* under investigation <peno> */

      ii += lp->rows;
      lp->best_solution[i] -= lp->best_solution[ii]; /* join the solution again */
      transfer_solution_var(lp, j);
      lp->best_solution[ii] = 0;

      /* if(lp->duals[i] == 0)
        lp->duals[i] = my_flipsign(lp->duals[ii]);
      lp->duals[ii] = 0;
      if(lp->dualsfrom[i] == -lp->infinite)
        lp->dualsfrom[i] = my_flipsign(lp->dualstill[ii]);
      lp->dualstill[ii] = lp->infinite;
      if(lp->dualstill[i] == lp->infinite)
        lp->dualstill[i] = my_flipsign(lp->dualsfrom[ii]);
      lp->dualsfrom[ii] = -lp->infinite; */ /* under investigation <peno> */

      /* Reset to original bound */
      lp->orig_lowbo[i] = my_flipsign(lp->orig_upbo[ii]);
    }
   /* Adjust for semi-continuous variables */
    else if(lp->var_is_sc[j] > 0) {
      lp->orig_lowbo[i] = lp->var_is_sc[j];
    }
  }

  /* Remove any split column helper variables */
  del_splitvars(lp);

  /* Do extended reporting, if specified */
  if(lp->verbose > NORMAL) {
    REPORT_extended(lp);

  }

  lp->wasPreprocessed = FALSE;
}

