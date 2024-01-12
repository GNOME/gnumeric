/*
 * simulation.c: Monte Carlo Simulation tool.
 *
 * Author:
 *        Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include <gnumeric.h>

#include <sheet.h>
#include <cell.h>
#include <ranges.h>
#include <value.h>
#include <workbook-view.h>
#include <workbook-control.h>
#include <sheet.h>

#include <mathfunc.h>
#include <rangefunc.h>
#include <tools/simulation.h>

static void
init_stats (simstats_t *stats, simulation_t *sim)
{
	stats->min        = g_new (gnm_float, sim->n_vars);
	stats->max        = g_new (gnm_float, sim->n_vars);
	stats->mean       = g_new (gnm_float, sim->n_vars);
	stats->median     = g_new (gnm_float, sim->n_vars);
	stats->mode       = g_new (gnm_float, sim->n_vars);
	stats->stddev     = g_new (gnm_float, sim->n_vars);
	stats->var        = g_new (gnm_float, sim->n_vars);
	stats->skew       = g_new (gnm_float, sim->n_vars);
	stats->kurtosis   = g_new (gnm_float, sim->n_vars);
	stats->range      = g_new (gnm_float, sim->n_vars);
	stats->confidence = g_new (gnm_float, sim->n_vars);
	stats->lower      = g_new (gnm_float, sim->n_vars);
	stats->upper      = g_new (gnm_float, sim->n_vars);
	stats->errmask    = g_new (int, sim->n_vars);
}

static void
free_stats (simstats_t *stats, G_GNUC_UNUSED simulation_t *sim)
{
	g_free (stats->min);
	g_free (stats->max);
	g_free (stats->mean);
	g_free (stats->median);
	g_free (stats->mode);
	g_free (stats->stddev);
	g_free (stats->var);
	g_free (stats->skew);
	g_free (stats->kurtosis);
	g_free (stats->range);
	g_free (stats->confidence);
	g_free (stats->lower);
	g_free (stats->upper);
	g_free (stats->errmask);
}

static const gchar *
eval_inputs_list (simulation_t *sim, gnm_float **outputs, int iter,
		  G_GNUC_UNUSED int round)
{
	GSList *cur;
	int    i = sim->n_output_vars;

	/* Recompute inputs. */
	for (cur = sim->list_inputs; cur != NULL; cur = cur->next) {
		GnmCell *cell = cur->data;

		cell_queue_recalc (cell);
		gnm_cell_eval (cell);

		if (cell->value == NULL || ! VALUE_IS_NUMBER (cell->value)) {
			return _("Input variable did not yield to a numeric "
				 "value. Check the model (maybe your last "
				 "round # is too high).");
		}

		if (outputs)
			outputs[i++][iter] = value_get_as_float (cell->value);
	}

	return NULL;
}

static const gchar *
eval_outputs_list (simulation_t *sim, gnm_float **outputs, int iter,
		   G_GNUC_UNUSED int round)
{
	GSList *cur;
	int    i = 0;

	/* Recompute outputs. */
	for (cur = sim->list_outputs; cur != NULL; cur = cur->next) {
		GnmCell *cell = cur->data;

		gnm_cell_eval (cell);
		if (cell->value == NULL || ! VALUE_IS_NUMBER (cell->value)) {
			return _("Output variable did not yield to a numeric "
				 "value. Check the output variables in your "
				 "model (maybe your last round # is too "
				 "high).");
		}

		if (outputs)
			outputs[i++][iter] = value_get_as_float (cell->value);
	}

	return NULL;
}

static const gchar *
recompute_outputs (simulation_t *sim, gnm_float **outputs, int iter,
		   int round)
{
	const gchar *err = eval_inputs_list (sim, outputs, iter, round);

	if (err)
		return err;

	return eval_outputs_list (sim, outputs, iter, round);
}

static void
create_stats (simulation_t *sim, gnm_float **outputs, simstats_t *stats)
{
	int        i, error;
	gnm_float x;

	/* Initialize. */
	for (i = 0; i < sim->n_vars; i++)
		stats->errmask[i] = 0;

	/* Calculate stats. */
	for (i = 0; i < sim->n_vars; i++) {
		/* Min */
		error = gnm_range_min (outputs[i], sim->n_iterations, &x);
		stats->min[i] = x;

		/* Mean */
		error = gnm_range_average (outputs[i], sim->n_iterations, &x);
		stats->mean[i] = x;

		/* Max */
		error = gnm_range_max (outputs[i], sim->n_iterations, &x);
		stats->max[i] = x;

		/* Median */
		error = gnm_range_median_inter (outputs[i], sim->n_iterations,
					    &x);
		if (error)
			stats->errmask[i] |= MedianErr;
		else
			stats->median[i] = x;

		/* Mode */
		error = gnm_range_mode (outputs[i], sim->n_iterations, &x);
		if (error)
			stats->errmask[i] |= ModeErr;
		else
			stats->mode[i] = x;

		/* Standard deviation */
		error = gnm_range_stddev_pop (outputs[i], sim->n_iterations, &x);
		if (error)
			stats->errmask[i] |= VarErr;
		else
			stats->stddev[i] = x;

		/* Variance */
		error = gnm_range_var_pop (outputs[i], sim->n_iterations, &x);
		if (error)
			stats->errmask[i] |= VarErr;
		else
			stats->var[i] = x;

		/* Skewness */
		error = gnm_range_skew_est (outputs[i], sim->n_iterations, &x);
		if (error)
			stats->errmask[i] |= SkewErr;
		else
			stats->skew[i] = x;

		/* Kurtosis */
		error = gnm_range_kurtosis_m3_est (outputs[i], sim->n_iterations,
					       &x);
		if (error)
			stats->errmask[i] |= KurtosisErr;
		else
			stats->kurtosis[i] = x;

		/* Range */
		stats->range[i] = stats->max[i] - stats->min[i];

		/* Confidence (95%) */
		stats->confidence[i] = 2 * qt (0.025, sim->n_iterations - 1,
					       FALSE, FALSE)
			* (stats->stddev[i] / gnm_sqrt (sim->n_iterations));

		/* Lower Confidence (95%) */
		stats->lower[i] = stats->mean[i] - stats->confidence[i] / 2;

		/* upper Confidence (95%) */
		stats->upper[i] = stats->mean[i] + stats->confidence[i] / 2;
	}
}

static void
create_reports (WorkbookControl *wbc, simulation_t *sim, simstats_t **stats,
		data_analysis_output_t *dao, Sheet *sheet)
{
	int i, n, t, n_rounds, rinc;

	n_rounds = 1 + sim->last_round - sim->first_round;

	dao_prepare_output (wbc, dao, _("Simulation Report"));
	if (dao->type == NewSheetOutput || dao->type == NewWorkbookOutput)
		g_object_set (dao->sheet, "display-grid", FALSE, NULL);

	/*
	 * Set this to fool the autofit_column function.  (It will be
	 * overwritten).
	 */
	dao_set_cell (dao, 0, 0, "A");

	rinc = sim->n_vars + 4;
	for (n = 0, t = sim->first_round; t <= sim->last_round; t++, n++) {
		dao_set_cell (dao,  2, 6 + n * rinc, _("Min"));
		dao_set_cell (dao,  3, 6 + n * rinc, _("Mean"));
		dao_set_cell (dao,  4, 6 + n * rinc, _("Max"));
		dao_set_cell (dao,  5, 6 + n * rinc, _("Median"));
		dao_set_cell (dao,  6, 6 + n * rinc, _("Mode"));
		dao_set_cell (dao,  7, 6 + n * rinc, _("Std. Dev."));
		dao_set_cell (dao,  8, 6 + n * rinc, _("Variance"));
		dao_set_cell (dao,  9, 6 + n * rinc, _("Skewness"));
		dao_set_cell (dao, 10, 6 + n * rinc, _("Kurtosis"));
		dao_set_cell (dao, 11, 6 + n * rinc, _("Range"));
		dao_set_cell (dao, 12, 6 + n * rinc, _("Count"));
		dao_set_cell (dao, 13, 6 + n * rinc, _("Confidence (95%)"));
		dao_set_cell (dao, 14, 6 + n * rinc, _("Lower Limit (95%)"));
		dao_set_cell (dao, 15, 6 + n * rinc, _("Upper Limit (95%)"));
		dao_set_bold (dao,  1, 6 + n * rinc, 15, 6 + n * rinc);

		for (i = 0; i < sim->n_vars; i++) {
			dao_set_cell (dao, 1, i + 7 + n * rinc,
				      sim->cellnames[i]);
			dao_set_bold (dao, 1, i + 7 + n * rinc, 1,
				      i + 7 + n * rinc);
			dao_set_cell_float (dao, 2, i + 7 + n * rinc,
					    stats[t]->min[i]);
			dao_set_cell_float (dao, 3, i + 7 + n * rinc,
					    stats[t]->mean[i]);
			dao_set_cell_float (dao, 4, i + 7 + n * rinc,
					    stats[t]->max[i]);
			dao_set_cell_float (dao, 5, i + 7 + n * rinc,
					    stats[t]->median[i]);
			dao_set_cell_float_na
				(dao, 6, i + 7 + n * rinc, stats[t]->mode[i],
				 ! (stats[t]->errmask[i] & ModeErr));
			dao_set_cell_float_na
				(dao, 7, i + 7 + n * rinc,
				 stats[t]->stddev[i],
				 ! (stats[t]->errmask[i] & StddevErr));
			dao_set_cell_float_na
				(dao, 8, i + 7 + n * rinc, stats[t]->var[i],
				 ! (stats[t]->errmask[i] & VarErr));
			dao_set_cell_float_na
				(dao, 9, i + 7 + n * rinc, stats[t]->skew[i],
				 ! (stats[t]->errmask[i] & SkewErr));
			dao_set_cell_float_na
				(dao, 10, i + 7 + n * rinc,
				 stats[t]->kurtosis[i],
				 ! (stats[t]->errmask[i] & KurtosisErr));
			dao_set_cell_float (dao, 11, i + 7 + n * rinc,
					    stats[t]->range[i]);
			dao_set_cell_float (dao, 12, i + 7 + n * rinc,
					    sim->n_iterations);
			dao_set_cell_float_na
				(dao, 13, i + 7 + n * rinc,
				 stats[t]->confidence[i],
				 ! (stats[t]->errmask[i] & StddevErr));
			dao_set_cell_float_na
				(dao, 14, i + 7 + n * rinc,
				 stats[t]->lower[i],
				 ! (stats[t]->errmask[i] & StddevErr));
			dao_set_cell_float_na
				(dao, 15, i + 7 + n * rinc,
				 stats[t]->upper[i],
				 ! (stats[t]->errmask[i] & StddevErr));
		}
	}


	/*
	 * Autofit columns to make the sheet more readable.
	 */

	dao_autofit_these_columns (dao, 0, 15);


	/*
	 * Fill in the titles.
	 */

	/* Fill in the column A labels into the simultaion report sheet. */
	if (n_rounds > 1)
		for (n = sim->first_round; n <= sim->last_round; n++) {
			char  *tmp = g_strdup_printf
				("%s%d", _("SUMMARY OF SIMULATION ROUND #"),
				 n + 1);
			int   ind = 5 + rinc * (n - sim->first_round);

			dao_set_cell (dao, 0, ind, tmp);
			dao_set_italic (dao, 0, ind, 0, ind);
		}
	else {
		dao_set_cell (dao, 0, 5, _("SUMMARY"));
		dao_set_italic (dao, 0, 5, 0, 5);
	}


	/* Fill in the header titles. */
	dao_write_header (dao, _("Risk Simulation"), _("Report"), sheet);
}

/*
 * Monte Carlo Simulation tool.  Helps decision making by generating
 * random numbers for given input variables.
 */
gchar const *
simulation_tool (WorkbookControl        *wbc,
		 data_analysis_output_t *dao,
		 simulation_t           *sim)
{
	int          round, i;
	gnm_float   **outputs;
	simstats_t   **stats;
	Sheet        *sheet;
	gchar const  *err = NULL;
	WorkbookView *wbv;
	GSList       *cur;

	wbv   = wb_control_view (wbc);
	sheet = wb_view_cur_sheet (wbv);

	/* Initialize results storage. */
	sim->cellnames = g_new (gchar *, sim->n_vars);
	outputs        = g_new (gnm_float *, sim->n_vars);
	for (i = 0; i < sim->n_vars; i++)
		outputs[i] = g_new (gnm_float, sim->n_iterations);

	stats     = g_new (simstats_t *, sim->last_round + 1);
	for (i = 0; i <= sim->last_round; i++) {
		stats[i] = g_new (simstats_t, 1);
		init_stats (stats[i], sim);
	}

	i = 0;
	for (cur = sim->list_outputs; cur != NULL; cur = cur->next) {
		GnmCell *cell = cur->data;

		sim->cellnames[i++] =
			(gchar *) dao_find_name (sheet, cell->pos.col,
						 cell->pos.row);
	}
	for (cur = sim->list_inputs; cur != NULL; cur = cur->next) {
		GnmCell *cell = cur->data;
		gchar *tmp = dao_find_name (sheet, cell->pos.col,
					    cell->pos.row);
		gchar *prefix = _("(Input) ");
		gchar *buf = g_strdup_printf ("%s %s", prefix, tmp);
		g_free (tmp);
		sim->cellnames[i++] = buf;
	}

	/* Run the simulations. */
	for (round = sim->first_round; round <= sim->last_round; round++) {
		sheet->simulation_round = round;
		for (i = 0; i < sim->n_iterations; i++) {
			err = recompute_outputs (sim, outputs, i, round);
			if (i % 100 == 99) {
				sim->end = g_get_monotonic_time ();
				if ((sim->end - sim->start) / 1e6 >
				    sim->max_time) {
					err = _("Maximum time exceeded. "
						"Simulation was not "
						"completed.");
					goto out;
				}
			}
			if (err != NULL)
				goto out;
		}
		create_stats (sim, outputs, stats[round]);
	}
 out:
	sheet->simulation_round = 0;
	eval_inputs_list (sim, NULL, 0, 0);
	eval_outputs_list (sim, NULL, 0, 0);

	/* Free results storage. */
	for (i = 0; i < sim->n_vars; i++)
		g_free (outputs[i]);
	g_free (outputs);

	if (err == NULL) {
		/* Create the reports. */
		create_reports (wbc, sim, stats, dao, sheet);
	}

	sim->stats = stats;

	sheet_redraw_all (sheet, TRUE);

	return err;
}

void
simulation_tool_destroy (simulation_t *sim)
{
	int i;

	if (sim == NULL)
		return;

	/* Free statistics storage. */
	for (i = 0; i <= sim->last_round; i++)
		free_stats (sim->stats[i], sim);

	g_free (sim->stats);


	/* Free the names of the cells. */
	for (i = 0; i < sim->n_vars; i++)
		g_free (sim->cellnames[i]);

	g_free (sim->cellnames);
}
