/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
** analysis-histogram.c
** 
** Made by (Solarion)
** Login   <gnumeric-hacker@digitasaru.net>
** 
** Started on  Mon Dec  4 20:37:18 2006 Johnny Q. Hacker
** Last update Wed Dec  6 20:10:55 2006 Johnny Q. Hacker
*
 * (C) Copyright 2000, 2001 by Jukka-Pekka Iivonen <jiivonen@hutcs.cs.hut.fi>
 * (C) Copyright 2002, 2004 by Andreas J. Guelzow  <aguelzow@taliesin.ca>
 * (C) Copyright 2006 by Solarion <gnumeric-hacker@digitasaru.net>
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
 *
 *Original version imported wholesale from analysis-tools.c
 */

#include <gnumeric-config.h>
#include <glib/gi18n-lib.h>
#include "gnumeric.h"
#include "mathfunc.h"
#include "func.h"
#include "expr.h"
#include "position.h"
#include "complex.h"
#include "rangefunc.h"
#include "tools.h"
#include "value.h"
#include "cell.h"
#include "sheet.h"
#include "ranges.h"
#include "style.h"
#include "regression.h"
#include "sheet-style.h"
#include "workbook.h"
#include "gnm-format.h"
#include "sheet-object-cell-comment.h"
#include "workbook-control.h"
#include "command-context.h"
#include "analysis-tools.h"
#include "analysis-histogram.h"
#include <goffice/utils/go-glib-extras.h>

#include <goffice/utils/go-glib-extras.h>

#include <string.h>
#include <stdlib.h>
#include <math.h>

/************* Histogram Tool *********************************************
 *
 * The results are given in a table which can be printed out in a new
 * sheet, in a new workbook, or simply into an existing sheet.
 *
 **/

typedef struct {
	gnm_float limit;
	GArray     *counts;
	char       *label;
	gboolean   strict;
	gboolean   first;
	gboolean   last;
	gboolean   destroy_label;
} bin_t;

static gint
bin_compare (bin_t const *set_a, bin_t const *set_b)
{
	gnm_float a, b;

	a = set_a->limit;
	b = set_b->limit;

        if (a < b)
                return -1;
        else if (a == b)
                return 0;
        else
                return 1;
}

static gint
bin_pareto_at_i (bin_t const *set_a, bin_t const *set_b, guint index)
{
	gnm_float a, b;

	if (set_a->counts->len <= index)
		return 0;

	a = g_array_index (set_a->counts, gnm_float, index);
	b = g_array_index (set_b->counts, gnm_float, index);

        if (a > b)
                return -1;
        else if (a == b)
                return bin_pareto_at_i (set_a, set_b, index + 1);
        else
                return 1;
}

static gint
bin_pareto (bin_t const *set_a, bin_t const *set_b)
{
	return bin_pareto_at_i (set_a, set_b, 0);
}

static void
destroy_items (gpointer data)
{
	if (((bin_t*)data)->label != NULL && ((bin_t*)data)->destroy_label)
		g_free (((bin_t*)data)->label);
	g_free (data);
}

static gboolean
analysis_tool_histogram_engine_run (data_analysis_output_t *dao,
				    analysis_tools_data_histogram_t *info,
				    GPtrArray **bin_data)
{
	GPtrArray *data = NULL;
	GSList *bin_list = NULL;
	bin_t  *a_bin;
	guint  i, j, row, col;
	GSList * this;
	gnm_float *this_value;

	data = new_data_set_list (info->input, info->group_by,
				  TRUE, info->labels, dao->sheet);

/* set up counter structure */
	if (info->bin != NULL) {
		for (i=0; i < (*bin_data)->len; i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = g_array_index (
				((data_set_t *)g_ptr_array_index ((*bin_data), i))->data,
				gnm_float, 0);
			a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnm_float));
			a_bin->counts = g_array_set_size (a_bin->counts, data->len);
			a_bin->label = ((data_set_t *)g_ptr_array_index ((*bin_data), i))->label;
			a_bin->destroy_label = FALSE;
			a_bin->last = FALSE;
			a_bin->first = FALSE;
			a_bin->strict = FALSE;
			bin_list = g_slist_prepend (bin_list, a_bin);
		}
		bin_list = g_slist_sort (bin_list,
					 (GCompareFunc) bin_compare);
	} else {
		gnm_float skip;
		gboolean value_set;
		char        *text;
		GnmValue       *val;

		if (!info->max_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnm_float a_max;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == gnm_range_max ((gnm_float *)the_data->data, the_data->len,
						    &a_max)) {
					if (value_set) {
						if (a_max > info->max)
							info->max = a_max;
					} else {
						info->max = a_max;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				info->max = 0.0;
		}
		if (!info->min_given) {
			value_set = FALSE;
			for (i = 0; i < data->len; i++) {
				GArray * the_data;
				gnm_float a_min;
				the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
				if (0 == gnm_range_min ((gnm_float *)the_data->data, the_data->len,
						    &a_min)) {
					if (value_set) {
						if (a_min < info->min)
							info->min = a_min;
					} else {
						info->min = a_min;
						value_set = TRUE;
					}
				}
			}
			if (!value_set)
				info->min = 0.0;
		}

		skip = (info->max - info->min) / info->n;
		for (i = 0; (int)i < info->n;  i++) {
			a_bin = g_new (bin_t, 1);
			a_bin->limit = info->max - i * skip;
			a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnm_float));
			a_bin->counts = g_array_set_size (a_bin->counts, data->len);
			a_bin->label = NULL;
			a_bin->destroy_label = FALSE;
			a_bin->last = FALSE;
			a_bin->first = FALSE;
			a_bin->strict = FALSE;
			bin_list = g_slist_prepend (bin_list, a_bin);
		}
		a_bin = g_new (bin_t, 1);
		a_bin->limit = info->min;
		a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnm_float));
		a_bin->counts = g_array_set_size (a_bin->counts, data->len);
		val = value_new_float(info->min);
		text = format_value (NULL, val, NULL, 10,
			workbook_date_conv (dao->sheet->workbook));
		if (text) {
			a_bin->label = g_strdup_printf (_("<%s"), text);
			a_bin->destroy_label = TRUE;
			g_free (text);
		} else {
			a_bin->label = _("Too Small");
			a_bin->destroy_label = FALSE;
		}
		if (val)
			value_release (val);
		a_bin->last = FALSE;
		a_bin->first = TRUE;
		a_bin->strict = TRUE;
		bin_list = g_slist_prepend (bin_list, a_bin);
		info->bin_labels = FALSE;
	}
	a_bin = g_new (bin_t, 1);
	a_bin->limit = 0.0;
	a_bin->counts = g_array_new (FALSE, TRUE, sizeof (gnm_float));
	a_bin->counts = g_array_set_size (a_bin->counts, data->len);
	a_bin->destroy_label = FALSE;
	if (info->bin != NULL) {
		a_bin->label = _("More");
	} else {
		char        *text;
		GnmValue       *val;

		val = value_new_float(info->max);
		text = format_value (NULL, val, NULL, 10,
			workbook_date_conv (dao->sheet->workbook));
		if (text) {
			a_bin->label = g_strdup_printf (_(">%s"), text);
			a_bin->destroy_label = TRUE;
			g_free (text);
		} else
			a_bin->label = _("Too Large");
		if (val)
			value_release (val);
	}
	a_bin->last = TRUE;
	a_bin->first = FALSE;
	a_bin->strict = FALSE;
	bin_list = g_slist_append (bin_list, a_bin);

/* count data */
	for (i = 0; i < data->len; i++) {
		GArray * the_data;
		gnm_float *the_sorted_data;

		the_data = ((data_set_t *)(g_ptr_array_index (data, i)))->data;
		the_sorted_data =  range_sort ((gnm_float *)(the_data->data), the_data->len);

		this = bin_list;
		this_value = the_sorted_data;

		for (j = 0; j < the_data->len;) {
			if ((*this_value < ((bin_t *)this->data)->limit) ||
			    (*this_value == ((bin_t *)this->data)->limit &&
			     !((bin_t *)this->data)->strict) ||
			    (this->next == NULL)){
				(g_array_index (((bin_t *)this->data)->counts,
						gnm_float, i))++;
				j++;
				this_value++;
			} else {
				this = this->next;
			}
		}
		g_free (the_sorted_data);
	}

/* sort if pareto */
	if (info->pareto && (data->len > 0))
		bin_list = g_slist_sort (bin_list,
					 (GCompareFunc) bin_pareto);

/* print labels */
	row = info->labels ? 1 : 0;
	if (!info->bin_labels)
		dao_set_cell (dao, 0, row, _("Bin"));

	this = bin_list;
	while (this != NULL) {
		row++;
		if (info->bin_labels || ((bin_t *)this->data)->last || ((bin_t *)this->data)->first) {
			dao_set_cell (dao, 0, row, ((bin_t *)this->data)->label);
		} else {
			dao_set_cell_float (dao, 0, row, ((bin_t *)this->data)->limit);
		}
		this = this->next;
	}
	dao_set_italic (dao, 0, 0, 0, row);

	col = 1;
	for (i = 0; i < data->len; i++) {
		guint l_col = col;
		gnm_float y = 0.0;
		row = 0;

		if (info->labels) {
			dao_set_cell (dao, col, row,
				  ((data_set_t *)g_ptr_array_index (data, i))->label);
			row++;
		}
		dao_set_cell (dao, col, row, _("Frequency"));
		if (info->percentage)
			dao_set_cell (dao, ++l_col, row, _("%"));
		if (info->cumulative)
			/* xgettext:no-c-format */
			dao_set_cell (dao, ++l_col, row, _("Cumulative %"));
/* print data */
		this = bin_list;
		while (this != NULL) {
			gnm_float x;

			l_col = col;
			x = g_array_index (((bin_t *)this->data)->counts, gnm_float, i);
			row ++;
			dao_set_cell_float (dao, col, row,  x);
			x /= ((data_set_t *)(g_ptr_array_index (data, i)))->data->len;
			if (info->percentage) {
				l_col++;
				dao_set_percent (dao, l_col, row, l_col, row);
				dao_set_cell_float (dao, l_col, row, x);
			}
			if (info->cumulative) {
				y += x;
				l_col++;
				dao_set_percent (dao, l_col, row, l_col, row);
				dao_set_cell_float (dao, l_col, row, y);
			}
			this = this->next;
		}
		col++;
		if (info->percentage)
			col++;
		if (info->cumulative)
			col++;
	}
	dao_set_italic (dao, 0, 0,  col - 1, info->labels ? 1 : 0);

/* finish up */
	destroy_data_set_list (data);
	go_slist_free_custom (bin_list, destroy_items);

	if (*bin_data) {
		destroy_data_set_list (*bin_data);
		*bin_data = NULL;
	}

	if (info->chart)
		g_warning ("TODO : tie this into the graph generator");
	return FALSE;
}

static gboolean
analysis_tool_histogram_engine_check_bins (data_analysis_output_t *dao,
					   analysis_tools_data_histogram_t *info,
					   GPtrArray **bin_data_cont)
{
	GPtrArray *bin_data = NULL;
	guint  i;

	if (info->bin == NULL)
		return FALSE;

	bin_data = new_data_set_list (info->bin, GROUPED_BY_BIN,
				      TRUE, info->bin_labels, dao->sheet);
	for (i = 0; i < bin_data->len; i++) {
		if (((data_set_t *)g_ptr_array_index (bin_data, i))->data->len != 1) {
			destroy_data_set_list (bin_data);
			return TRUE;
		}
	}
	*bin_data_cont = bin_data;

	return FALSE;
}

gboolean
analysis_tool_histogram_engine (data_analysis_output_t *dao, gpointer specs,
			      analysis_tool_engine_t selector, gpointer result)
{
	analysis_tools_data_histogram_t *info = specs;

	switch (selector) {
	case TOOL_ENGINE_UPDATE_DESCRIPTOR:
		return (dao_command_descriptor (dao, _("Histogram (%s)"), result)
			== NULL);
	case TOOL_ENGINE_UPDATE_DAO:
		prepare_input_range (&info->input, info->group_by);
		if (info->bin)
			prepare_input_range (&info->bin, GROUPED_BY_ROW);
		dao_adjust (dao,
			    1 + (1 + (info->cumulative ? 1 : 0) +
				 (info->percentage ? 1 : 0)) * g_slist_length (info->input),
			    2 + (info->bin ? (int)g_slist_length (info->bin) : info->n) +
			    (info->labels ? 1 : 0));
		return FALSE;
	case TOOL_ENGINE_CLEAN_UP:
		range_list_destroy (info->input);
		range_list_destroy (info->bin);
		return FALSE;
	case TOOL_ENGINE_LAST_VALIDITY_CHECK:
		return analysis_tool_histogram_engine_check_bins (dao, specs, result);
	case TOOL_ENGINE_PREPARE_OUTPUT_RANGE:
		dao_prepare_output (NULL, dao, _("Histogram"));
		return FALSE;
	case TOOL_ENGINE_FORMAT_OUTPUT_RANGE:
		return dao_format_output (dao, _("Histogram"));
	case TOOL_ENGINE_PERFORM_CALC:
	default:
		return analysis_tool_histogram_engine_run (dao, specs, result);
	}
	return TRUE;  /* We shouldn't get here */
}
