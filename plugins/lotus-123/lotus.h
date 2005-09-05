#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H

#include <gnumeric.h>
#include <gsf/gsf.h>

typedef enum {
	LOTUS_VERSION_ORIG_123 = 0x0404,
	LOTUS_VERSION_SYMPHONY = 0x0405,
	LOTUS_VERSION_123V6    = 0x1003,
	LOTUS_VERSION_123SS98  = 0x1005
} LotusVersion;

typedef struct {
	GsfInput	*input;
	IOContext	*io_context;
	GIConv           converter;
	WorkbookView	*wbv;
	Workbook	*wb;
	Sheet		*sheet;
	LotusVersion     version;
  
} LotusWk1Read;

Sheet *lotus_get_sheet (Workbook *wb, int i);
double lotus_unpack_number (guint32 u);
GnmValue *lotus_new_string (LotusWk1Read *state, gchar const *data);
gboolean  lotus_read   (LotusWk1Read *state);

#endif
