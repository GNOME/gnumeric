/*
 * Data Source abstract class
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 Helix Code, Inc (http://www.helixcode.com)
 */
#include <config.h>
#include "data-source.h"
#include "util.h"

static DataSourceClass *dsc;

static void
data_source_init (GtkObject *object)
{
}

static void
data_source_class_init (GtkObjectClass *object_class)
{
	dsc = gtk_type_class (data_source_get_type ());
}

DEFINE_TYPE(DataSource,"DataSource",gtk_object,data_source);

Value *
data_source_get_value (DataSource *data_source, const char *spec, int pos)
{
	return (*dsc->get_value)(data_source, spec, pos);
}

gboolean
data_source_set_value (DataSource *data_source, const char *spec, int pos, Value *value)
{
	return (*dsc->set_value)(data_source, spec, pos, value);
}

int
data_source_len (DataSource *data_source, const char *spec)
{
	return (*dsc->len) (data_source, spec);
}
