#include <string.h>
#include <time.h>
#include <signal.h>
#include "lp_lib.h"
#include "patchlevel.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif

#define filetypeLP      1
#define filetypeMPS     2
#define filetypeFREEMPS 3
#define filetypeCPLEX   4
#define filetypeXLI     5

int EndOfPgr(int i)
{
#   if defined FORTIFY
      Fortify_LeaveScope();
#   endif
    exit(i);
}

void SIGABRT_func(int sig)
 {
   EndOfPgr(EXIT_FAILURE);
 }

void print_help(char *argv[])
{
  printf("Usage of %s version %d.%d.%d.%d:\n", argv[0], MAJORVERSION, MINORVERSION, RELEASE, BUILD);
  printf("%s [options] [[<]input_file]\n", argv[0]);
  printf("List of options:\n");
  printf("-h\t\tprints this message\n");
#if defined PARSER_LP
  printf("-lp\t\tread from LP file (default)\n");
#endif
  printf("-mps\t\tread from MPS file in fixed format\n");
  printf("-fmps\t\tread from MPS file in free format\n");
#if defined PARSER_CPLEX
  printf("-lpt\t\tread from ILOG CPLEX file\n");
#endif
  printf("-rxli xliname filename\n\t\tread file with xli library\n");
  printf("-rxlidata datafilename\n\t\tdata file name for xli library.\n");
  printf("-rxliopt options\n\t\toptions for xli library.\n");
  printf("-wlp filename\twrite to LP file\n");
  printf("-wmps filename\twrite to MPS file in fixed format\n");
  printf("-wfmps filename\twrite to MPS file in free format\n");
#if defined PARSER_CPLEX
  printf("-wlpt filename\twrite to ILOG CPLEX file\n");
#endif
  printf("-wxli xliname filename\n\t\twrite file with xli library\n");
  printf("-wxliopt options\n\t\toptions for xli library.\n");
  printf("-parse_only\tparse input file but do not solve\n");
  printf("\n");
  printf("-min\t\tMinimize the lp problem (overrules setting in file)\n");
  printf("-max\t\tMaximize the lp problem (overrules setting in file)\n");
  printf("-b <bound>\tspecify a lower bound for the objective function\n\t\tto the program. If close enough, may speed up the\n\t\tcalculations.\n");
  printf("-r <value>\tspecify max nbr of pivots between a re-inversion of the matrix\n");
  printf("-piv <rule>\tspecify simplex pivot rule\n");
  printf("\t -piv0: Select first\n");
  printf("\t -piv1: Select according to Dantzig\n");
  printf("\t -piv2: Select Devex pricing from Paula Harris (default)\n");
  printf("\t -piv3: Select steepest edge\n");
  printf("These pivot rules can be combined with any of the following:\n");
  printf("-pivf\t\tIn case of Steepest Edge, fall back to DEVEX in primal.\n");
  printf("-pivm\t\tMultiple pricing.\n");
  printf("-piva\t\tTemporarily use First Index if cycling is detected.\n");
  printf("-pivr\t\tAdds a small randomization effect to the selected pricer.\n");
#if defined EnablePartialOptimization
  printf("-pivp\t\tEnabled partial pricing.\n");
  printf("-pivpc\t\tEnabled partial pricing on columns.\n");
  printf("-pivpr\t\tEnabled partial pricing on rows.\n");
#endif
  printf("-pivll\t\tScan entering/leaving columns left rather than right.\n");
  printf("-pivla\t\tScan entering/leaving columns alternatingly left/right.\n");
  printf("-s <mode> <scaleloop>\tuse automatic problem scaling.\n");
  printf("\t  -s:\n");
  printf("\t -s0: Numerical range-based scaling\n");
  printf("\t -s1: Geometric scaling\n");
  printf("\t -s2: Curtis-reid scaling\n");
  printf("\t -s3: Scale to convergence using largest absolute value\n");
  printf("\t -s4: Numerical range-based scaling\n");
  printf("\t -s5: Scale to convergence using logarithmic mean of all values\n");
  printf("\t -s6: Scale based on the simple numerical range\n");
  printf("\t -s7: Scale quadratic\n");
  printf("These scaling rules can be combined with any of the following:\n");
  printf("-sp\t\talso do power scaling.\n");
  printf("-si\t\talso do Integer scaling.\n");
  printf("-se\t\talso do equilibration to scale matrix vales to the -1..1 range.\n");
  printf("-presolve\tpresolve problem before start optimizing\n");
  printf("-presolvel\talso eliminate linearly dependent rows\n");
  printf("-presolves\talso convert constraints to SOSes (only SOS1 handled)\n");
  printf("-presolver\tIf the phase 1 solution process finds that a constraint is\n\t\tredundant then this constraint is deleted\n");
  printf("-C <mode>\tbasis crash mode\n");
  printf("\t -C0: No crash basis\n");
  printf("\t -C2: Most feasible basis\n");
  printf("-prim\t\tPrefer the primal simplex for both phases.\n");
  printf("-dual\t\tPrefer the dual simplex for both phases.\n");
  printf("-simplexpp\tSet Phase1 Primal, Phase2 Primal.\n");
  printf("-simplexdp\tSet Phase1 Dual, Phase2 Primal.\n");
  printf("-simplexpd\tSet Phase1 Primal, Phase2 Dual.\n");
  printf("-simplexdd\tSet Phase1 Dual, Phase2 Dual.\n");
  printf("-degen\t\tuse perturbations to reduce degeneracy,\n\t\tcan increase numerical instability\n");
  printf("-degenc\t\tuse column check to reduce degeneracy\n");
  printf("-degend\t\tdynamic check to reduce degeneracy\n");
  printf("-trej <Trej>\tset minimum pivot value\n");
  printf("-epsd <epsd>\tset minimum tolerance for reduced costs\n");
  printf("-epsb <epsb>\tset minimum tolerance for the RHS\n");
  printf("-epsel <epsel>\tset tolerance\n");
  printf("-epsp <epsp>\tset the value that is used as perturbation scalar for\n\t\tdegenerative problems\n");
  printf("-improve <level>\titerative improvement level\n");
  printf("\t -improve0: none (default)\n");
  printf("\t -improve1: FTRAN only\n");
  printf("\t -improve2: BTRAN only\n");
  printf("\t -improve3: FTRAN + BTRAN\n");
  printf("\t -improve4: Automatic inverse accuracy control in the dual simplex\n");
  printf("-timeout <sec>\tTimeout after sec seconds when not solution found.\n");
  printf("-timeoutok\tIf timeout, take the best yet found solution.\n");
  printf("-bfp <filename>\tSet basis factorization package.\n");
  printf("\n");
  printf("-e <number>\tspecifies the epsilon which is used to determine whether a\n\t\tfloating point number is in fact an integer.\n\t\tShould be < 0.5\n");
  printf("-g <number>\n");
  printf("-ga <number>\tspecifies the absolute MIP gap for branch-and-bound.\n\t\tThis specifies the absolute allowed tolerance\n\t\ton the object function. Can result in faster solving times.\n");
  printf("-gr <number>\tspecifies the relative MIP gap for branch-and-bound.\n\t\tThis specifies the relative allowed tolerance\n\t\ton the object function. Can result in faster solving times.\n");
  printf("-f\t\tspecifies that branch-and-bound algorithm stops at first found\n");
  printf("\t\tsolution\n");
  printf("-o <value>\tspecifies that branch-and-bound algorithm stops when objective\n");
  printf("\t\tvalue is better than value\n");
  printf("-c\t\tduring branch-and-bound, take the ceiling branch first\n");
  printf("-ca\t\tduring branch-and-bound, the algorithm chooses branch\n");
  printf("-depth <limit>\tset branch-and-bound depth limit\n");
  printf("-n <solnr>\tspecify which solution number to return\n");
  printf("-B <rule>\tspecify branch-and-bound rule\n");
  printf("\t -B0: Select Lowest indexed non-integer column (default)\n");
  printf("\t -B1: Selection based on distance from the current bounds\n");
  printf("\t -B2: Selection based on the largest current bound\n");
  printf("\t -B3: Selection based on largest fractional value\n");
  printf("\t -B4: Simple, unweighted pseudo-cost of a variable\n");
  printf("\t -B5: This is an extended pseudo-costing strategy based on minimizing\n\t      the number of integer infeasibilities\n");
  printf("\t -B6: This is an extended pseudo-costing strategy based on maximizing\n\t      the normal pseudo-cost divided by the number of infeasibilities.\n\t      Similar to (the reciprocal of) a cost/benefit ratio\n");
  printf("These branch-and-bound rules can be combined with any of the following:\n");
  printf("-Bw\t\tWeightReverse branch-and-bound\n");
  printf("-Bb\t\tBranchReverse branch-and-bound\n");
  printf("-Bg\t\tGreedy branch-and-bound\n");
  printf("-Bp\t\tPseudoCost branch-and-bound\n");
  printf("-Bf\t\tDepthFirst branch-and-bound\n");
  printf("-Br\t\tRandomize branch-and-bound\n");
  printf("-BG\t\tGubMode branch-and-bound\n");
  printf("-Bd\t\tDynamic branch-and-bound\n");
  printf("-Bs\t\tRestartMode branch-and-bound\n");
  printf("\n");
  printf("-time\t\tPrint CPU time to parse input and to calculate result.\n");
  printf("-v <level>\tverbose mode, gives flow through the program.\n");
  printf("\t\t if level not provided (-v) then -v4 (NORMAL) is taken.\n");
  printf("\t -v0: NEUTRAL\n");
  printf("\t -v1: CRITICAL\n");
  printf("\t -v2: SEVERE\n");
  printf("\t -v3: IMPORTANT (default)\n");
  printf("\t -v4: NORMAL\n");
  printf("\t -v5: DETAILED\n");
  printf("\t -v6: FULL\n");
  printf("-t\t\ttrace pivot selection\n");
  printf("-d\t\tdebug mode, all intermediate results are printed,\n\t\tand the branch-and-bound decisions\n");
  printf("-R\t\treport information while solving the model\n");
  printf("-Db <filename>\tDo a generic readable data dump of key lp_solve model variables\n\t\tbefore solve.\n\t\tPrincipally for run difference and debugging purposes\n");
  printf("-Da <filename>\tDo a generic readable data dump of key lp_solve model variables\n\t\tafter solve.\n\t\tPrincipally for run difference and debugging purposes\n");
  printf("-i\t\tprint all intermediate valid solutions.\n\t\tCan give you useful solutions even if the total run time\n\t\tis too long\n");
  printf("-ia\t\tprint all intermediate (only non-zero values) valid solutions.\n\t\tCan give you useful solutions even if the total run time\n\t\tis too long\n");
  printf("-S <detail>\tPrint solution. If detail omitted, then -S2 is used.\n");
  printf("\t -S0: Print nothing\n");
  printf("\t -S1: Only objective value\n");
  printf("\t -S2: Obj value+variables (default)\n");
  printf("\t -S3: Obj value+variables+constraints\n");
  printf("\t -S4: Obj value+variables+constraints+duals\n");
  printf("\t -S5: Obj value+variables+constraints+duals+lp model\n");
  printf("\t -S6: Obj value+variables+constraints+duals+lp model+scales\n");
  printf("\t -S7: Obj value+variables+constraints+duals+lp model+scales+lp tableau\n");
}

void print_cpu_times(const char *info)
{
  static clock_t last_time = 0;
  clock_t new_time;

  new_time = clock();
  fprintf(stderr, "CPU Time for %s: %gs (%gs total since program start)\n",
	  info, (new_time - last_time) / (double) CLOCKS_PER_SEC,
	  new_time / (double) CLOCKS_PER_SEC);
  last_time = new_time;
}

#if 0
int myabortfunc(lprec *lp, void *aborthandle)
{
  /* printf("%f\n",lp->rhs[0]*(lp->maximise ? 1 : -1)); */
  return(0);
}
#endif

static MYBOOL isNum(char *val)
{
  int ord;
  char *pointer;

  ord = strtol(val, &pointer, 10);
  return(*pointer == 0);
}

static void DoReport(lprec *lp, char *str)
{
  fprintf(stderr, "%s %6.1fsec %8g\n", str, time_elapsed(lp), get_working_objective(lp));
}

static void __WINAPI LPMessageCB(lprec *lp, void *USERHANDLE, int msg)
{
  if(msg==MSG_LPFEASIBLE)
    DoReport(lp, "Feasible solution ");
  else if(msg==MSG_LPOPTIMAL)
    DoReport(lp, "Real solution ");
  else if(msg==MSG_MILPFEASIBLE)
    DoReport(lp, "First MILP    ");
  else if(msg==MSG_MILPBETTER)
    DoReport(lp, "Improved MILP ");
  else
      ;
}

int main(int argc, char *argv[])
{
  lprec *lp;
  char *filen, *wlp = NULL, *wmps = NULL, *wfmps = NULL, *wlpt = NULL;
  int i;
  int verbose = IMPORTANT /* CRITICAL */;
  MYBOOL debug = FALSE;
  MYBOOL report = FALSE;
  int print_sol = FALSE;
  int floor_first;
  int bb_depthlimit;
  int solutionlimit;
  MYBOOL break_at_first;
  int scaling = 0;
  double scaleloop = 0;
  MYBOOL tracing;
  short filetype = filetypeLP;
  int anti_degen;
  short print_timing = FALSE;
  short parse_only = FALSE;
  int do_presolve;
  short objective = 0;
  short PRINT_SOLUTION = 2;
  int improve;
  int pivoting;
  int bb_rule;
  int max_num_inv;
  int scalemode;
  int crashmode;
  short timeoutok = FALSE;
  long sectimeout;
  int result;
  MYBOOL preferdual = AUTOMATIC;
  int simplextype;
  REAL obj_bound;
  REAL mip_absgap;
  REAL mip_relgap;
  REAL epsperturb;
  REAL epsilon;
  REAL epspivot;
  REAL epsd;
  REAL epsb;
  REAL epsel;
  REAL break_at_value;
  FILE *fpin = stdin;
  char *bfp = NULL;
  char *rxliname = NULL, *rxli = NULL, *rxlidata = NULL, *rxlioptions = NULL, *wxliname = NULL, *wxli = NULL, *wxlioptions = NULL;
  char *debugdump_before = NULL;
  char *debugdump_after = NULL;
# define SCALINGTHRESHOLD 0.03

  /* read command line arguments */

# if defined FORTIFY
   Fortify_EnterScope();
# endif

  lp = lp_solve_make_lp(0, 0);
  floor_first = get_bb_floorfirst(lp);
  bb_depthlimit = get_bb_depthlimit(lp);
  solutionlimit = get_solutionlimit(lp);
  break_at_first = is_break_at_first(lp);
  tracing = is_trace(lp);
  anti_degen = get_anti_degen(lp);
  do_presolve = get_presolve(lp);
  improve = get_improve(lp);
  pivoting = get_pivoting(lp);
  bb_rule = get_bb_rule(lp);
  scalemode = get_scaling(lp);
  crashmode = get_basiscrash(lp);
  sectimeout = get_timeout(lp);
  obj_bound = get_obj_bound(lp);
  mip_absgap = get_mip_gap(lp, TRUE);
  mip_relgap = get_mip_gap(lp, FALSE);
  epsilon = get_epsilon(lp);
  epspivot = get_epspivot(lp);
  epsperturb = get_epsperturb(lp);
  epsd = get_epsd(lp);
  epsb = get_epsb(lp);
  epsel = get_epsel(lp);
  break_at_value = get_break_at_value(lp);
  max_num_inv = get_maxpivot(lp);
  simplextype = get_simplextype(lp);
  lp_solve_delete_lp(lp);

  for(i = 1; i < argc; i++) {
    if(strncmp(argv[i], "-v", 2) == 0) {
      if (argv[i][2])
        verbose = atoi(argv[i] + 2);
      else
        verbose = NORMAL;
    }
    else if(strcmp(argv[i], "-d") == 0)
      debug = TRUE;
    else if(strcmp(argv[i], "-R") == 0)
      report = TRUE;
    else if(strcmp(argv[i], "-i") == 0)
      print_sol = TRUE;
    else if(strcmp(argv[i], "-ia") == 0)
      print_sol = AUTOMATIC ;
    else if(strcmp(argv[i], "-c") == 0)
      floor_first = BRANCH_CEILING;
    else if(strcmp(argv[i], "-ca") == 0)
      floor_first = BRANCH_AUTOMATIC;
    else if((strcmp(argv[i], "-depth") == 0) && (i + 1 < argc))
      bb_depthlimit = atoi(argv[++i]);
    else if(strncmp(argv[i], "-B", 2) == 0) {
      bb_rule &= ~7;
      if (argv[i][2])
        bb_rule |= atoi(argv[i] + 2);
      else
        bb_rule |= NODE_FIRSTSELECT;
    }
    else if(strncmp(argv[i], "-Bw", 2) == 0)
      bb_rule |= NODE_WEIGHTREVERSEMODE;
    else if(strncmp(argv[i], "-Bb", 2) == 0)
      bb_rule |= NODE_BRANCHREVERSEMODE;
    else if(strncmp(argv[i], "-Bg", 2) == 0)
      bb_rule |= NODE_GREEDYMODE;
    else if(strncmp(argv[i], "-Bp", 2) == 0)
      bb_rule |= NODE_PSEUDOCOSTMODE;
    else if(strncmp(argv[i], "-Bf", 2) == 0)
      bb_rule |= NODE_DEPTHFIRSTMODE;
    else if(strncmp(argv[i], "-Br", 2) == 0)
      bb_rule |= NODE_RANDOMIZEMODE;
    else if(strncmp(argv[i], "-BG", 2) == 0)
      bb_rule |= NODE_GUBMODE;
    else if(strncmp(argv[i], "-Bd", 2) == 0)
      bb_rule |= NODE_DYNAMICMODE;
    else if(strncmp(argv[i], "-Bs", 2) == 0)
      bb_rule |= NODE_RESTARTMODE;
    else if((strcmp(argv[i], "-n") == 0) && (i + 1 < argc))
      solutionlimit = atoi(argv[++i]);
    else if((strcmp(argv[i], "-b") == 0) && (i + 1 < argc))
      obj_bound = atof(argv[++i]);
    else if(((strcmp(argv[i], "-g") == 0) || (strcmp(argv[i], "-ga") == 0)) && (i + 1 < argc))
      mip_absgap = atof(argv[++i]);
    else if((strcmp(argv[i], "-gr") == 0) && (i + 1 < argc))
      mip_relgap = atof(argv[++i]);
    else if((strcmp(argv[i], "-e") == 0) && (i + 1 < argc)) {
      epsilon = atof(argv[++i]);
      if((epsilon <= 0.0) || (epsilon >= 0.5)) {
	fprintf(stderr, "Invalid epsilon %g; 0 < epsilon < 0.5\n",
		(double)epsilon);
	EndOfPgr(EXIT_FAILURE);
      }
    }
    else if((strcmp(argv[i], "-r") == 0) && (i + 1 < argc))
      max_num_inv = atoi(argv[++i]);
    else if((strcmp(argv[i], "-o") == 0) && (i + 1 < argc))
      break_at_value = atof(argv[++i]);
    else if(strcmp(argv[i], "-f") == 0)
      break_at_first = TRUE;
    else if(strcmp(argv[i], "-timeoutok") == 0)
      timeoutok = TRUE;
    else if(strcmp(argv[i], "-h") == 0) {
      print_help(argv);
      EndOfPgr(EXIT_SUCCESS);
    }
    else if(strcmp(argv[i], "-prim") == 0)
      preferdual = FALSE;
    else if(strcmp(argv[i], "-dual") == 0)
      preferdual = TRUE;
    else if(strcmp(argv[i], "-simplexpp") == 0)
      simplextype = SIMPLEX_PRIMAL_PRIMAL;
    else if(strcmp(argv[i], "-simplexdp") == 0)
      simplextype = SIMPLEX_DUAL_PRIMAL;
    else if(strcmp(argv[i], "-simplexpd") == 0)
      simplextype = SIMPLEX_PRIMAL_DUAL;
    else if(strcmp(argv[i], "-simplexdd") == 0)
      simplextype = SIMPLEX_DUAL_DUAL;
    else if(strcmp(argv[i], "-sp") == 0)
      scalemode |= SCALE_POWER2;
    else if(strcmp(argv[i], "-si") == 0)
      scalemode |= SCALE_INTEGERS;
    else if(strcmp(argv[i], "-se") == 0)
      scalemode |= SCALE_EQUILIBRATE;
    else if(strncmp(argv[i], "-s", 2) == 0) {
      scaling = SCALE_MEAN;
      scalemode &= ~(SCALE_MEAN | SCALE_GEOMETRIC | SCALE_CURTISREID | SCALE_LOGARITHMIC | SCALE_EXTREME);
      if (argv[i][2]) {
        switch (atoi(argv[i] + 2)) {
        case 1:
	 scalemode |= SCALE_GEOMETRIC;
	 break;
	case 2:
	 scalemode |= SCALE_CURTISREID;
	 break;
	case 3:
	 scalemode |= SCALE_EXTREME;
	 break;
        case 4:
	 scalemode |= SCALE_MEAN;
	 break;
	case 5:
	 scalemode |= SCALE_LOGARITHMIC | SCALE_MEAN;
	 break;
        case 6:
	 scalemode |= SCALE_RANGE;
        case 7:
	 scalemode |= SCALE_QUADRATIC;
	 break;
        }
      }
      if((i + 1 < argc) && (isNum(argv[i + 1])))
	scaleloop = atoi(argv[++i]);
    }
    else if(strncmp(argv[i], "-C", 2) == 0)
      crashmode = atoi(argv[i] + 2);
    else if(strcmp(argv[i], "-t") == 0)
      tracing = TRUE;
    else if(strncmp(argv[i], "-S", 2) == 0) {
      if (argv[i][2])
        PRINT_SOLUTION = (short) atoi(argv[i] + 2);
      else
        PRINT_SOLUTION = 2;
    }
    else if(strncmp(argv[i], "-improve", 8) == 0) {
      if (argv[i][8])
        improve = atoi(argv[i] + 8);
      else
        improve = 0;
    }
    else if(strncmp(argv[i], "-piv", 4) == 0) {
      pivoting &= ~3;
      if (argv[i][4])
        pivoting |= atoi(argv[i] + 4);
      else
        pivoting |= PRICER_DEVEX | PRICE_ADAPTIVE;
    }
    else if(strncmp(argv[i], "-pivf", 5) == 0)
      pivoting |= PRICE_PRIMALFALLBACK;
    else if(strncmp(argv[i], "-pivm", 5) == 0)
      pivoting |= PRICE_MULTIPLE;
    else if(strncmp(argv[i], "-pivp", 5) == 0)
      pivoting |= PRICE_PARTIAL;
    else if(strncmp(argv[i], "-piva", 5) == 0)
      pivoting |= PRICE_ADAPTIVE;
    else if(strncmp(argv[i], "-pivr", 5) == 0)
      pivoting |= PRICE_RANDOMIZE;
#if defined EnablePartialOptimization
    else if(strncmp(argv[i], "-pivp", 5) == 0)
      pivoting |= PRICE_AUTOPARTIAL;
    else if(strncmp(argv[i], "-pivpc", 5) == 0)
      pivoting |= PRICE_AUTOPARTIALCOLS;
    else if(strncmp(argv[i], "-pivpr", 5) == 0)
      pivoting |= PRICE_AUTOPARTIALROWS;
#endif
    else if(strncmp(argv[i], "-pivll", 5) == 0)
      pivoting |= PRICE_LOOPLEFT;
    else if(strncmp(argv[i], "-pivla", 5) == 0)
      pivoting |= PRICE_LOOPALTERNATE;
#if defined PARSER_LP
    else if(strcmp(argv[i],"-lp") == 0)
      filetype = filetypeLP;
#endif
    else if((strcmp(argv[i],"-wlp") == 0) && (i + 1 < argc))
      wlp = argv[++i];
    else if(strcmp(argv[i],"-mps") == 0)
      filetype = filetypeMPS;
    else if(strcmp(argv[i],"-fmps") == 0)
      filetype = filetypeFREEMPS;
    else if((strcmp(argv[i],"-wmps") == 0) && (i + 1 < argc))
      wmps = argv[++i];
    else if((strcmp(argv[i],"-wfmps") == 0) && (i + 1 < argc))
      wfmps = argv[++i];
#if defined PARSER_CPLEX
    else if(strcmp(argv[i],"-lpt") == 0)
      filetype = filetypeCPLEX;
    else if((strcmp(argv[i],"-wlpt") == 0) && (i + 1 < argc))
      wlpt = argv[++i];
#endif
    else if(strcmp(argv[i],"-degen") == 0)
      anti_degen = ANTIDEGEN_DEFAULT;
    else if(strcmp(argv[i],"-degenf") == 0)
      anti_degen |= ANTIDEGEN_FIXEDVARS;
    else if(strcmp(argv[i],"-degenc") == 0)
      anti_degen |= ANTIDEGEN_COLUMNCHECK;
    else if(strcmp(argv[i],"-degens") == 0)
      anti_degen |= ANTIDEGEN_STALLING;
    else if(strcmp(argv[i],"-degenn") == 0)
      anti_degen |= ANTIDEGEN_NUMFAILURE;
    else if(strcmp(argv[i],"-degenl") == 0)
      anti_degen |= ANTIDEGEN_LOSTFEAS;
    else if(strcmp(argv[i],"-degeni") == 0)
      anti_degen |= ANTIDEGEN_INFEASIBLE;
    else if(strcmp(argv[i],"-degend") == 0)
      anti_degen |= ANTIDEGEN_DYNAMIC;
    else if(strcmp(argv[i],"-degend") == 0)
      anti_degen |= ANTIDEGEN_DURINGBB;
    else if(strcmp(argv[i],"-degenb") == 0)
      anti_degen |= ANTIDEGEN_COLUMNCHECK;
    else if(strcmp(argv[i],"-time") == 0) {
      if(clock() == -1)
	fprintf(stderr, "CPU times not available on this machine\n");
      else
	print_timing = TRUE;
    }
    else if((strcmp(argv[i],"-bfp") == 0) && (i + 1 < argc))
      bfp = argv[++i];
    else if((strcmp(argv[i],"-rxli") == 0) && (i + 2 < argc)) {
      rxliname = argv[++i];
      rxli = argv[++i];
      fpin = NULL;
      filetype = filetypeXLI;
    }
    else if((strcmp(argv[i],"-rxlidata") == 0) && (i + 1 < argc))
      rxlidata = argv[++i];
    else if((strcmp(argv[i],"-rxliopt") == 0) && (i + 1 < argc))
      rxlioptions = argv[++i];
    else if((strcmp(argv[i],"-wxli") == 0) && (i + 2 < argc)) {
      wxliname = argv[++i];
      wxli = argv[++i];
    }
    else if((strcmp(argv[i],"-wxliopt") == 0) && (i + 1 < argc))
      wxlioptions = argv[++i];
    else if((strcmp(argv[i],"-Db") == 0) && (i + 1 < argc))
      debugdump_before = argv[++i];
    else if((strcmp(argv[i],"-Da") == 0) && (i + 1 < argc))
      debugdump_after = argv[++i];
    else if((strcmp(argv[i],"-timeout") == 0) && (i + 1 < argc))
      sectimeout = atol(argv[++i]);
    else if((strcmp(argv[i],"-trej") == 0) && (i + 1 < argc))
      epspivot = atof(argv[++i]);
    else if((strcmp(argv[i],"-epsp") == 0) && (i + 1 < argc))
      epsperturb = atof(argv[++i]);
    else if((strcmp(argv[i],"-epsd") == 0) && (i + 1 < argc))
      epsd = atof(argv[++i]);
    else if((strcmp(argv[i],"-epsb") == 0) && (i + 1 < argc))
      epsb = atof(argv[++i]);
    else if((strcmp(argv[i],"-epsel") == 0) && (i + 1 < argc))
      epsel = atof(argv[++i]);
    else if(strcmp(argv[i],"-parse_only") == 0)
      parse_only = TRUE;
    else if(strcmp(argv[i],"-presolve") == 0)
      do_presolve |= PRESOLVE_ROWS | PRESOLVE_COLS;
    else if(strcmp(argv[i],"-presolvel") == 0)
      do_presolve |= PRESOLVE_LINDEP;
    else if(strcmp(argv[i],"-presolves") == 0)
      do_presolve |= PRESOLVE_SOS;
    else if(strcmp(argv[i],"-presolver") == 0)
      do_presolve |= PRESOLVE_REDUCEMIP;
    else if(strcmp(argv[i],"-min") == 0)
      objective = -1;
    else if(strcmp(argv[i],"-max") == 0)
      objective =  1;
    else if(fpin == stdin) {
      filen = argv[i];
      if(*filen == '<')
        filen++;
      if((fpin = fopen(filen, "r")) == NULL) {
	print_help(argv);
	fprintf(stderr,"\nError, Unable to open input file '%s'\n",
		argv[i]);
	EndOfPgr(EXIT_FAILURE);
      }
    }
    else {
      filen = argv[i];
      if(*filen != '>') {
        print_help(argv);
        fprintf(stderr, "\nError, Unrecognized command line argument '%s'\n",
		argv[i]);
        EndOfPgr(EXIT_FAILURE);
      }
    }
  }

  signal(SIGABRT,/* (void (*) OF((int))) */ SIGABRT_func);

  switch(filetype) {
#if defined PARSER_LP
  case filetypeLP:
    lp = read_lp(fpin, verbose, "lp" );
    break;
#endif
  case filetypeMPS:
    lp = read_mps(fpin, verbose);
    break;
  case filetypeFREEMPS:
    lp = read_freemps(fpin, verbose);
    break;
#if defined PARSER_CPLEX
  case filetypeCPLEX:
    lp = read_lpt(fpin, verbose, "lpt" );
    break;
#endif
  case filetypeXLI:
    lp = read_XLI(rxliname, rxli, rxlidata, rxlioptions, verbose);
    break;
  }

  if((fpin != NULL) && (fpin != stdin))
    fclose(fpin);

  if(print_timing)
    print_cpu_times("Parsing input");

  if(lp == NULL) {
    fprintf(stderr, "Unable to read model.\n");
    EndOfPgr(EXIT_FAILURE);
  }

  if(objective != 0) {
    if(objective == 1)
      lp_solve_set_maxim(lp);
    else
      lp_solve_set_minim(lp);
  }

  if(wlp != NULL)
    write_lp(lp, wlp);

  if(wmps != NULL)
    write_mps(lp, wmps);

  if(wfmps != NULL)
    write_freemps(lp, wfmps);

  if(wxli != NULL) {
    if(!set_XLI(lp, wxliname)) {
      fprintf(stderr, "Unable to set XLI library (%s).\n", wxliname);
      EndOfPgr(EXIT_FAILURE);
    }
    write_XLI(lp, wxli, wxlioptions, FALSE);
    set_XLI(lp, NULL);
  }

#if defined PARSER_CPLEX
  if(wlpt != NULL)
    write_lpt(lp, wlpt);
#endif

  if(parse_only) {
    lp_solve_delete_lp(lp);
    EndOfPgr(0);
  }

  if(PRINT_SOLUTION >= 5)
    lp_solve_print_lp(lp);

#if 0
  put_abortfunc(lp,(abortfunc *) myabortfunc, NULL);
#endif

  if(sectimeout>0)
    lp_solve_set_timeout(lp, sectimeout);
  set_print_sol(lp, print_sol);
  set_epsilon(lp, epsilon);
  set_epspivot(lp, epspivot);
  set_epsperturb(lp, epsperturb);
  set_epsd(lp, epsd);
  set_epsb(lp, epsb);
  set_epsel(lp, epsel);
  set_debug(lp, debug);
  set_bb_floorfirst(lp, floor_first);
  set_bb_depthlimit(lp, bb_depthlimit);
  set_solutionlimit(lp, solutionlimit);
  set_trace(lp, tracing);
  set_obj_bound(lp, obj_bound);
  set_break_at_value(lp, break_at_value);
  set_break_at_first(lp, break_at_first);
  set_mip_gap(lp, TRUE, mip_absgap);
  set_mip_gap(lp, FALSE, mip_relgap);
  set_anti_degen(lp, anti_degen);
  set_presolve(lp, do_presolve | ((PRINT_SOLUTION >= 4) ? PRESOLVE_SENSDUALS : 0));
  set_improve(lp, improve);
  set_maxpivot(lp, max_num_inv);
  if(preferdual != AUTOMATIC)
    set_preferdual(lp, preferdual);
  set_pivoting(lp, pivoting);
  set_scaling(lp, scalemode);
  set_basiscrash(lp, crashmode);
  set_bb_rule(lp, bb_rule);
  set_simplextype(lp, simplextype);
  if(bfp != NULL)
    if(!set_BFP(lp, bfp)) {
      fprintf(stderr, "Unable to set BFP package.\n");
      EndOfPgr(EXIT_FAILURE);
    }
  if(debugdump_before != NULL)
    print_debugdump(lp, debugdump_before);
  if(report)
    put_msgfunc(lp, LPMessageCB, NULL, MSG_LPFEASIBLE | MSG_LPOPTIMAL | MSG_MILPFEASIBLE | MSG_MILPBETTER | MSG_PERFORMANCE);

  if(scaling) {
    if(scaleloop <= 0)
      scaleloop = 5;
    if(scaleloop - (int) scaleloop < SCALINGTHRESHOLD)
      scaleloop = (int) scaleloop + SCALINGTHRESHOLD;
    set_scalelimit(lp, scaleloop);
  }

  result = lp_solve_solve(lp);

  if(PRINT_SOLUTION >= 6)
    print_scales(lp);

  if(print_timing)
    print_cpu_times("solving");

  if(debugdump_after != NULL)
    print_debugdump(lp, debugdump_after);

  if((timeoutok) && (result == TIMEOUT) && (get_solutioncount(lp) > 0))
    result = OPTIMAL;

  switch(result) {
  case OPTIMAL:
  case PROCBREAK:
  case FEASFOUND:
    if (PRINT_SOLUTION >= 1)
      print_objective(lp);

    if (PRINT_SOLUTION >= 2)
      print_solution(lp, 1);

    if (PRINT_SOLUTION >= 3)
      print_constraints(lp, 1);

    if (PRINT_SOLUTION >= 4)
      print_duals(lp);

    if(tracing)
      fprintf(stderr,
      "Branch & Bound depth: %d\nNodes processed: %d\nSimplex pivots: %d\nNumber of equal solutions: %d\n",
	      get_max_level(lp), get_total_nodes(lp), get_total_iter(lp), get_solutioncount(lp));
    break;
  case NOMEMORY:
    if (PRINT_SOLUTION >= 1)
      printf("Out of memory\n");
    break;
  case INFEASIBLE:
    if (PRINT_SOLUTION >= 1)
      printf("This problem is infeasible\n");
    break;
  case UNBOUNDED:
    if (PRINT_SOLUTION >= 1)
      printf("This problem is unbounded\n");
    break;
  case PROCFAIL:
   if (PRINT_SOLUTION >= 1)
      printf("The B&B routine failed\n");
    break;
  case TIMEOUT:
    if (PRINT_SOLUTION >= 1)
      printf("Timeout\n");
    break;
  case USERABORT:
    if (PRINT_SOLUTION >= 1)
      printf("User aborted\n");
    break;
  default:
    if (PRINT_SOLUTION >= 1)
      printf("lp_solve failed\n");
    break;
  }

  if (PRINT_SOLUTION >= 7)
    print_tableau(lp);

  lp_solve_delete_lp(lp);

  EndOfPgr(result);
}
