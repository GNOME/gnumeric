#include <gnumeric-config.h>
#include "gnumeric.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "workbook.h"
#include "ranges.h"
#include "gutils.h"
#include "gnm-solver.h"
#include "workbook-view.h"
#include "workbook-control.h"
#include "gnm-marshalers.h"
#include <gsf/gsf-impl-utils.h>
#include <gsf/gsf-output-stdio.h>
#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

/* ------------------------------------------------------------------------- */

GType
gnm_solver_status_get_type (void)
{
	static GType etype = 0;
	if (etype == 0) {
		static GEnumValue const values[] = {
			{ GNM_SOLVER_STATUS_READY,
			  (char *)"GNM_SOLVER_STATUS_READY",
			  (char *)"ready"
			},
			{ GNM_SOLVER_STATUS_PREPARING,
			  (char *)"GNM_SOLVER_STATUS_PREPARING",
			  (char *)"preparing"
			},
			{ GNM_SOLVER_STATUS_PREPARED,
			  (char *)"GNM_SOLVER_STATUS_PREPARED",
			  (char *)"prepared"
			},
			{ GNM_SOLVER_STATUS_RUNNING,
			  (char *)"GNM_SOLVER_STATUS_RUNNING",
			  (char *)"running"
			},
			{ GNM_SOLVER_STATUS_DONE,
			  (char *)"GNM_SOLVER_STATUS_DONE",
			  (char *)"done"
			},
			{ GNM_SOLVER_STATUS_ERROR,
			  (char *)"GNM_SOLVER_STATUS_ERROR",
			  (char *)"error"
			},
			{ GNM_SOLVER_STATUS_CANCELLED,
			  (char *)"GNM_SOLVER_STATUS_CANCELLED",
			  (char *)"cancelled"
			},
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("GnmStatus", values);
	}
	return etype;
}

/* ------------------------------------------------------------------------- */

enum {
	SOL_SIG_PREPARE,
	SOL_SIG_START,
	SOL_SIG_STOP,
	SOL_SIG_LAST
};

static guint solver_signals[SOL_SIG_LAST] = { 0 };

enum {
	SOL_PROP_0,
	SOL_PROP_STATUS,
	SOL_PROP_PARAMS,
	SOL_PROP_RESULT
};

static GObjectClass *gnm_solver_parent_class;

static void
gnm_solver_dispose (GObject *obj)
{
	GnmSolver *sol = GNM_SOLVER (obj);

	if (sol->status == GNM_SOLVER_STATUS_RUNNING) {
		gboolean ok = gnm_solver_stop (sol, NULL);
		if (ok) {
			g_warning ("Failed to stop solver -- now what?");
		}
	}

	if (sol->result) {
		g_object_unref (sol->result);
		sol->result = NULL;
	}

	gnm_solver_parent_class->dispose (obj);
}

static void
gnm_solver_get_property (GObject *object, guint property_id,
			 GValue *value, GParamSpec *pspec)
{
	GnmSolver *sol = (GnmSolver *)object;

	switch (property_id) {
	case SOL_PROP_STATUS:
		g_value_set_enum (value, sol->status);
		break;

	case SOL_PROP_PARAMS:
		g_value_set_pointer (value, sol->params);
		break;

	case SOL_PROP_RESULT:
		g_value_set_object (value, sol->result);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
gnm_solver_set_property (GObject *object, guint property_id,
			 GValue const *value, GParamSpec *pspec)
{
	GnmSolver *sol = (GnmSolver *)object;

	switch (property_id) {
	case SOL_PROP_STATUS:
		gnm_solver_set_status (sol, g_value_get_enum (value));
		break;

	case SOL_PROP_PARAMS:
		sol->params = g_value_peek_pointer (value);
		break;

	case SOL_PROP_RESULT:
		if (sol->result) g_object_unref (sol->result);
		sol->result = g_value_dup_object (value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

gboolean
gnm_solver_prepare (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY, FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_PREPARE], 0, wbc, err, &res);
	return res;
}

gboolean
gnm_solver_start (GnmSolver *sol, WorkbookControl *wbc, GError **err)
{
	gboolean res;

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_READY ||
			      sol->status == GNM_SOLVER_STATUS_PREPARED,
			      FALSE);

	if (sol->status == GNM_SOLVER_STATUS_READY) {
		res = gnm_solver_prepare (sol, wbc, err);
		if (!res)
			return FALSE;
	}

	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_START], 0, wbc, err, &res);
	return res;
}

gboolean
gnm_solver_stop (GnmSolver *sol, GError **err)
{
	gboolean res;

	g_return_val_if_fail (GNM_IS_SOLVER (sol), FALSE);

	g_signal_emit (sol, solver_signals[SOL_SIG_STOP], 0, err, &res);
	return res;
}

void
gnm_solver_store_result (GnmSolver *sol)
{
	GnmValue const *vinput;
	GnmSheetRange sr;
	int h, w, x, y;
	GnmSolverResult const *result;
	GnmValue const *solution;

	g_return_if_fail (GNM_IS_SOLVER (sol));
	g_return_if_fail (sol->result != NULL);
	g_return_if_fail (sol->result->solution);

	vinput = gnm_solver_param_get_input (sol->params);
	gnm_sheet_range_from_value (&sr, vinput);
	if (!sr.sheet) sr.sheet = sol->params->sheet;
	h = range_height (&sr.range);
	w = range_width (&sr.range);

	result = sol->result;
	switch (result->quality) {
	case GNM_SOLVER_RESULT_FEASIBLE:
	case GNM_SOLVER_RESULT_OPTIMAL:
		solution = result->solution;
		break;
	default:
	case GNM_SOLVER_RESULT_NONE:
	case GNM_SOLVER_RESULT_INFEASIBLE:
	case GNM_SOLVER_RESULT_UNBOUNDED:
		solution = NULL;
		break;
	}

	for (x = 0; x < w; x++) {
		for (y = 0; y < h; y++) {
			GnmValue *v = solution
				? value_dup (value_area_fetch_x_y (solution, x, y, NULL))
				: value_new_error_NA (NULL);
			GnmCell *cell =
				sheet_cell_fetch (sr.sheet,
						  sr.range.start.col + x,
						  sr.range.start.row + y);
			gnm_cell_set_value (cell, v);
			cell_queue_recalc (cell);
		}
	}
}

gboolean
gnm_solver_finished (GnmSolver *sol)
{
	g_return_val_if_fail (GNM_IS_SOLVER (sol), TRUE);

	switch (sol->status) {

	case GNM_SOLVER_STATUS_READY:
	case GNM_SOLVER_STATUS_PREPARING:
	case GNM_SOLVER_STATUS_PREPARED:
	case GNM_SOLVER_STATUS_RUNNING:
		return FALSE;
	case GNM_SOLVER_STATUS_DONE:
	default:
	case GNM_SOLVER_STATUS_ERROR:
	case GNM_SOLVER_STATUS_CANCELLED:
		return TRUE;
	}
}

void
gnm_solver_set_status (GnmSolver *solver, GnmSolverStatus status)
{
	if (status == solver->status)
		return;

	solver->status = status;
	g_object_notify (G_OBJECT (solver), "status");
}

gboolean
gnm_solver_saveas (GnmSolver *solver, WorkbookControl *wbc,
		   GOFileSaver *fs,
		   const char *template, char **filename,
		   GError **err)
{
	int fd;
	GsfOutput *output;
	FILE *file;
	GOIOContext *io_context;
	gboolean ok;
	WorkbookView *wbv = wb_control_view (wbc);

	fd = g_file_open_tmp (template, filename, err);
	if (fd == -1) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to create file for linear program"));
		return FALSE;
	}

	file = fdopen (fd, "wb");
	if (!file) {
		/* This shouldn't really happen.  */
		close (fd);
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to create linear program file"));
		return FALSE;
	}

	output = gsf_output_stdio_new_FILE (*filename, file, TRUE);
	io_context = go_io_context_new (GO_CMD_CONTEXT (wbc));
	wbv_save_to_output (wbv, fs, output, io_context);
	ok = !go_io_error_occurred (io_context);
	g_object_unref (io_context);
	g_object_unref (output);

	if (!ok) {
		g_set_error (err, G_FILE_ERROR, 0,
			     _("Failed to save linear program"));
		return FALSE;
	}

	return TRUE;
}

static void
gnm_solver_class_init (GObjectClass *object_class)
{
	gnm_solver_parent_class = g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_solver_dispose;
	object_class->set_property = gnm_solver_set_property;
	object_class->get_property = gnm_solver_get_property;

        g_object_class_install_property (object_class, SOL_PROP_STATUS,
		 g_param_spec_enum ("status", _("status"),
				    _("The solver's current status"),
				    GNM_SOLVER_STATUS_TYPE,
				    GNM_SOLVER_STATUS_READY,
				    GSF_PARAM_STATIC |
				    G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_PARAMS,
		 g_param_spec_pointer ("params", _("Parameters"),
				       _("Solver parameters"),
				       GSF_PARAM_STATIC |
				       G_PARAM_CONSTRUCT_ONLY |
				       G_PARAM_READWRITE));

	g_object_class_install_property (object_class, SOL_PROP_RESULT,
		 g_param_spec_object ("result", _("Result"),
				      _("Current best feasible result"),
				      GNM_SOLVER_RESULT_TYPE,
				      GSF_PARAM_STATIC |
				      G_PARAM_READWRITE));

	solver_signals[SOL_SIG_PREPARE] =
		g_signal_new ("prepare",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, prepare),
			      NULL, NULL,
			      gnm__BOOLEAN__OBJECT_POINTER,
			      G_TYPE_BOOLEAN, 2,
			      G_TYPE_OBJECT,
			      G_TYPE_POINTER);

	solver_signals[SOL_SIG_START] =
		g_signal_new ("start",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, start),
			      NULL, NULL,
			      gnm__BOOLEAN__OBJECT_POINTER,
			      G_TYPE_BOOLEAN, 2,
			      G_TYPE_OBJECT,
			      G_TYPE_POINTER);

	solver_signals[SOL_SIG_STOP] =
		g_signal_new ("stop",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GnmSolverClass, stop),
			      NULL, NULL,
			      gnm__BOOLEAN__POINTER,
			      G_TYPE_BOOLEAN, 1,
			      G_TYPE_POINTER);
}

GSF_CLASS (GnmSolver, gnm_solver,
	   &gnm_solver_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_result_parent_class;

static void
gnm_solver_result_finalize (GObject *obj)
{
	GnmSolverResult *r = GNM_SOLVER_RESULT (obj);
	value_release (r->solution);
	gnm_solver_result_parent_class->finalize (obj);
}

static void
gnm_solver_result_class_init (GObjectClass *object_class)
{
	gnm_solver_result_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->finalize = gnm_solver_result_finalize;
}

GSF_CLASS (GnmSolverResult, gnm_solver_result,
	   &gnm_solver_result_class_init, NULL, G_TYPE_OBJECT)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_sub_solver_parent_class;

void
gnm_sub_solver_clear (GnmSubSolver *subsol)
{
	int i;

	if (subsol->child_watch) {
		g_source_remove (subsol->child_watch);
		subsol->child_watch = 0;
		subsol->child_exit = NULL;
		subsol->exit_data = NULL;
	}

	if (subsol->child_pid) {
		kill (subsol->child_pid, SIGKILL);
		g_spawn_close_pid (subsol->child_pid);
		subsol->child_pid = (GPid)0;
	}

	for (i = 0; i <= 2; i++) {
		if (subsol->channel_watches[i]) {
			g_source_remove (subsol->channel_watches[i]);
			subsol->channel_watches[i] = 0;
		}
		if (subsol->channels[i]) {
			g_io_channel_unref (subsol->channels[i]);
			subsol->channels[i] = NULL;
		}
		if (subsol->fd[i] != -1) {
			close (subsol->fd[i]);
			subsol->fd[i] = -1;
		}
	}

	if (subsol->program_filename) {
		g_unlink (subsol->program_filename);
		g_free (subsol->program_filename);
		subsol->program_filename = NULL;
	}
}

static void
gnm_sub_solver_dispose (GObject *obj)
{
	GnmSubSolver *subsol = GNM_SUB_SOLVER (obj);

	gnm_sub_solver_clear (subsol);

	gnm_sub_solver_parent_class->dispose (obj);
}

static void
gnm_sub_solver_init (GnmSubSolver *subsol)
{
	int i;

	for (i = 0; i <= 2; i++)
		subsol->fd[i] = -1;
}

static void
cb_child_exit (GPid pid, gint status, GnmSubSolver *subsol)
{
	subsol->child_watch = 0;
	if (subsol->child_exit)
		subsol->child_exit (pid, status, subsol->exit_data);
}

gboolean
gnm_sub_solver_spawn (GnmSubSolver *subsol,
		      char **argv,
		      GSpawnChildSetupFunc child_setup, gpointer setup_data,
		      GChildWatchFunc child_exit, gpointer exit_data,
		      GIOFunc io_stdout, gpointer stdout_data,
		      GIOFunc io_stderr, gpointer stderr_data,
		      GError **err)
{
	GnmSolver *sol = GNM_SOLVER (subsol);
	gboolean ok;
	GSpawnFlags spflags = G_SPAWN_DO_NOT_REAP_CHILD;
	int fd;

	g_return_val_if_fail (subsol->child_watch == 0, FALSE);
	g_return_val_if_fail (sol->status == GNM_SOLVER_STATUS_PREPARED, FALSE);

	if (!g_path_is_absolute (argv[0]))
		spflags |= G_SPAWN_SEARCH_PATH;

	ok = g_spawn_async_with_pipes
		(g_get_home_dir (),  /* PWD */
		 argv,
		 NULL, /* environment */
		 spflags,
		 child_setup, setup_data,
		 &subsol->child_pid,
		 NULL,			/* stdin */
		 &subsol->fd[1],	/* stdout */
		 &subsol->fd[2],	/* stderr */
		 err);
	if (!ok)
		goto fail;

	subsol->child_exit = child_exit;
	subsol->exit_data = exit_data;
	subsol->child_watch =
		g_child_watch_add (subsol->child_pid,
				   (GChildWatchFunc)cb_child_exit, subsol);

	subsol->io_funcs[1] = io_stdout;
	subsol->io_funcs_data[1] = stdout_data;
	subsol->io_funcs[2] = io_stderr;
	subsol->io_funcs_data[2] = stderr_data;

	for (fd = 1; fd <= 2; fd++) {
		GIOFlags ioflags;

		if (subsol->io_funcs[fd] == NULL)
			continue;

		/*
		 * Despite the name these are documented to work on Win32.
		 * Let us hope that is actually true.
		 */
		subsol->channels[fd] = g_io_channel_unix_new (subsol->fd[fd]);
		ioflags = g_io_channel_get_flags (subsol->channels[fd]);
		g_io_channel_set_flags (subsol->channels[fd],
					ioflags | G_IO_FLAG_NONBLOCK,
					NULL);
		subsol->channel_watches[fd] =
			g_io_add_watch (subsol->channels[fd],
					G_IO_IN,
					subsol->io_funcs[fd],
					subsol->io_funcs_data[fd]);
	}

	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_RUNNING);
	return TRUE;

fail:
	gnm_sub_solver_clear (subsol);
	gnm_solver_set_status (sol, GNM_SOLVER_STATUS_ERROR);
	return FALSE;
}

void
gnm_sub_solver_flush (GnmSubSolver *subsol)
{
	int fd;

	for (fd = 1; fd <= 2; fd++) {
		if (subsol->io_funcs[fd] == NULL)
			continue;

		subsol->io_funcs[fd] (subsol->channels[fd],
				      G_IO_IN,
				      subsol->io_funcs_data[fd]);
	}
}

static void
gnm_sub_solver_class_init (GObjectClass *object_class)
{
	gnm_sub_solver_parent_class = g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_sub_solver_dispose;
}

GSF_CLASS (GnmSubSolver, gnm_sub_solver,
	   gnm_sub_solver_class_init, gnm_sub_solver_init, GNM_SOLVER_TYPE)

/* ------------------------------------------------------------------------- */

static GObjectClass *gnm_solver_factory_parent_class;

static void
gnm_solver_factory_dispose (GObject *obj)
{
	GnmSolverFactory *factory = GNM_SOLVER_FACTORY (obj);

	g_free (factory->id);
	g_free (factory->name);

	gnm_solver_factory_parent_class->dispose (obj);
}

static void
gnm_solver_factory_class_init (GObjectClass *object_class)
{
	gnm_solver_factory_parent_class =
		g_type_class_peek_parent (object_class);

	object_class->dispose = gnm_solver_factory_dispose;
}

GSF_CLASS (GnmSolverFactory, gnm_solver_factory,
	   gnm_solver_factory_class_init, NULL, G_TYPE_OBJECT)


static GSList *solvers;

GSList *
gnm_solver_db_get (void)
{
	return solvers;
}

GnmSolverFactory *
gnm_solver_factory_new (const char *id,
			const char *name,
			SolverModelType type,
			GnmSolverCreator creator)
{
	GnmSolverFactory *res;

	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (creator != NULL, NULL);

	res = g_object_new (GNM_SOLVER_FACTORY_TYPE, NULL);
	res->id = g_strdup (id);
	res->name = g_strdup (name);
	res->type = type;
	res->creator = creator;
	return res;
}

GnmSolver *
gnm_solver_factory_create (GnmSolverFactory *factory,
			   SolverParameters *param)
{
	g_return_val_if_fail (GNM_IS_SOLVER_FACTORY (factory), NULL);
	return factory->creator (factory, param);
}

void
gnm_solver_db_register (GnmSolverFactory *factory)
{
	if (gnm_debug_flag ("solver"))
		g_printerr ("Registering %s\n", factory->id);
	g_object_ref (factory);
	solvers = g_slist_prepend (solvers, factory);
}

void
gnm_solver_db_unregister (GnmSolverFactory *factory)
{
	if (gnm_debug_flag ("solver"))
		g_printerr ("Unregistering %s\n", factory->id);
	solvers = g_slist_remove (solvers, factory);
	g_object_unref (factory);
}

/* ------------------------------------------------------------------------- */
