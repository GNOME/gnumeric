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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <gnumeric-config.h>
#include <gnumeric.h>
#include "dialogs.h"
#include "analysis-tools.h"
#include "random-generator.h"

#include <workbook.h>
#include <workbook-control.h>
#include <workbook-edit.h>
#include <workbook-view.h>
#include <gui-util.h>
#include <parse-util.h>
#include <format.h>
#include <tools.h>
#include <dao-gui-utils.h>
#include <sheet.h>
#include <expr.h>
#include <number-match.h>
#include <ranges.h>
#include <selection.h>
#include <value.h>
#include <widgets/gnumeric-expr-entry.h>

#include <libgnome/gnome-i18n.h>
#include <glade/glade.h>
#include <string.h>
#include <commands.h>

/**********************************************/
/*  Generic guru items */
/**********************************************/



#define RANDOM_KEY            "analysistools-random-dialog"

ANALYSISTOOLS_OUTPUT_GROUP       /* defined in dao.h */

typedef struct {
	GENERIC_TOOL_STATE

	GtkWidget *distribution_table;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label;
	GtkWidget *par1_entry;
	GtkWidget *par1_expr_entry;
	GtkWidget *par2_label;
	GtkWidget *par2_entry;
	GtkWidget *vars_entry;
	GtkWidget *count_entry;
	GtkAccelGroup *distribution_accel;
	random_distribution_t distribution;
} RandomToolState;


/**********************************************/
/*  Begin of random tool code */
/**********************************************/

typedef struct {
        GtkWidget *dialog;
	GtkWidget *distribution_table;
        GtkWidget *distribution_combo;
	GtkWidget *par1_label, *par1_entry;
	GtkWidget *par2_label, *par2_entry;
	GtkAccelGroup *distribution_accel;
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
        { DiscreteDistribution,
	  N_("Discrete"), N_("_Value And Probability Input Range:"), NULL, TRUE },
        { NormalDistribution,
	  N_("Normal"), N_("_Mean:"), N_("_Standard Deviation:"), FALSE },
     	{ PoissonDistribution,
	  N_("Poisson"), N_("_Lambda:"), NULL, FALSE },
	{ ExponentialDistribution,
	  N_("Exponential"), N_("_b Value:"), NULL, FALSE },
	{ BinomialDistribution,
	  N_("Binomial"), N_("_p Value:"), N_("N_umber of Trials:"), FALSE },
	{ NegativeBinomialDistribution,
	  N_("Negative Binomial"), N_("_p Value:"),
	  N_("N_umber of Failures"), FALSE },
        { BernoulliDistribution,
	  N_("Bernoulli"), N_("_p Value:"), NULL, FALSE },
        { UniformDistribution,
	  N_("Uniform"), N_("_Lower Bound:"),  N_("_Upper Bound:"), FALSE },
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
        char const *text;
	int i;

        text = gtk_entry_get_text (GTK_ENTRY (GTK_COMBO (combo)->entry));

	for (i = 0; distribution_strs[i].name != NULL; i++)
		if (strcmp (text, _(distribution_strs[i].name)) == 0)
			return distribution_strs[i].dist;

	return UniformDistribution;
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
random_tool_update_sensitivity_cb (GtkWidget *dummy, RandomToolState *state)
{
	gboolean ready  = FALSE;
	gint count, vars, i;
	gnum_float a_float, from_val, to_val, p_val;
        Value *output_range;
	Value *disc_prob_range;
	random_distribution_t the_dist;

        output_range = gnm_expr_entry_parse_as_value
		(GNUMERIC_EXPR_ENTRY (state->output_entry), state->sheet);
	the_dist = combo_get_distribution (state->distribution_combo);

	i = gnumeric_glade_group_value (state->gui, output_group);

	ready = ((entry_to_int (GTK_ENTRY (state->vars_entry), &vars, FALSE) == 0 &&
		  vars > 0) &&
		 (entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE) == 0 &&
		  count > 0) &&
                 ((i != 2) ||
		  (output_range != NULL)));
        if (output_range != NULL) value_release (output_range);

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
			p_val <= 1.0 && p_val > 0.0;
		break;
	case PoissonDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0.0;
		break;
	case ExponentialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &a_float, FALSE) == 0 &&
			a_float > 0.0;
		break;
	case BinomialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 &&
			p_val <= 1.0 && p_val > 0.0 &&
			count > 0;
		break;
	case NegativeBinomialDistribution:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &p_val, FALSE) == 0 &&
			entry_to_int (GTK_ENTRY (state->par2_entry), &count, FALSE) == 0 &&
			p_val <= 1.0 && p_val > 0.0 &&
			count > 0;
		break;
	case DiscreteDistribution:
		disc_prob_range = gnm_expr_entry_parse_as_value
			(GNUMERIC_EXPR_ENTRY (state->par1_expr_entry), state->sheet);
		ready = ready && disc_prob_range != NULL;
		if (disc_prob_range != NULL) value_release (disc_prob_range);
		break;
	case UniformDistribution:
	default:
		ready = ready &&
			entry_to_float (GTK_ENTRY (state->par1_entry), &from_val, FALSE) == 0 &&
			entry_to_float (GTK_ENTRY (state->par2_entry), &to_val, FALSE) == 0 &&
			from_val <= to_val;
		break;
	}

	gtk_widget_set_sensitive (state->clear_outputrange_button, (i == 2));
	gtk_widget_set_sensitive (state->retain_format_button, (i == 2));
	gtk_widget_set_sensitive (state->retain_comments_button, (i == 2));

	gtk_widget_set_sensitive (state->apply_button, ready);
	gtk_widget_set_sensitive (state->ok_button, ready);
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
	guint par1_key = 0, par2_key = 0;
	const DistributionStrs *ds = distribution_strs_find (dist);

	if (ds->par1_is_range) {
		par1_entry = state->par1_expr_entry;
		gtk_widget_hide (state->par1_entry);
	} else {
		par1_entry = state->par1_entry;
		gtk_widget_hide (state->par1_expr_entry);
	}
	gtk_widget_show (par1_entry);

	if (state->distribution_accel != NULL) {
		gtk_window_remove_accel_group (GTK_WINDOW (state->dialog),
					       state->distribution_accel);
		state->distribution_accel = NULL;
	}
	state->distribution_accel = gtk_accel_group_new ();

	par1_key = gtk_label_parse_uline (GTK_LABEL (state->par1_label),
					  _(ds->label1));
	if (par1_key != GDK_VoidSymbol)
		gtk_widget_add_accelerator (par1_entry, "grab_focus",
					    state->distribution_accel, par1_key,
					    GDK_MOD1_MASK, 0);
	if (ds->label2 != NULL) {
		par2_key = gtk_label_parse_uline (GTK_LABEL (state->par2_label),
						  _(ds->label2));
		if (par2_key != GDK_VoidSymbol)
			gtk_widget_add_accelerator
				(state->par2_entry, "grab_focus",
				 state->distribution_accel, par2_key,
				 GDK_MOD1_MASK, 0);
	        gtk_widget_show (state->par2_entry);
	} else {
		gtk_label_set_text (GTK_LABEL (state->par2_label), "");
	        gtk_widget_hide (state->par2_entry);
	}
	gtk_window_add_accel_group (GTK_WINDOW (state->dialog),
				    state->distribution_accel);
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
distribution_callback (GtkWidget *widget, RandomToolState *state)
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
 * The dialog is constructed with the distribution_table containing the widgets
 * which need the most space. At construction time, we do not know how large
 * the distribution_table needs to be, but we do know when the dialog is
 * realized. This callback for "realized" makes this size the user specified
 * size so that the table will not shrink when we later change label texts and
 * hide/show widgets.
  *
 **/
static void
dialog_random_realized (GtkWidget *widget, RandomToolState *state)
{
	GtkWidget *t = state->distribution_table;
	GtkWidget *l = state->par1_label;

	gtk_widget_set_usize (t, t->allocation.width, t->allocation.height);
	gtk_widget_set_usize (l, l->allocation.width, l->allocation.height);
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

	data_analysis_output_t  dao;
        char   *text;
	gint vars, count, err;
	random_tool_t           param;

	if (state->warning_dialog != NULL)
		gtk_widget_destroy (state->warning_dialog);

        parse_output ((GenericToolState *)state, &dao);

	err = entry_to_int (GTK_ENTRY (state->vars_entry), &vars, FALSE);
	err = entry_to_int (GTK_ENTRY (state->count_entry), &count, FALSE);

	state->distribution = combo_get_distribution (state->distribution_combo);
	switch (state->distribution) {
	case NormalDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.normal.mean, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry), &param.normal.stdev, TRUE);
		break;
	case BernoulliDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.bernoulli.p, TRUE);
		break;
	case PoissonDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.poisson.lambda, TRUE);
		break;
	case ExponentialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.exponential.b, TRUE);
		break;
	case BinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.binomial.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry), &param.binomial.trials, TRUE);
		break;
	case NegativeBinomialDistribution:
		err = entry_to_float (GTK_ENTRY (state->par1_entry), &param.negbinom.p, TRUE);
		err = entry_to_int (GTK_ENTRY (state->par2_entry), &param.negbinom.f, TRUE);
		break;
	case DiscreteDistribution:
		param.discrete.range = gnm_expr_entry_parse_as_value (
			GNUMERIC_EXPR_ENTRY (state->par1_expr_entry), state->sheet);
		break;
	case UniformDistribution:
	default:
		err = entry_to_float (GTK_ENTRY (state->par1_entry),
				     &param.uniform.lower_limit, TRUE);
		err = entry_to_float (GTK_ENTRY (state->par2_entry),
				     &param.uniform.upper_limit, TRUE);
		break;
	}

	err = random_tool (WORKBOOK_CONTROL (state->wbcg), state->sheet,
			   vars, count, state->distribution, &param,&dao);
	switch (err) {
	case 0:
		if (button == state->ok_button) {
			if (state->distribution_accel) {
				gtk_window_remove_accel_group (GTK_WINDOW (state->dialog),
							       state->distribution_accel);
				state->distribution_accel = NULL;
			}
			gtk_widget_destroy (state->dialog);
		}
		break;
	case 1: /* non-numeric probability (DiscreteDistribution) */
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->par1_expr_entry),
				_("The probability input range contains a non-numeric value.\n"
				  "All probabilities must be non-negative numbers."));
		break;
        case 2: /* probabilities are all zero  (DiscreteDistribution) */
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->par1_expr_entry),
				_("The probabilities may not all be 0!"));
		break;
        case 3: /* negative probability  (DiscreteDistribution) */
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->par1_expr_entry),
				_("The probability input range contains a negative number.\n"
				"All probabilities must be non-negative!"));
		break;
        case 4: /* value is empty  (DiscreteDistribution) */
		error_in_entry ((GenericToolState *) state, GTK_WIDGET (state->par1_expr_entry),
				_("None of the values in the value range may be empty!"));
		break;
	default:
		text = g_strdup_printf (_("An unexpected error has occurred: %d."), err);
		gnumeric_notice (state->wbcg, GTK_MESSAGE_ERROR, text);
		g_free (text);
		break;
	}
	return;
}



/**
 * dialog_random_tool_init:
 * @state:
 *
 * Create the dialog (guru).
 *
 **/
static gboolean
dialog_random_tool_init (RandomToolState *state)
{
	int   i, dist_str_no;
	const DistributionStrs *ds;
	GList *distribution_type_strs = NULL;
	GtkTable *table;
	Range const *first;

	state->gui = gnumeric_glade_xml_new (state->wbcg, "random-generation.glade");
        if (state->gui == NULL)
                return TRUE;

	state->dialog = glade_xml_get_widget (state->gui, "Random");
        if (state->dialog == NULL)
                return TRUE;


	state->accel = NULL;
	state->distribution_accel = NULL;
	state->distribution = DiscreteDistribution;

	dialog_tool_init_buttons ((GenericToolState *)state,
				  G_CALLBACK (random_tool_ok_clicked_cb) );

	dialog_tool_init_outputs ((GenericToolState *)state,
				  G_CALLBACK (random_tool_update_sensitivity_cb));

	state->distribution_table = glade_xml_get_widget (state->gui, "distribution_table");
	state->distribution_combo = glade_xml_get_widget (state->gui, "distribution_combo");
	state->par1_entry = glade_xml_get_widget (state->gui, "par1_entry");
	state->par1_label = glade_xml_get_widget (state->gui, "par1_label");
	state->par2_label = glade_xml_get_widget (state->gui, "par2_label");
	state->par2_entry = glade_xml_get_widget (state->gui, "par2_entry");
	state->vars_entry = glade_xml_get_widget (state->gui, "vars_entry");
	state->count_entry = glade_xml_get_widget (state->gui, "count_entry");
	int_to_entry (GTK_ENTRY (state->count_entry), 1);

	for (i = 0, dist_str_no = 0; distribution_strs[i].name != NULL; i++) {
		distribution_type_strs
			= g_list_append (distribution_type_strs,
					 (gpointer) _(distribution_strs[i].name));
		if (distribution_strs[i].dist == state->distribution)
			dist_str_no = i;
	}
	gtk_combo_set_popdown_strings (GTK_COMBO (state->distribution_combo),
				       distribution_type_strs);
	g_list_free (distribution_type_strs);
	distribution_type_strs = NULL;

	gtk_entry_set_text (GTK_ENTRY (GTK_COMBO (state->distribution_combo)->entry),
			   _(distribution_strs[dist_str_no].name));

	ds = distribution_strs_find (DiscreteDistribution);
	(void) gtk_label_parse_uline (GTK_LABEL (state->par1_label), _(ds->label1));

  	g_signal_connect (G_OBJECT (GTK_COMBO (state->distribution_combo)->entry),
		"changed",
		G_CALLBACK (distribution_callback), state);
  	g_signal_connect (G_OBJECT (GTK_COMBO (state->distribution_combo)->entry),
		"changed",
		G_CALLBACK (random_tool_update_sensitivity_cb), state);

	table = GTK_TABLE (glade_xml_get_widget (state->gui, "distribution_table"));
	state->par1_expr_entry = GTK_WIDGET (gnumeric_expr_entry_new (state->wbcg, TRUE));
	gnm_expr_entry_set_flags (GNUMERIC_EXPR_ENTRY (state->par1_expr_entry),
				       GNUM_EE_SINGLE_RANGE, GNUM_EE_MASK);
        gnm_expr_entry_set_scg (GNUMERIC_EXPR_ENTRY (state->par1_expr_entry),
				wbcg_cur_scg (state->wbcg));
	gtk_table_attach (table, state->par1_expr_entry,
			  1, 2, 1, 2,
			  GTK_EXPAND | GTK_FILL, 0,
			  0, 0);
 	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->par1_expr_entry));


	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->par1_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->par2_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->vars_entry));
	gnumeric_editable_enters (GTK_WINDOW (state->dialog),
				  GTK_WIDGET (state->count_entry));

	wbcg_edit_attach_guru (state->wbcg, state->dialog);
	g_signal_connect (G_OBJECT (state->dialog),
		"destroy",
		G_CALLBACK (tool_destroy), state);
	g_signal_connect (G_OBJECT (state->dialog),
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

	gnumeric_keyed_dialog (state->wbcg, GTK_WINDOW (state->dialog),
			       RANDOM_KEY);

	first = selection_first_range (state->sheet, NULL, NULL);
	if (first != NULL) {
		gnm_expr_entry_load_from_range (state->output_entry,
						state->sheet, first);
		int_to_entry (GTK_ENTRY (state->count_entry),
			      first->end.row - first->start.row + 1);
		int_to_entry (GTK_ENTRY (state->vars_entry),
			      first->end.col - first->start.col + 1);
	}

	random_tool_update_sensitivity_cb (NULL, state);

	return FALSE;
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
dialog_random_tool (WorkbookControlGUI *wbcg, Sheet *sheet)
{
        RandomToolState *state;

	if (wbcg == NULL) {
		return 1;
	}


	/* Only pop up one copy per workbook */
	if (gnumeric_dialog_raise_if_exists (wbcg, RANDOM_KEY)) {
		return 0;
	}

	state = g_new (RandomToolState, 1);
	(*(ToolType *)state) = TOOL_RANDOM;
	state->wbcg  = wbcg;
	state->wb   = wb_control_workbook (WORKBOOK_CONTROL (wbcg));
	state->sheet = sheet;
	state->warning_dialog = NULL;
	state->help_link = "random-number-generation-tool.html";
	state->input_var1_str = NULL;
	state->input_var2_str = NULL;

	if (dialog_random_tool_init (state)) {
		gnumeric_notice (wbcg, GTK_MESSAGE_ERROR,
				 _("Could not create the Random Tool dialog."));
		g_free (state);
		return 0;
	}

	gtk_widget_show (state->dialog);

        return 0;
}
/**********************************************/
/*  End of random tool code */
/**********************************************/

