/*
 * fn-misc.c:  Miscelaneous built-in functions
 *
 * Author:
 *  Miguel de Icaza (miguel@gnu.org)
 *
 */
#include <config.h>
#include "gnumeric.h"
#include "utils.h"
#include "func.h"


/***************************************************************************/

void
misc_functions_init (void)
{
	FunctionCategory *cat = function_get_category (_("Miscellaneous"));
}
