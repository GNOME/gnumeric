#ifndef GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H
#define GNUMERIC_PLUGIN_LOTUS_123_LOTUS_H

#include <gnumeric.h>
#include <gsf/gsf.h>

typedef enum {
	LOTUS_VERSION_ORIG_123  = 0x0404,
	LOTUS_VERSION_SYMPHONY  = 0x0405,
	LOTUS_VERSION_SYMPHONY2 = 0x0406,
	LOTUS_VERSION_123V4     = 0x1002,
	LOTUS_VERSION_123V6     = 0x1003,
	LOTUS_VERSION_123V7     = 0x1004,  /* Not sure.  */
	LOTUS_VERSION_123SS98   = 0x1005
} LotusVersion;

typedef struct {
	GsfInput	*input;
	IOContext	*io_context;
	WorkbookView	*wbv;
	Workbook	*wb;
	Sheet		*sheet;
	LotusVersion     version;
	guint8          lmbcs_group;

        GHashTable      *style_pool;
} LotusState;

Sheet	 *lotus_get_sheet  (Workbook *wb, int i);
char	 *lotus_get_lmbcs  (char const *data, int maxlen, int def_group);
GnmValue *lotus_new_string (gchar const *data, int def_group);
gboolean  lotus_read	   (LotusState *state);
GnmValue *lotus_unpack_number (guint32 u);
GnmValue *lotus_smallnum (signed int d);

void lmbcs_init (void);
void lmbcs_shutdown (void);

#endif
