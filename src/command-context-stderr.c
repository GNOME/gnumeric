/*
 * command-context-stderr.c : Error dispatch for line oriented clients
 *
 * Author:
 * 	Jon K Hellan <hellan@acm.org>
 *
 * (C) 2002 Jon K Hellan
 */
#include <stdio.h>
#include <gnumeric-config.h>
#include "gnumeric.h"
#include "command-context-stderr.h"
#include "command-context-priv.h"
#include <gsf/gsf-impl-utils.h>
#include <libgnome/gnome-i18n.h>
#include "error-info.h"
#include "ranges.h"

struct _CommandContextStderr {
	CommandContext context;
	int status;
};

typedef struct {
	CommandContextClass   context_class;
} CommandContextStderrClass;

#define COMMAND_CONTEXT_STDERR_CLASS(k) \
 (G_TYPE_CHECK_CLASS_CAST ((k), COMMAND_CONTEXT_STDERR_TYPE, \
 CommandContextStderrClass))

CommandContextStderr *
command_context_stderr_new (void)
{
	return g_object_new (command_context_stderr_get_type (), NULL);
}

void
command_context_stderr_set_status (CommandContextStderr *ccs, int status)
{
	g_return_if_fail (ccs != NULL);
	g_return_if_fail (IS_COMMAND_CONTEXT_STDERR (ccs));

	ccs->status = status;
}

int
command_context_stderr_get_status (CommandContextStderr *ccs)
{
	g_return_val_if_fail (ccs != NULL, -1);
	g_return_val_if_fail (IS_COMMAND_CONTEXT_STDERR (ccs), -1);

	return ccs->status;
}

static void
ccs_system (CommandContext *ctxt, char const *msg)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s\n", msg);
	ccs->status = -1;
}

static void
ccs_plugin (CommandContext *ctxt, char const *msg)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s\n", msg);
	ccs->status = -1;
}

static void
ccs_read (CommandContext *ctxt, char const *msg)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s\n", msg);
	ccs->status = -1;
}

static void
ccs_save (CommandContext *ctxt, char const *msg)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s\n", msg);
	ccs->status = -1;
}

static void
ccs_splits_array (CommandContext *ctxt, char const *cmd, Range const *array)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);
	char *msg;

	if (array != NULL)
		msg = g_strdup_printf (_("Would split array %s"),
					   range_name (array));
	else
		msg = g_strdup (_("Would split an array"));
	fprintf (stderr, "Error: %s\n", msg);
	g_free (msg);
	ccs->status = -1;
}

static void
ccs_invalid (CommandContext *ctxt, char const *msg, char const *val)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s : %s\n", msg, val);
	ccs->status = -1;
}

static void
ccs_error_info (CommandContext *ctxt, ErrorInfo *error)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	error_info_print (error);
	ccs->status = -1;
}

static void
ccs_init (CommandContextStderr *ccs)
{
	ccs->status = 0;
}

static void
ccs_class_init (GObjectClass *object_class)
{
	CommandContextClass *cc_class = COMMAND_CONTEXT_CLASS (object_class);

	g_return_if_fail (cc_class != NULL);
	cc_class->error.system       = ccs_system;
	cc_class->error.plugin       = ccs_plugin;
	cc_class->error.read         = ccs_read;
	cc_class->error.save         = ccs_save;
	cc_class->error.splits_array = ccs_splits_array;
	cc_class->error.invalid      = ccs_invalid;
	cc_class->error.error_info   = ccs_error_info;
}

GSF_CLASS (CommandContextStderr, command_context_stderr,
	   ccs_class_init, ccs_init, COMMAND_CONTEXT_TYPE);
