/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * command-context-stderr.c : Error dispatch for line oriented clients
 *
 * Author:
 * 	Jon K Hellan <hellan@acm.org>
 *
 * (C) 2002 Jon K Hellan
 */
#include <gnumeric-config.h>
#include <gnumeric-i18n.h>
#include <stdio.h>
#include "gnumeric.h"
#include "command-context-stderr.h"
#include "command-context-priv.h"
#include <gsf/gsf-impl-utils.h>
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
ccs_error_error (CommandContext *ctxt, GError *error)
{
	CommandContextStderr *ccs = COMMAND_CONTEXT_STDERR (ctxt);

	fprintf (stderr, "Error: %s\n", error->message);
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

static char *
ccs_get_password (G_GNUC_UNUSED CommandContext *cc,
		  G_GNUC_UNUSED char const* msg)
{
	return NULL;
}
static void
ccs_set_sensitive (G_GNUC_UNUSED CommandContext *cc,
		   G_GNUC_UNUSED gboolean sensitive)
{
}

static void
ccs_class_init (GObjectClass *object_class)
{
	CommandContextClass *cc_class = COMMAND_CONTEXT_CLASS (object_class);

	g_return_if_fail (cc_class != NULL);
	cc_class->get_password	   = ccs_get_password;
	cc_class->set_sensitive	   = ccs_set_sensitive;
	cc_class->error.error      = ccs_error_error;
	cc_class->error.error_info = ccs_error_info;
}

GSF_CLASS (CommandContextStderr, command_context_stderr,
	   ccs_class_init, ccs_init, COMMAND_CONTEXT_TYPE);
