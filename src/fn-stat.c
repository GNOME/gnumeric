/*
 * fn-stat.c:  Built in statistical functions and functions registration
 *
 * Author:
 *  Michael Meeks <michael@imaginator.com>
 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "numbers.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

#if 0
/* help template */
static char *help_ = {
	N_("@FUNCTION=NAME\n"
	   "@SYNTAX=(b1, b2, ...)\n"

	   "@DESCRIPTION"
	   ""
	   "\n"

	   ""
	   ""
	   "\n"
	   
	   ""
	   ""
	   ""
	   ""
	   "@SEEALSO=")
};

#endif

typedef struct {
	guint32 num ;
	float_t sum_x ;
	float_t sum_x_squared ;
} stat_closure_t;

static void
setup_stat_closure (stat_closure_t *cl)
{
	cl->num = 0 ;
	cl->sum_x = 0.0 ;
	cl->sum_x_squared = 0.0 ;
}

static int
callback_function_stat (Sheet *sheet, Value *value, char **error_string, void *closure)
{
	stat_closure_t *mm = closure;
	
	switch (value->type){
	case VALUE_INTEGER:
		mm->num++ ;
		mm->sum_x+=value->v.v_int ;
		mm->sum_x_squared+= ((float_t)value->v.v_int)*((float_t)value->v.v_int) ;
		break;

	case VALUE_FLOAT:
		mm->num++ ;
		mm->sum_x+=value->v.v_float ;
		mm->sum_x_squared+= ((float_t)value->v.v_float)*((float_t)value->v.v_float) ;
		break ;
	default:
		/* ignore strings */
		break;
	}
	
	return TRUE;
}

static char *help_varp = {
	N_("@FUNCTION=VARP\n"
	   "@SYNTAX=VARP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VARP calculates the variance of a set of numbers "
	   "where each number is a member of a population "
	   "and the set is the entire population"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=STDEV,VAR,MEAN")
};

static Value *
gnumeric_varp (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_closure_t cl;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	setup_stat_closure (&cl) ;

	function_iterate_argument_values (sheet, callback_function_stat,
					  &cl, expr_node_list,
					  eval_col, eval_row, error_string);

	num = (float_t)cl.num ;
	ans = (num*cl.sum_x_squared - cl.sum_x*cl.sum_x)
		/ (num * num) ;

	return 	value_float(ans) ;
}

static char *help_var = {
	N_("@FUNCTION=VAR\n"
	   "@SYNTAX=VAR(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "VAR estimates the variance of a sample of a population "
	   "To get the true variance of a complete population use @VARP"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=VARP,STDEV")
};

static Value *
gnumeric_var (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	stat_closure_t cl;
	Sheet *sheet = (Sheet *) tsheet;
	float_t ans, num ;

	setup_stat_closure (&cl) ;

	function_iterate_argument_values (sheet, callback_function_stat,
					  &cl, expr_node_list,
					  eval_col, eval_row, error_string);


	num = (float_t)cl.num ;
	ans = (num*cl.sum_x_squared - cl.sum_x*cl.sum_x)
		/ (num * (num - 1.0)) ;

	return value_float(ans) ;
}

static char *help_stdev = {
	N_("@FUNCTION=STDEV\n"
	   "@SYNTAX=STDEV(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEV returns standard deviation of a set of numbers "
	   "treating these numbers as members of a population"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=VAR,MEAN")
};

static Value *
gnumeric_stdev (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *ans = gnumeric_var (tsheet, expr_node_list, eval_col, eval_row, error_string) ;
	if(ans && ans->type==VALUE_FLOAT)
		ans->v.v_float = sqrt(ans->v.v_float) ;
	return ans ;
}

static char *help_stdevp = {
	N_("@FUNCTION=STDEVP\n"
	   "@SYNTAX=STDEVP(b1, b2, ...)\n"

	   "@DESCRIPTION="
	   "STDEV returns standard deviation of a set of numbers "
	   "treating these numbers as members of a complete population"
	   "\n"
	   "Performing this function on a string or empty cell simply does nothing."
	   "\n"
	   "@SEEALSO=STDEV,VAR,MEAN")
};

static Value *
gnumeric_stdevp (void *tsheet, GList *expr_node_list, int eval_col, int eval_row, char **error_string)
{
	Value *ans = gnumeric_varp (tsheet, expr_node_list, eval_col, eval_row, error_string) ;
	if(ans && ans->type==VALUE_FLOAT)
		ans->v.v_float = sqrt(ans->v.v_float) ;
	return ans ;
}

FunctionDefinition stat_functions [] = {
	{ "stdev",     0,      "",          &help_stdev,     gnumeric_stdev, NULL },
	{ "stdevp",    0,      "",          &help_stdevp,    gnumeric_stdevp, NULL },
	{ "var",       0,      "",          &help_var,       gnumeric_var, NULL },
	{ "varp",      0,      "",          &help_varp,      gnumeric_varp, NULL },
	{ NULL, NULL },
};


/*
  Mode, Median: Use large hash table :-)
 */
