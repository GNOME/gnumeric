/* prototypes for debug printing by other files */

void lp_solve_debug_print (lprec *lp, const char *format, ...);
void lp_solve_debug_print_solution (lprec *lp);
void lp_solve_debug_print_bounds (lprec *lp, gnm_float *upbo,
				  gnm_float *lowbo);
