/*
 * reference.c: references to an external source
 *
 * Author:
 *   Miguel de Icaza (miguel@kernel.org)
 *
 * (C) 1999 International GNOME Support
 */
#include <config.h>
#include "reference.h"

struct _Reference {
	DataSource *data_source;
	char       *spec;
};

Reference *
reference_new (DataSource *data_source, const char *spec)
{
	Reference *ref;

	g_return_val_if_fail (data_source != NULL, NULL);
	g_return_val_if_fail (spec != NULL, NULL);
	
	ref = g_new (Reference, 1);
	ref->data_source = data_source;
	gtk_object_ref (GTK_OBJECT (ref->data_source));
	ref->spec = g_strdup (spec);

	return ref;
}

Reference *
reference_duplicate (Reference *reference)
{
	g_return_val_if_fail (reference != NULL, NULL);

	return reference_new (reference->data_source, reference->spec);
}

void
reference_destroy (Reference *reference)
{
	g_return_if_fail (reference != NULL);

	gtk_object_unref (GTK_OBJECT (reference->data_source));
	g_free (reference->spec);
	
	g_free (reference);
}

Value *
reference_value (Reference *reference)
{
	g_return_val_if_fail (reference != NULL, NULL);
	
	return data_source_get_value (reference->data_source, reference->spec, 0);
}

