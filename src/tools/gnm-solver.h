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
	GnmDepManaged lhs;

	/* Must be a constant or a range.  */
	GnmDepManaged rhs;
};

GType gnm_solver_constraint_get_type (void);

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
char *gnm_solver_constraint_part_as_str (GnmSolverConstraint const *c, int i,
					 GnmSolverParameters *sp);

/* ------------------------------------------------------------------------- */

typedef struct {
	int                 max_time_sec;
	unsigned            max_iter;
	GnmSolverFactory   *algorithm;
	GnmSolverModelType  model_type;
	gboolean            assume_non_negative;
	gboolean            assume_discrete;
	gboolean            automatic_scaling;
	gboolean            program_report;
	gboolean            sensitivity_report;
	gboolean            add_scenario;
	gchar               *scenario_name;
	unsigned            gradient_order;
} GnmSolverOptions;

#define GNM_SOLVER_PARAMETERS_TYPE   (gnm_solver_param_get_type ())
#define GNM_SOLVER_PARAMETERS(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_PARAMETERS_TYPE, GnmSolverParameters))
#define GNM_IS_SOLVER_PARAMETERS(o)  (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_PARAMETERS_TYPE))

struct GnmSolverParameters_ {
	GObject parent;

	/* Default parsing sheet.  No ref held.  */
	Sheet *sheet;

	GnmSolverProblemType problem_type;
	GnmDepManaged target;
	GnmDepManaged input;
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
GnmSolverParameters *gnm_solver_param_dup (GnmSolverParameters *src,
					   Sheet *new_sheet);

gboolean gnm_solver_param_equal (GnmSolverParameters const *a,
				 GnmSolverParameters const *b);

GnmValue const *gnm_solver_param_get_input (GnmSolverParameters const *sp);
void gnm_solver_param_set_input (GnmSolverParameters *sp, GnmValue *v);
GPtrArray *gnm_solver_param_get_input_cells (GnmSolverParameters const *sp);

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
	gnm_float *solution;
} GnmSolverResult;

typedef struct {
	GObjectClass parent_class;
} GnmSolverResultClass;

GType gnm_solver_result_get_type (void);

/* ------------------------------------------------------------------------- */

#define GNM_SOLVER_SENSITIVITY_TYPE   (gnm_solver_sensitivity_get_type ())
#define GNM_SOLVER_SENSITIVITY(o)     (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_SENSITIVITY_TYPE, GnmSolverSensitivity))

typedef struct {
	GObject parent;

	GnmSolver *solver;

	struct GnmSolverSensitivityVars_ {
		gnm_float low, high;      //  Allowable range
		gnm_float reduced_cost;
	} *vars;

	struct GnmSolverSensitivityConstraints_ {
		gnm_float low, high;      //  Allowable range
		gnm_float shadow_price;
	} *constraints;
} GnmSolverSensitivity;

typedef struct {
	GObjectClass parent_class;
} GnmSolverSensitivityClass;

GType gnm_solver_sensitivity_get_type (void);

GnmSolverSensitivity *gnm_solver_sensitivity_new (GnmSolver *sol);

/* ------------------------------------------------------------------------- */
/* Generic Solver class. */

#define GNM_SOLVER_TYPE        (gnm_solver_get_type ())
#define GNM_SOLVER(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_TYPE, GnmSolver))
#define GNM_IS_SOLVER(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_TYPE))

struct GnmSolver_ {
	GObject parent;

	GnmSolverStatus status;
	char *reason;

	GnmSolverParameters *params;
	GnmSolverResult *result;
	GnmSolverSensitivity *sensitivity;
	double starttime, endtime;
	gboolean flip_sign;

	/* Derived information */
	GnmCell *target;
	GPtrArray *input_cells;
	GHashTable *index_from_cell;
	gnm_float *min;
	gnm_float *max;
	guint8 *discrete;

	// Analytic gradient
	int gradient_status; // 0: not tried; 1: ok; 2: fail
	GPtrArray *gradient;

	// Analytic Hessian
	int hessian_status; // 0: not tried; 1: ok; 2: fail
	GPtrArray *hessian;
};

typedef struct {
	GObjectClass parent_class;

	gboolean (*prepare) (GnmSolver *sol,
			     WorkbookControl *wbc, GError **err);
	gboolean (*start) (GnmSolver *sol,
			   WorkbookControl *wbc, GError **err);
	gboolean (*stop) (GnmSolver *sol, GError **err);
} GnmSolverClass;

GType gnm_solver_get_type (void);

gboolean gnm_solver_prepare (GnmSolver *sol,
			     WorkbookControl *wbc, GError **err);
gboolean gnm_solver_start (GnmSolver *sol,
			   WorkbookControl *wbc, GError **err);
gboolean gnm_solver_stop (GnmSolver *sol, GError **err);

void gnm_solver_set_status (GnmSolver *solver, GnmSolverStatus status);

void gnm_solver_set_reason (GnmSolver *solver, const char *reason);

void gnm_solver_store_result (GnmSolver *solver);

void gnm_solver_create_report (GnmSolver *solver, const char *name);

double gnm_solver_elapsed (GnmSolver *solver);

gboolean gnm_solver_check_timeout (GnmSolver *solver);

gboolean gnm_solver_finished (GnmSolver *solver);

gboolean gnm_solver_has_solution (GnmSolver *solver);

gboolean gnm_solver_check_constraints (GnmSolver *solver);

gboolean gnm_solver_saveas (GnmSolver *solver, WorkbookControl *wbc,
			    GOFileSaver *fs,
			    const char *templ, char **filename,
			    GError **err);

gboolean gnm_solver_debug (void);

int gnm_solver_cell_index (GnmSolver *solver, GnmCell const *cell);

gnm_float gnm_solver_get_target_value (GnmSolver *solver);
void gnm_solver_set_var (GnmSolver *sol, int i, gnm_float x);
void gnm_solver_set_vars (GnmSolver *sol, gnm_float const *xs);

GPtrArray *gnm_solver_save_vars (GnmSolver *sol);
void gnm_solver_restore_vars (GnmSolver *sol, GPtrArray *vals);

gboolean gnm_solver_has_analytic_gradient (GnmSolver *sol);
gnm_float *gnm_solver_compute_gradient (GnmSolver *sol, gnm_float const *xs);

gboolean gnm_solver_has_analytic_hessian (GnmSolver *sol);
GnmMatrix *gnm_solver_compute_hessian (GnmSolver *sol, gnm_float const *xs);

gnm_float gnm_solver_line_search (GnmSolver *sol,
				  gnm_float const *x0, gnm_float const *dir,
				  gboolean try_reverse,
				  gnm_float step, gnm_float max_step, gnm_float eps,
				  gnm_float *py);

void gnm_solver_pick_lp_coords (GnmSolver *sol,
				gnm_float **px1, gnm_float **px2);

gnm_float *gnm_solver_get_lp_coeffs (GnmSolver *sol, GnmCell *ycell,
				     gnm_float const *x1, gnm_float const *x2,
				     GError **err);

/* ------------------------------------------------------------------------- */
/* Solver subclass for subprocesses. */

#define GNM_SUB_SOLVER_TYPE     (gnm_sub_solver_get_type ())
#define GNM_SUB_SOLVER(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SUB_SOLVER_TYPE, GnmSubSolver))
#define GNM_IS_SUB_SOLVER(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SUB_SOLVER_TYPE))

typedef struct {
	GnmSolver parent;

	char *program_filename;

	/* Hashes between char* and cell*.  */
	GHashTable *cell_from_name;
	GHashTable *name_from_cell;

	GHashTable *constraint_from_name;

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

	void (*child_exit) (GnmSubSolver *subsol, gboolean normal, int code);
} GnmSubSolverClass;

GType gnm_sub_solver_get_type (void);

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

const char *gnm_sub_solver_name_constraint (GnmSubSolver *subsol,
					    int cidx,
					    const char *name);
int gnm_sub_solver_find_constraint (GnmSubSolver *subsol, const char *name);

char *gnm_sub_solver_locate_binary (const char *binary, const char *solver,
				    const char *url,
				    WBCGtk *wbcg);

/* ------------------------------------------------------------------------- */

typedef struct GnmIterSolver_ GnmIterSolver;
typedef struct GnmSolverIterator_ GnmSolverIterator;

/* Utility class for single iteration in a solving process.  */

#define GNM_SOLVER_ITERATOR_TYPE     (gnm_solver_iterator_get_type ())
#define GNM_SOLVER_ITERATOR(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_ITERATOR_TYPE, GnmSolverIterator))
#define GNM_IS_SOLVER_ITERATOR(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_ITERATOR_TYPE))

struct GnmSolverIterator_ {
	GObject parent;
};

typedef struct {
	GObjectClass parent_class;

	gboolean (*iterate) (GnmSolverIterator *iter);
} GnmSolverIteratorClass;

GType gnm_solver_iterator_get_type (void);

GnmSolverIterator *gnm_solver_iterator_new_func (GCallback iterate, gpointer user);
GnmSolverIterator *gnm_solver_iterator_new_polish (GnmIterSolver *isol);
GnmSolverIterator *gnm_solver_iterator_new_gradient (GnmIterSolver *isol);

gboolean gnm_solver_iterator_iterate (GnmSolverIterator *iter);



#define GNM_SOLVER_ITERATOR_COMPOUND_TYPE     (gnm_solver_iterator_compound_get_type ())
#define GNM_SOLVER_ITERATOR_COMPOUND(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_ITERATOR_COMPOUND_TYPE, GnmSolverIteratorCompound))
#define GNM_IS_SOLVER_ITERATOR_COMPOUND(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_ITERATOR_COMPOUND_TYPE))

typedef struct {
	GnmSolverIterator parent;
	unsigned cycles;

	/* <protected> */
	GPtrArray *iterators;
	unsigned *counts;
	unsigned next, next_counter, cycle;
	gboolean cycle_progress;
} GnmSolverIteratorCompound;

typedef struct {
	GnmSolverIteratorClass parent_class;
} GnmSolverIteratorCompoundClass;

GType gnm_solver_iterator_compound_get_type (void);
void gnm_solver_iterator_compound_add (GnmSolverIteratorCompound *ic,
				       GnmSolverIterator *iter,
				       unsigned count);

/* ------------------------------------------------------------------------- */
/* Solver subclass for iterative in-process solvers. */

#define GNM_ITER_SOLVER_TYPE     (gnm_iter_solver_get_type ())
#define GNM_ITER_SOLVER(o)       (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_ITER_SOLVER_TYPE, GnmIterSolver))
#define GNM_IS_ITER_SOLVER(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_ITER_SOLVER_TYPE))

struct GnmIterSolver_ {
	GnmSolver parent;

	/* Current point */
	gnm_float *xk, yk;

	GnmSolverIterator *iterator;

	guint64 iterations;

	/* <private> */
	guint idle_tag;
};

typedef struct {
	GnmSolverClass parent_class;
} GnmIterSolverClass;

GType gnm_iter_solver_get_type (void);

void gnm_iter_solver_set_iterator (GnmIterSolver *isol, GnmSolverIterator *iterator);

gboolean gnm_iter_solver_get_initial_solution (GnmIterSolver *isol, GError **err);
void gnm_iter_solver_set_solution (GnmIterSolver *isol);

/* ------------------------------------------------------------------------- */

#define GNM_SOLVER_FACTORY_TYPE        (gnm_solver_factory_get_type ())
#define GNM_SOLVER_FACTORY(o)          (G_TYPE_CHECK_INSTANCE_CAST ((o), GNM_SOLVER_FACTORY_TYPE, GnmSolverFactory))
#define GNM_IS_SOLVER_FACTORY(o)       (G_TYPE_CHECK_INSTANCE_TYPE ((o), GNM_SOLVER_FACTORY_TYPE))

typedef GnmSolver * (*GnmSolverCreator) (GnmSolverFactory *factory,
					 GnmSolverParameters *param,
					 gpointer data);
typedef gboolean (*GnmSolverFactoryFunctional) (GnmSolverFactory *factory,
						WBCGtk *wbcg,
						gpointer data);

struct GnmSolverFactory_ {
	GObject parent;

	/* <private> */
	char *id;
	char *name; /* Already translated */
	GnmSolverModelType type;
	GnmSolverCreator creator;
	GnmSolverFactoryFunctional functional;
	gpointer data;
	GDestroyNotify notify;
};

typedef struct {
	GObjectClass parent_class;
} GnmSolverFactoryClass;

GType gnm_solver_factory_get_type (void);

GnmSolverFactory *gnm_solver_factory_new (const char *id,
					  const char *name,
					  GnmSolverModelType type,
					  GnmSolverCreator creator,
					  GnmSolverFactoryFunctional functional,
					  gpointer data,
					  GDestroyNotify notify);
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
