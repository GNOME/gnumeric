/*
 * Series handling.
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 */
#include <config.h>
#include "series.h"

struct _SeriesName {
	Reference *ref;
	char *str;
};

SeriesName *
series_name_new (void)
{
	return g_new0 (SeriesName, 1);
}

static void inline
free_sn (SeriesName *sn)
{
	if (sn->ref){
		reference_destroy (sn->ref);
		sn->ref = NULL;
	}

	if (sn->str){
		g_free (sn->str);
		sn->str = NULL;
	}
}

void
series_name_set_from_ref (SeriesName *sn, Reference *ref)
{
	g_return_if_fail (sn != NULL);
	g_return_if_fail (ref != NULL);

	free_sn (sn);
	sn->ref = reference_duplicate (ref);
}

void
series_name_set_from_string (SeriesName *sn, const char *str)
{
	g_return_if_fail (sn != NULL);
	g_return_if_fail (str != NULL);

	free_sn (sn);
	sn->str = g_strdup (str);
}

void
series_name_destroy (SeriesName *sn)
{
	g_return_if_fail (sn != NULL);
	
	free_sn (sn);
	g_free (sn);
}

char *
series_name_get_string (SeriesName *sn)
{
	Value *val;
	char *ret;
	
	g_return_val_if_fail (sn != NULL, NULL);

	if (sn->str)
		return g_strdup (sn->str);

	val = reference_value (sn->ref);
	ret = value_get_as_string (val);
	value_release (val);

	return ret;
}

struct _Series {
	DataSource *source;
	SeriesName *series_name;
	char       *value_spec;
};

Series *
series_new (DataSource *source, const char *value_spec)
{
	Series *series;
	
	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (name_spec != NULL, NULL);

	series = g_new (Series, 1);

	series->source = source;
	gtk_object_ref (GTK_OBJECT (series->source));

	series->name_spec = NULL
	series->value_spec = g_strdup (value_spec);

	return series
}

void
series_set_name (Series *series, SeriesName *series_name)
{
	g_return_if_fail (series != NULL);
	g_return_if_fail (series_name != NULL);

	if (series->name_spec)
		series_name_destroy (series->name_spec);

	series->name_spec = series_name;
}

void
series_set_source (Series *series, DataSource *source, const char *value_spec)
{
	g_return_val_if_fail (source != NULL, NULL);
	g_return_val_if_fail (value_spec != NULL, NULL);
	g_return_val_if_fail (series != NULL, NULL);

	gtk_object_unref (GTK_OBJECT (series->source));
	series->source = source;
	gtk_object_ref (GTK_OBJECT (series->source));

	g_free (series->value_spec);
	series->value_spec = g_strdup (value_spec);
}

void
series_destroy (Series *series)
{
	if (series->name_spec)
		series_name_destroy (series->name_spec);
	gtk_object_unref (GTK_OBJECT (series->source));
	g_free (series->value_spec);
	g_free (series);
}

SeriesName *
series_get_series_name (Series *series)
{
	g_return_val_if_fail (series != NULL, NULL);

	return series->name_spec;
}

char *
series_get_name (Series *series)
{
	g_return_val_if_fail (series != NULL, NULL);

	return series_name_get_string (series->name_spec);
}

Value *
series_get_value (Series *series, int n)
{
	g_return_val_if_fail (series != NULL);

	return data_source_fetch_value (series->data_source, series->value_spec, n);
}

void
data_set_set_value (Series *series, int n, Value *value)
{
	g_return_val_if_fail (series != NULL);p

	return data_source_set_value (series->data_source, series->value_spec, n, value);
}
