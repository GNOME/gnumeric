#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H

#include <gnumeric.h>
#include <gsf/gsf.h>

typedef struct {
	GsfInput	*input;
	IOContext	*io_context;
	GIConv           converter;
	WorkbookView	*wbv;
	Workbook	*wb;
	Sheet		*sheet;
} LotusWk1Read;

GnmValue *lotus_new_string (LotusWk1Read *state, gchar const *data);
gboolean  lotus_read   (LotusWk1Read *state);

#endif
