/* prototypes for debug printing by other files */

void lp_solve_debug_print (lprec *lp, const char *format, ...);
void lp_solve_debug_print_solution (lprec *lp);
void lp_solve_debug_print_bounds (lprec *lp, gnum_float *upbo,
				  gnum_float *lowbo);
