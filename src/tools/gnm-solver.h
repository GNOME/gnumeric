#ifndef _TOOLS_GNM_SOLVER_H_
#define _TOOLS_GNM_SOLVER_H_

#include <gnumeric.h>
#include <glib-object.h>
#include <dependent.h>
#include <numbers.h>
#include <wbc-gtk.h>

G_BEGIN_DECLS

/* ------------------------------------------------------------------------- */

typedef enum {
	GNM_SOLVER_RESULT_NONE,
	GNM_SOLVER_RESULT_FEASIBLE,
	GNM_SOLVER_RESULT_OPTIMAL,
	GNM_SOLVER_RESULT_INFEASIBLE,
	GNM_SOLVER_RESULT_UNBOUNDED
} GnmSolverResultQuality;


GType gnm_solver_status_get_type (void);
#define GNM_SOLVER_STATUS_TYPE (gnm_solver_status_get_type ())

typedef enum {
	GNM_SOLVER_STATUS_READY,
	GNM_SOLVER_STATUS_PREPARING,
	GNM_SOLVER_STATUS_PREPARED,
	GNM_SOLVER_STATUS_RUNNING,
	GNM_SOLVER_STATUS_DONE,
	GNM_SOLVER_STATUS_ERROR,
	GNM_SOLVER_STATUS_CANCELLED
} GnmSolverStatus;


typedef enum {
        GNM_SOLVER_LE,
	GNM_SOLVER_GE,
	GNM_SOLVER_EQ,
	GNM_SOLVER_INTEGER,
	GNM_SOLVER_BOOLEAN
} GnmSolverConstraintType;


typedef enum {
	GNM_SOLVER_LP, GNM_SOLVER_QP, GNM_SOLVER_NLP
} GnmSolverModelType;


GType gnm_solver_problem_type_get_type (void);
#define GNM_SOLVER_PROBLEM_TYPE_TYPE (gnm_solver_problem_type_get_type ())

typedef enum {
        GNM_SOLVER_MINIMIZE, GNM_SOLVER_MAXIMIZE
} GnmSolverProblemType;

/* -------------------------------------------------------------------------- */

struct GnmSolverConstraint_ {
	GnmSolverConstraintType type;

	/* Must be a range.  */
	GnmDependent lhs;

	/* Must be a constant or a range.  */
	GnmDependent rhs;
};

GnmSolverConstraint *gnm_solver_constraint_new (Sheet *sheet);
void gnm_solver_constraint_free (GnmSolverConstraint *c);
GnmSolverConstraint *gnm_solver_constraint_dup (GnmSolverConstraint *c,
						Sheet *sheet);
gboolean gnm_solver_constraint_equal (GnmSolverConstraint const *a,
				      GnmSolverConstraint const *b);

void gnm_solver_constraint_set_old (GnmSolverConstraint *c,
				    GnmSolverConstraintType type,
				    int lhs_col, int lhs_row,
				    int rhs_col, int rhs_row,
				    int cols, int rows);

gboolean gnm_solver_constraint_has_rhs (GnmSolverConstraint const *c);
gboolean gnm_solver_constraint_valid (GnmSolverConstraint const *c,
				      GnmSolverParameters const *sp);
gboolean gnm_solver_constraint_get_part (GnmSolverConstraint const *c,
					 GnmSolverParameters const *sp, int i,
					 GnmCell **lhs, gnm_float *cl,
					 GnmCell **rhs, gnm_float *cr);

GnmValue const *gnm_solver_constraint_get_lhs (GnmSolverConstraint const *c);
GnmValue const *gnm_solver_constraint_get_rhs (GnmSolverConstraint const *c);

void gnm_solver_constraint_set_lhs (GnmSolverConstraint *c, GnmValue *v);
void gnm_solver_constraint_set_rhs (GnmSolverConstraint *c, GnmValue *v);

void gnm_solver_constraint_side_as_str (GnmSolverConstraint const *c,
					Sheet const *sheet,
					GString *buf, gboolean lhs);
char *gnm_solver_constraint_as_str (GnmSolverConstraint const *c, Sheet *sheet);

/* ------------------------------------------------------------------------- */

typedef struct {
	int                 max_time_sec;
	int                 max_iter;
	GnmSolverFactory   *algorithm;
	GnmSolverModelType  model_type;
	gboolean            assume_non_negative;
	gboolean            assume_discrete;
	gboolean            automatic_scaling;
	gboolean            program_report;
	gboolean            add_scenario;
	gchar               *scenario_name;
} GnmSolverOptions;

#define GNM_SOLVER_PARAMETERS_TYPE   (gnm_solver_param_get_type ())
#define GNM_SOLVER_PARAMETERS(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_PARAMETERS_TYPE, GnmSolverParameters))
#define GNM_IS_SOLVER_PARAMETERS(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_PARAMETERS_TYPE))

struct GnmSolverParameters_ {
	GObject parent;

	/* Default parsing sheet.  No ref held.  */
	Sheet *sheet;

	GnmSolverProblemType problem_type;
	GnmDependent target;
	GnmDependent input;
	GSList *constraints;
	GnmSolverOptions options;
};

typedef struct {
	GObjectClass parent_class;
} GnmSolverParametersClass;

GType gnm_solver_param_get_type (void);

/* Creates a new GnmSolverParameters object. */
GnmSolverParameters *gnm_solver_param_new (Sheet *sheet);

/* Duplicate a GnmSolverParameters object. */
GnmSolverParameters *gnm_solver_param_dup (GnmSolverParameters *src_param,
					   Sheet *new_sheet);

gboolean gnm_solver_param_equal (GnmSolverParameters const *a,
				 GnmSolverParameters const *b);

GnmValue const *gnm_solver_param_get_input (GnmSolverParameters const *sp);
void gnm_solver_param_set_input (GnmSolverParameters *sp, GnmValue *v);
GSList *gnm_solver_param_get_input_cells (GnmSolverParameters const *sp);

const GnmCellRef *gnm_solver_param_get_target (GnmSolverParameters const *sp);
void gnm_solver_param_set_target (GnmSolverParameters *sp,
				  GnmCellRef const *cr);
GnmCell *gnm_solver_param_get_target_cell (GnmSolverParameters const *sp);

void gnm_solver_param_set_algorithm (GnmSolverParameters *sp,
				     GnmSolverFactory *algo);

gboolean gnm_solver_param_valid (GnmSolverParameters const *sp, GError **err);

/* -------------------------------------------------------------------------- */

#define GNM_SOLVER_RESULT_TYPE   (gnm_solver_result_get_type ())
#define GNM_SOLVER_RESULT(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_RESULT_TYPE, GnmSolverResult))

typedef struct {
	GObject parent;

	GnmSolverResultQuality quality;

	/* Objective value, if any */
	gnm_float value;

	/* Array value of solution, if any */
	GnmValue *solution;
} GnmSolverResult;

typedef struct {
	GObjectClass parent_class;
} GnmSolverResultClass;

GType gnm_solver_result_get_type (void);

/* ------------------------------------------------------------------------- */
/* Generic Solver class. */

#define GNM_SOLVER_TYPE        (gnm_solver_get_type ())
#define GNM_SOLVER(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_TYPE, GnmSolver))
#define GNM_SOLVER_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SOLVER_TYPE, GnmSolverClass))
#define GNM_IS_SOLVER(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_TYPE))

typedef struct {
	GObject parent;

	GnmSolverStatus status;
	char *reason;

	GnmSolverParameters *params;
	GnmSolverResult *result;
	double starttime, endtime;
} GnmSolver;

typedef struct {
	GObjectClass parent_class;

	gboolean (*prepare) (GnmSolver *solver,
			     WorkbookControl *wbc, GError **err);
	gboolean (*start) (GnmSolver *solver,
			   WorkbookControl *wbc, GError **err);
	gboolean (*stop) (GnmSolver *solver, GError **err);
	void (*child_exit) (GnmSolver *solver, gboolean normal, int code);
} GnmSolverClass;

GType gnm_solver_get_type  (void);

gboolean gnm_solver_prepare (GnmSolver *solver,
			     WorkbookControl *wbc, GError **err);
gboolean gnm_solver_start (GnmSolver *solver,
			   WorkbookControl *wbc, GError **err);
gboolean gnm_solver_stop (GnmSolver *solver, GError **err);

void gnm_solver_set_status (GnmSolver *solver, GnmSolverStatus status);

void gnm_solver_set_reason (GnmSolver *solver, const char *reason);

void gnm_solver_store_result (GnmSolver *solver);

void gnm_solver_create_report (GnmSolver *solver, const char *name);

double gnm_solver_elapsed (GnmSolver *solver);

gboolean gnm_solver_check_timeout (GnmSolver *solver);

gboolean gnm_solver_finished (GnmSolver *solver);

gboolean gnm_solver_has_solution (GnmSolver *solver);

gboolean gnm_solver_check_constraints (GnmSolver *solver);

GnmValue *gnm_solver_get_current_values (GnmSolver *solver);

gboolean gnm_solver_saveas (GnmSolver *solver, WorkbookControl *wbc,
			    GOFileSaver *fs,
			    const char *templ, char **filename,
			    GError **err);

gboolean gnm_solver_debug (void);

/* ------------------------------------------------------------------------- */
/* Solver subclass for subprocesses. */

#define GNM_SUB_SOLVER_TYPE     (gnm_sub_solver_get_type ())
#define GNM_SUB_SOLVER(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SUB_SOLVER_TYPE, GnmSubSolver))
#define GNM_SUB_SOLVER_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), GNM_SUB_SOLVER_TYPE, GnmSubSolverClass))
#define GNM_IS_SUB_SOLVER(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SUB_SOLVER_TYPE))

typedef struct {
	GnmSolver parent;

	char *program_filename;

	/* Hashes between char* and cell*.  */
	GHashTable *cell_from_name;
	GHashTable *name_from_cell;

	GPid child_pid;
	guint child_watch;

	gint fd[3];
	GIOChannel *channels[3];
	guint channel_watches[3];
	GIOFunc io_funcs[3];
	gpointer io_funcs_data[3];
} GnmSubSolver;

typedef struct {
	GnmSolverClass parent_class;
} GnmSubSolverClass;

GType gnm_sub_solver_get_type  (void);

void gnm_sub_solver_clear (GnmSubSolver *subsol);

gboolean gnm_sub_solver_spawn
		(GnmSubSolver *subsol,
		 char **argv,
		 GSpawnChildSetupFunc child_setup, gpointer setup_data,
		 GIOFunc io_stdout, gpointer stdout_data,
		 GIOFunc io_stderr, gpointer stderr_data,
		 GError **err);

void gnm_sub_solver_flush (GnmSubSolver *subsol);

const char *gnm_sub_solver_name_cell (GnmSubSolver *subsol,
				      GnmCell const *cell,
				      const char *name);
GnmCell *gnm_sub_solver_find_cell (GnmSubSolver *subsol, const char *name);
const char *gnm_sub_solver_get_cell_name (GnmSubSolver *subsol,
					  GnmCell const *cell);

char *gnm_sub_solver_locate_binary (const char *binary, const char *solver,
				    const char *url,
				    WBCGtk *wbcg);

/* ------------------------------------------------------------------------- */

#define GNM_SOLVER_FACTORY_TYPE        (gnm_solver_factory_get_type ())
#define GNM_SOLVER_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_FACTORY_TYPE, GnmSolverFactory))
#define GNM_IS_SOLVER_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_FACTORY_TYPE))

typedef GnmSolver * (*GnmSolverCreator) (GnmSolverFactory *,
					 GnmSolverParameters *);
typedef gboolean (*GnmSolverFactoryFunctional) (GnmSolverFactory *,
						WBCGtk *);

struct GnmSolverFactory_ {
	GObject parent;

	char *id;
	char *name; /* Already translated */
	GnmSolverModelType type;
	GnmSolverCreator creator;
	GnmSolverFactoryFunctional functional;
};

typedef struct {
	GObjectClass parent_class;
} GnmSolverFactoryClass;

GType gnm_solver_factory_get_type (void);

GnmSolverFactory *gnm_solver_factory_new (const char *id,
					  const char *name,
					  GnmSolverModelType type,
					  GnmSolverCreator creator,
					  GnmSolverFactoryFunctional funct);
GnmSolver *gnm_solver_factory_create (GnmSolverFactory *factory,
				      GnmSolverParameters *param);
gboolean gnm_solver_factory_functional (GnmSolverFactory *factory,
					WBCGtk *wbcg);

GSList *gnm_solver_db_get (void);
void gnm_solver_db_register (GnmSolverFactory *factory);
void gnm_solver_db_unregister (GnmSolverFactory *factory);

/* ------------------------------------------------------------------------- */

G_END_DECLS

#endif /* _TOOLS_GNM_SOLVER_H_ */
