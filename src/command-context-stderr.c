/*
 * command-context-stderr.c : Error dispatch for line oriented client
 *
 * Author:
 *	Jon K Hellan <hellan@acm.org>
 *
 * (C) 2002-2005 Jon K Hellan
 */
#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>
#include <command-context-stderr.h>
#include <gsf/gsf-impl-utils.h>
#include <goffice/goffice.h>
#include <ranges.h>

struct GnmCmdContextStderr_ {
	GObject	 base;
	int	 status;
};
typedef GObjectClass GnmCmdContextStderrClass;

/**
 * gnm_cmd_context_stderr_new:
 *
 * Returns: (transfer full): the newly allocated #GOCmdContext.
 **/
GOCmdContext *
gnm_cmd_context_stderr_new (void)
{
	return g_object_new (GNM_CMD_CONTEXT_STDERR_TYPE, NULL);
}

void
gnm_cmd_context_stderr_set_status (GnmCmdContextStderr *ccs, int status)
{
	g_return_if_fail (ccs != NULL);
	g_return_if_fail (GNM_IS_CMD_CONTEXT_STDERR (ccs));

	ccs->status = status;
}

int
gnm_cmd_context_stderr_get_status (GnmCmdContextStderr *ccs)
{
	g_return_val_if_fail (ccs != NULL, -1);
	g_return_val_if_fail (GNM_IS_CMD_CONTEXT_STDERR (ccs), -1);

	return ccs->status;
}

static void
ccs_error_error (GOCmdContext *cc, GError *error)
{
	GnmCmdContextStderr *ccs = GNM_CMD_CONTEXT_STDERR (cc);

	g_printerr ("Error: %s\n", error->message);
	ccs->status = -1;
}
static void
ccs_error_info (GOCmdContext *cc, GOErrorInfo *error)
{
	GnmCmdContextStderr *ccs = GNM_CMD_CONTEXT_STDERR (cc);

	go_error_info_print (error);
	ccs->status = -1;
}

static char *
ccs_get_password (G_GNUC_UNUSED GOCmdContext *cc,
		  G_GNUC_UNUSED char const* filename)
{
	return NULL;
}
static void
ccs_set_sensitive (G_GNUC_UNUSED GOCmdContext *cc,
		   G_GNUC_UNUSED gboolean sensitive)
{
}

static void
ccs_progress_set (GOCmdContext *cc, double val)
{
}

static void
ccs_progress_message_set (GOCmdContext *cc, gchar const *msg)
{
}

static void
ccs_init (GnmCmdContextStderr *ccs)
{
	ccs->status = 0;
}

static void
ccs_gnm_cmd_context_init (GOCmdContextClass *cc_class)
{
	cc_class->get_password		= ccs_get_password;
	cc_class->set_sensitive		= ccs_set_sensitive;
	cc_class->progress_set		= ccs_progress_set;
	cc_class->progress_message_set	= ccs_progress_message_set;
	cc_class->error.error		= ccs_error_error;
	cc_class->error.error_info	= ccs_error_info;
}

GSF_CLASS_FULL (GnmCmdContextStderr, gnm_cmd_context_stderr,
		NULL, NULL, NULL, NULL,
		ccs_init, G_TYPE_OBJECT, 0,
		GSF_INTERFACE (ccs_gnm_cmd_context_init, GO_TYPE_CMD_CONTEXT))
