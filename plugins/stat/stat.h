#ifndef GNUMERIC_STAT_H
#define GNUMERIC_STAT_H

#include <glib.h>
#include <gnome.h>
#include "../../src/gnumeric.h"
#include "../../src/func.h"
#include "../../src/plugin.h"

static Value *stat_stdev (void *sheet, GList *expr_node_list, int eval_col,
				int eval_row, char **error_string);

static Value *stat_variance(void *sheet, GList *expr_node_list, int eval_col,
				int eval_row, char **error_string);

int callback_var(Sheet *sheet, Value *value, 
		char **error_string, void *closure);
static void cleanup_plugin(PluginData *pd);
int init_plugin(PluginData *pd);

#endif /* GNUMERIC_STAT_H */
