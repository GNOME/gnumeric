#include "lpkit.h"
#include "lpglob.h"
#include <stdarg.h>


static void
print_indent (void)
{
        int i;

	fprintf (stderr, "%2d", Level);
	if (Level < 50) /* useless otherwise */
	        for (i = Level; i > 0; i--)
		        fprintf (stderr, "--");
	else
	        fprintf (stderr, " *** too deep ***");
	fprintf (stderr, "> ");
}


void
lp_solve_debug_print_solution (lprec *lp)
{
        int i;

	if (lp->debug)
	        for (i = lp->rows + 1; i <= lp->sum; i++) {
		        print_indent ();
			if (lp->names_used)
			        fprintf (stderr, "%s %g\n",
					 lp->col_name[i - lp->rows],
					 (double)lp->solution[i]);
			else 
			        fprintf (stderr, "Var [%d] %g\n", i - lp->rows,
					 (double)lp->solution[i]);
		}
}


void
lp_solve_debug_print_bounds (lprec *lp, gnum_float *upbo, gnum_float *lowbo)
{
        int i;

        if (lp->debug)
	        for (i = lp->rows + 1; i <= lp->sum; i++) {
		        if (lowbo[i] == upbo[i]) {
			        print_indent ();
				if (lp->names_used)
				        fprintf (stderr, "%s = %g\n",
						 lp->col_name[i - lp->rows],
						 (double) lowbo[i]);
				else
				        fprintf (stderr, "Var [%d]  = %g\n",
						 i - lp->rows,
						 (double) lowbo[i]);
			} else {
			        if (lowbo[i] != 0) {
				        print_indent ();
					if (lp->names_used)
					        fprintf (stderr, "%s > %g\n",
							 lp->col_name[i - lp->rows],
							 (double)lowbo[i]);
					else
					        fprintf (stderr,
							 "Var [%d]  > %g\n",
							 i - lp->rows,
							 (double) lowbo[i]);
				}
				if (upbo[i] != lp->infinite) {
				        print_indent ();
					if (lp->names_used)
					        fprintf (stderr, "%s < %g\n",
							 lp->col_name[i - lp->rows],
							 (double) upbo[i]);
					else
					        fprintf (stderr,
							 "Var [%d]  < %g\n",
							 i - lp->rows,
							 (double) upbo[i]);
				}
			}
		}
}


void
lp_solve_debug_print (lprec *lp, const char *format, ...)
{
        va_list ap;

	if (lp->debug) {
	        va_start (ap, format);
		print_indent ();
		vfprintf (stderr, format, ap);
		fputc ('\n', stderr);
		va_end (ap);
	}
}
