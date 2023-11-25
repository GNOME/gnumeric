/*
 * dialog-random-generator.c:
 *
 * Authors:
 *  Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 *  Andreas J. Guelzow  <aguelzow@taliesin.ca>
 *
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
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
#include <dialogs/dialogs.h>
#include <dialogs/help.h>
#include <dialogs/tool-dialogs.h>
#include <tools/random-generator.h>

#include <workbook.h>
#include <workbook-control.h>
#include <wbc-gtk.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <gnm-format.h>
#include <dialogs/dao-gui-utils.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <commands.h>

#include <widgets/gnm-expr-entry.h>
#include <widgets/gnm-dao.h>

#include <string.h>


/**********************************************/
/*  Generic guru items */
/**********************************************/


#define RANDOM_KEY            "analysistools-random-dialog"

typedef struct {
	GnmGenericToolState base;
	GtkWidget *distribution_grid;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label;
	GtkWidget *par1_entry;
	GtkWidget *par1_expr_entry;
	GtkWidget *par2_label;
	GtkWidget *par2_entry;
	GtkWidget *vars_entry;
	GtkWidget *count_entry;
	random_distribution_t distribution;
} RandomToolState;


/**********************************************/
/*  Begin of random tool code */
/**********************************************/

typedef struct {
        GtkWidget *dialog;
	GtkWidget *distribution_grid;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label, *par1_entry;
	GtkWidget *par2_label, *par2_entry;
} random_tool_callback_t;

/* Name to show in list and parameter labels for a random distribution */
typedef struct {
	random_distribution_t dist;
	const char *name;
	const char *label1;
	const char *label2;
	gboolean par1_is_range;
} DistributionStrs;

/* Distribution strings for Random Number Generator */
static const DistributionStrs distribution_strs[] = {
        /* The most commonly used are listed first.  I think uniform, gaussian
	 * and discrete are the most commonly used, or what do you think? */

        { UniformDistribution,
	  N_("Uniform"), N_("_Lower Bound:"), N_("_Upper Bound:"), FALSE },
        { UniformIntDistribution,
	  N_("Uniform Integer"), N_("_Lower Bound:"), N_("_Upper Bound:"),
	  FALSE },
        { NormalDistribution,
	  N_("Normal"), N_("_Mean:"), N_("_Standard Deviation:"), FALSE },
        { DiscreteDistribution,
	  N_("Discrete"), N_("_Value And Probability Input Range:"), NULL,
	  TRUE },

	/* The others are in alphabetical order. */

        { BernoulliDistribution,
	  N_("Bernoulli"), N_("_p Value:"), NULL, FALSE },
        { BetaDistribution,
	  N_("Beta"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ BinomialDistribution,
	  N_("Binomial"), N_("_p Value:"), N_("N_umber of Trials:"), FALSE },
	{ CauchyDistribution,
	  N_("Cauchy"), N_("_a Value:"), NULL, FALSE },
	{ ChisqDistribution,
	  N_("Chisq"), N_("_nu Value:"), NULL, FALSE },
	{ ExponentialDistribution,
	  N_("Exponential"), N_("_b Value:"), NULL, FALSE },
	{ ExponentialPowerDistribution,
	  N_("Exponential Power"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ FdistDistribution,
	  N_("F"), N_("nu_1 Value:"), N_("nu_2 Value:"), FALSE },
	{ GammaDistribution,
	  N_("Gamma"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ GaussianTailDistribution,
	  N_("Gaussian Tail"), N_("_a Value:"), N_("_Sigma"), FALSE },
	{ GeometricDistribution,
	  N_("Geometric"), N_("_p Value:"), NULL, FALSE },
	{ Gumbel1Distribution,
	  N_("Gumbel (Type I)"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ Gumbel2Distribution,
	  N_("Gumbel (Type II)"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ LandauDistribution,
	  N_("Landau"), NULL, NULL, FALSE },
	{ LaplaceDistribution,
	  N_("Laplace"), N_("_a Value:"), NULL, FALSE },
	{ LevyDistribution,
	  N_("Levy alpha-Stable"), N_("_c Value:"), N_("_alpha:"), FALSE },
	{ LogarithmicDistribution,
	  N_("Logarithmic"), N_("_p Value:"), NULL, FALSE },
	{ LogisticDistribution,
	  N_("Logistic"), N_("_a Value:"), NULL, FALSE },
	{ LognormalDistribution,
	  N_("Lognormal"), N_("_Zeta Value:"), N_("_Sigma"), FALSE },
	{ NegativeBinomialDistribution,
	  N_("Negative Binomial"), N_("_p Value:"),
	  N_("N_umber of Failures"), FALSE },
	{ ParetoDistribution,
	  N_("Pareto"), N_("_a Value:"), N_("_b Value:"), FALSE },
	{ PoissonDistribution,
	  N_("Poisson"), N_("_Lambda:"), NULL, FALSE },
	{ RayleighDistribution,
	  N_("Rayleigh"), N_("_Sigma:"), NULL, FALSE },
	{ RayleighTailDistribution,
	  N_("Rayleigh Tail"), N_("_a Value:"), N_("_Sigma:"), FALSE },
	{ TdistDistribution,
	  N_("Student t"), N_("nu Value:"), NULL, FALSE },
	{ WeibullDistribution,
	  N_("Weibull"), N_("_a Value:"), N_("_b Value:"), FALSE },
        { 0, NULL, NULL, NULL, FALSE }
};

/*
 * distribution_strs_find
 * @dist  Distribution enum
 *
 * Find the strings record, given distribution enum.
 * Returns pointer to strings record.
 */
static const DistributionStrs *
distribution_strs_find (random_distribution_t dist)
{
	int i;

	for (i = 0; distribution_strs[i].name != NULL; i++)
		if (distribution_strs[i].dist == dist)
			return &distribution_strs[i];

	return &distribution_strs[0];
}

/*
 * combo_get_distribution
 * @combo  combo widget with distribution list
 *
 * Find from combo the distribution the user selected
 */
static random_distribution_t
combo_get_distribution (GtkWidget *combo)
{
	return distribution_strs[gtk_combo_box_get_active (GTK_COMBO_BOX (combo))].dist;
}

/**
 * random_tool_update_sensitivity:
 * @dummy:
 * @state:
 *
 * Update the dialog widgets sensitivity if the only items of interest
 * are the standard input (one range) and output items.
 **/
static void
random_tool_update_sensitivity_cb (G_GNUC_UNUSED GtkWidget *dummy,
				   RandomToolState *state)
{
	gboolean   ready  = FALSE;
	gint       count, vars;
	gnm_float a_float, b_float, from_val, to_val, p_val;
	GnmValue      *disc_prob_range;
	random_distribution_t the_dist;

	the_dist = combo_get_distribution (state->distribution_combo);

	ready = ((entry_to_int (GTK_ENTRY (state->vars_entry), &vars, FALSE) == 0 &&
		  vars > 0) &&
		 (entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE) == 0 &&
		  count > 0) &&
                 gnm_dao_is_ready (GNM_DAO (state->base.gdao)));

	switch (the_dist) {
	case NormalDistribution:
		ready = ready && entry_to_float (GTK_ENTRY (state->par1_entry), &a_float,
						 FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &a_float,
					FALSE) == 0;
		break;
	case BernoulliDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			p_val <= 1 && p_val > 0;
		break;
	case BetaDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0;
		break;
	case PoissonDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case ExponentialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case ExponentialPowerDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case CauchyDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case ChisqDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case LandauDistribution:
		ready = TRUE;
		break;
	case LaplaceDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case GaussianTailDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case RayleighDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case RayleighTailDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case ParetoDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case LevyDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case FdistDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case LognormalDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case TdistDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case WeibullDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case GeometricDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			p_val >= 0 && p_val <= 1;
		break;
	case LogarithmicDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			p_val >= 0 && p_val <= 1;
		break;
	case LogisticDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		break;
	case GammaDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case Gumbel1Distribution:
	case Gumbel2Distribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0;
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &b_float, FALSE) == 0 &&
			b_float > 0;
		break;
	case BinomialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 &&
			p_val <= 1 && p_val > 0 &&
			count > 0;
		break;
	case NegativeBinomialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 &&
			p_val <= 1 && p_val > 0 &&
			count > 0;
		break;
	case DiscreteDistribution:
		disc_prob_range = gnm_expr_entry_parse_as_value
			(GNM_EXPR_ENTRY (state->par1_expr_entry), state->base.sheet);
		ready = ready && disc_prob_range != NULL;
		value_release (disc_prob_range);
		break;
	case UniformIntDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &from_val, FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &to_val, FALSE) == 0 &&
			from_val <= to_val;
		break;
	case UniformDistribution:
	default:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &from_val, FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &to_val, FALSE) == 0 &&
			from_val <= to_val;
		break;
	}

	gtk_widget_set_sensitive (state->base.apply_button, ready);
	gtk_widget_set_sensitive (state->base.ok_button, ready);
}

/*
 * distribution_parbox_config
 * @p     Callback data
 * @dist  Distribution
 *
 * Configure parameter widgets given random distribution.
 *
 * Set labels and accelerators, and hide/show entry fields as needed.
 **/

static void
distribution_parbox_config (RandomToolState *state,
			    random_distribution_t dist)
{
	GtkWidget *par1_entry;
	const DistributionStrs *ds = distribution_strs_find (dist);

	if (ds->par1_is_range) {
		par1_entry = state->par1_expr_entry;
		gtk_widget_hide (state->par1_entry);
	} else {
		par1_entry = state->par1_entry;
		gtk_widget_hide (state->par1_expr_entry);
	}
	if (ds->label1 != NULL) {
		gtk_label_set_text_with_mnemonic (GTK_LABEL (state->par1_label),
			_(ds->label1));
		gtk_label_set_mnemonic_widget (GTK_LABEL (state->par1_label),
			par1_entry);
		gtk_widget_show (par1_entry);
	} else {
		gtk_label_set_text (GTK_LABEL (state->par1_label), "");
		gtk_widget_hide (par1_entry);
	}

	if (ds->label2 != NULL) {
		gtk_label_set_text_with_mnemonic (GTK_LABEL (state->par2_label),
			_(ds->label2));
		gtk_label_set_mnemonic_widget (GTK_LABEL (state->par2_label),
			state->par2_entry);
		gtk_widget_show (state->par2_entry);
	} else {
		gtk_label_set_text (GTK_LABEL (state->par2_label), "");
		gtk_widget_hide (state->par2_entry);
	}
}

/*
 * distribution_callback
 * @widget  Not used
 * @p       Callback data
 *
 * Configure the random distribution parameters widgets for the distribution
 * which was selected.
 */
static void
distribution_callback (G_GNUC_UNUSED GtkWidget *widget,
		       RandomToolState *state)
{
	random_distribution_t dist;

	dist = combo_get_distribution (state->distribution_combo);
	distribution_parbox_config (state, dist);
}


/**
 * dialog_random_realized:
 * @widget
 * @state:
 *
 * Make initial geometry of distribution table permanent.
 *
 * The dialog is constructed with the distribution_grid containing the widgets
 * which need the most space. At construction time, we do not know how large
 * the distribution_grid needs to be, but we do know when the dialog is
 * realized. This callback for "realized" makes this size the user specified
 * size so that the table will not shrink when we later change label texts and
 * hide/show widgets.
  *
 **/
static void
dialog_random_realized (GtkWidget *widget, RandomToolState *state)
{
	GtkWidget *t = state->distribution_grid;
	GtkWidget *l = state->par1_label;
	GtkAllocation a;

	gtk_widget_get_allocation (t, &a);
	gtk_widget_set_size_request (t, a.width, a.height);

	gtk_widget_get_allocation (l, &a);
	gtk_widget_set_size_request (l, a.width, a.height);

	distribution_callback (widget, state);
}


/**
 * random_tool_ok_clicked_cb:
 * @button:
 * @state:
 *
 * Retrieve the information from the dialog and call the appropriate tool.
 * Note that we assume that the ok_button is only active if the entry fields
 * contain sensible data.
 **/
static void
random_tool_ok_clicked_cb (GtkWidget *button, RandomToolState *state)
{
	data_analysis_output_t  *dao;
	tools_data_random_t  *data;

	gint err;

	data = g_new0 (tools_data_random_t, 1);
	dao  = parse_output ((GnmGenericToolState *)state, NULL);

	data->wbc = GNM_WBC (state->base.wbcg);

	err = entry_to_int (GTK_ENTRY (state->vars_entry), &data->n_vars, FALSE);
	err = entry_to_int (GTK_ENTRY (state->count_entry), &data->count, FALSE);

	data->distribution = state->distribution =
		combo_get_distribution (state->distribution_combo);
	switch (state->distribution) {
	case NormalDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.normal.mean, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.normal.stdev, TRUE);
		break;
	case BernoulliDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.bernoulli.p, TRUE);
		break;
	case BetaDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.beta.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.beta.b, TRUE);
		break;
	case PoissonDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.poisson.lambda, TRUE);
		break;
	case ExponentialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.exponential.b, TRUE);
		break;
	case ExponentialPowerDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.exppow.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.exppow.b, TRUE);
		break;
	case CauchyDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.cauchy.a, TRUE);
		break;
	case LandauDistribution:
		break;
	case LaplaceDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.laplace.a, TRUE);
		break;
	case GaussianTailDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.gaussian_tail.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.gaussian_tail.sigma, TRUE);
		break;
	case ChisqDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.chisq.nu, TRUE);
		break;
	case LogarithmicDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.logarithmic.p, TRUE);
		break;
	case LogisticDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.logistic.a, TRUE);
		break;
	case RayleighDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.rayleigh.sigma, TRUE);
		break;
	case RayleighTailDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.rayleigh_tail.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.rayleigh_tail.sigma, TRUE);
		break;
	case LognormalDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.lognormal.zeta, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.lognormal.sigma, TRUE);
		break;
	case LevyDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.levy.c, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.levy.alpha, TRUE);
		break;
	case FdistDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.fdist.nu1, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.fdist.nu2, TRUE);
		break;
	case ParetoDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.pareto.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.pareto.b, TRUE);
		break;
	case TdistDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.tdist.nu, TRUE);
		break;
	case WeibullDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.weibull.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.weibull.b, TRUE);
		break;
	case GeometricDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.geometric.p, TRUE);
		break;
	case GammaDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.gamma.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.gamma.b, TRUE);
		break;
	case Gumbel1Distribution:
	case Gumbel2Distribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.gumbel.a, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				      &data->param.gumbel.b, TRUE);
		break;
	case BinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.binomial.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry),
				    &data->param.binomial.trials, TRUE);
		break;
	case NegativeBinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				      &data->param.negbinom.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry),
				    &data->param.negbinom.f, TRUE);
		break;
	case DiscreteDistribution:
		data->param.discrete.range = gnm_expr_entry_parse_as_value (
			GNM_EXPR_ENTRY (state->par1_expr_entry),
			state->base.sheet);
		break;
	case UniformIntDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				     &data->param.uniform.lower_limit, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				     &data->param.uniform.upper_limit, TRUE);
		break;
	case UniformDistribution:
	default:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				     &data->param.uniform.lower_limit, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				     &data->param.uniform.upper_limit, TRUE);
		break;
	}
	(void)err;

	if (!cmd_analysis_tool (GNM_WBC (state->base.wbcg),
				state->base.sheet,
				dao, data, tool_random_engine, TRUE) &&
	    (button == state->base.ok_button))
		gtk_widget_destroy (state->base.dialog);
}

/**
 * dialog_random_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static void
dialog_random_tool_init (RandomToolState *state)
{
	int   i, dist_str_no;
	const DistributionStrs *ds;
/*	GList *distribution_type_strs = NULL;*/
	GtkGrid *grid;
	GtkListStore *store;
	GtkTreeIter iter;
	GtkCellRenderer *renderer;
	GnmRange const *first;

	state->distribution = UniformDistribution;

	state->distribution_grid = go_gtk_builder_get_widget (state->base.gui,
							  "distribution-grid");
	state->distribution_combo = go_gtk_builder_get_widget (state->base.gui,
							  "distribution_combo");
	state->par1_entry = go_gtk_builder_get_widget (state->base.gui, "par1_entry");
	state->par1_label = go_gtk_builder_get_widget (state->base.gui, "par1_label");
	state->par2_label = go_gtk_builder_get_widget (state->base.gui, "par2_label");
	state->par2_entry = go_gtk_builder_get_widget (state->base.gui, "par2_entry");
	state->vars_entry = go_gtk_builder_get_widget (state->base.gui, "vars_entry");
	state->count_entry = go_gtk_builder_get_widget (state->base.gui, "count_entry");
	int_to_entry (GTK_ENTRY (state->count_entry), 1);

	renderer = (GtkCellRenderer*) gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (state->distribution_combo), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (state->distribution_combo), renderer,
                                        "text", 0,
                                        NULL);
	store = gtk_list_store_new (1, G_TYPE_STRING);
	gtk_combo_box_set_model (GTK_COMBO_BOX (state->distribution_combo),
				 GTK_TREE_MODEL (store));
	g_object_unref (store);
	for (i = 0, dist_str_no = 0; distribution_strs[i].name != NULL; i++) {
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter,
					0, _(distribution_strs[i].name),
					-1);
		if (distribution_strs[i].dist == state->distribution)
			dist_str_no = i;
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (state->distribution_combo),
				       dist_str_no);

	ds = distribution_strs_find (UniformDistribution);
	gtk_label_set_text_with_mnemonic (GTK_LABEL (state->par1_label),
					  _(ds->label1));

	g_signal_connect (state->distribution_combo,
		"changed",
		G_CALLBACK (distribution_callback), state);
	g_signal_connect (state->distribution_combo,
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);

	grid = GTK_GRID (go_gtk_builder_get_widget (state->base.gui, "distribution-grid"));
	state->par1_expr_entry = GTK_WIDGET (gnm_expr_entry_new (state->base.wbcg, TRUE));
	gnm_expr_entry_set_flags (GNM_EXPR_ENTRY (state->par1_expr_entry),
				  GNM_EE_SINGLE_RANGE, GNM_EE_MASK);
	gtk_widget_set_hexpand (state->par1_expr_entry, TRUE);
	gtk_grid_attach (grid, state->par1_expr_entry, 1, 1, 1, 1);
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->par1_expr_entry));

	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->par1_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->par2_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->vars_entry));
	gnm_editable_enters (GTK_WINDOW (state->base.dialog),
				  GTK_WIDGET (state->count_entry));

	g_signal_connect (G_OBJECT (state->base.dialog),
		"realize",
		G_CALLBACK (dialog_random_realized), state);
	g_signal_connect_after (G_OBJECT (state->vars_entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->count_entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->par1_entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->par2_entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);
	g_signal_connect_after (G_OBJECT (state->par1_expr_entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);

	first = selection_first_range (state->base.sv, NULL, NULL);
	if (first != NULL) {
		dialog_tool_preset_to_range (&state->base);
		int_to_entry (GTK_ENTRY (state->count_entry),
			      first->end.row - first->start.row + 1);
		int_to_entry (GTK_ENTRY (state->vars_entry),
			      first->end.col - first->start.col + 1);
	}

	random_tool_update_sensitivity_cb (NULL, state);
}


/**
 * dialog_random_tool:
 * @wbcg:
 * @sheet:
 *
 * Show the dialog (guru).
 *
 **/
int
dialog_random_tool (WBCGtk *wbcg, Sheet *sheet)
{
        RandomToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnm_dialog_raise_if_exists (wbcg, RANDOM_KEY)) {
		return 0;
	}

	state = g_new (RandomToolState, 1);

	if (dialog_tool_init ((GnmGenericToolState *)state, wbcg, sheet,
			      GNUMERIC_HELP_LINK_RANDOM_GENERATOR,
			      "res:ui/random-generation.ui", "Random",
			      _("Could not create the Random Tool dialog."),
			      RANDOM_KEY,
			      G_CALLBACK (random_tool_ok_clicked_cb), NULL,
			      G_CALLBACK (random_tool_update_sensitivity_cb),
			      0))
		return 0;


	gnm_dao_set_put (GNM_DAO (state->base.gdao), FALSE, FALSE);
	dialog_random_tool_init (state);
	gtk_widget_show (state->base.dialog);

        return 0;
}
