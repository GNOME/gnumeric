#include <config.h>
#include "command-context.h"
#include "command-context-impl.h"

/*
 * command_context_vtbl_init : A utility routine to be used to init
 *    a vtbl to NULL.  As new functions are added this will ensure that
 *    if a class does not define a new hander we can catch it rather
 *    than calling uninitialized memory.
 */
void
command_context_vtbl_init (GnmCmdcontext_vtbl *vtbl)
{
	g_return_if_fail (vtbl);

	vtbl->plugin_problem = NULL;
	vtbl->splits_array = NULL;
}

void
gnumeric_error_plugin_problem (CmdContext *context,
			       char const * const message)
{
	g_return_if_fail (context);
	g_return_if_fail (context->vtbl);
	g_return_if_fail (context->vtbl->plugin_problem);

	(*context->vtbl->plugin_problem)(context, message);
}

void
gnumeric_error_splits_array (CmdContext *context)
{
	g_return_if_fail (context);
	g_return_if_fail (context->vtbl);
	g_return_if_fail (context->vtbl->splits_array);

	(*context->vtbl->splits_array)(context);
}
