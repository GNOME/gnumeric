#ifndef GNUMERIC_PLUGIN_XBASE_XBASE_H
#define GNUMERIC_PLUGIN_XBASE_XBASE_H

#include <gnumeric.h>

typedef struct { /* field format */
	gchar name[11]; /* name, including terminating '\0' */
	guint8 type; /* type (single ASCII char) */
	guint8 len; /* byte length */
	guint  pos; /* position in record */
	GOFormat *fmt;
} XBfield;

typedef struct { /* database instance */
	GsfInput *input;	/* file handle */
	guint     records; /* number of records */
	guint     fields; /* number of fields */
	guint     fieldlen; /* bytes per record */
	guint     headerlen; /* bytes per record */
	XBfield **format; /* array of (pointers to) field formats */
	gsf_off_t offset; /* start of records in file */
	GIConv	  char_map;
} XBfile;

typedef struct { /* record in a db */
	XBfile *file;
	gsf_off_t row; /* record number : 1 thru file->records are valid */
	guint8 *data; /* private: all fields as binary read from file */
} XBrecord;

XBrecord *record_new  (XBfile *file);
gboolean  record_seek (XBrecord *record, int whence, gsf_off_t row);
void      record_free (XBrecord *record);
gchar	*record_get_field (XBrecord const *record, guint num);
gboolean  record_deleted (XBrecord *record);

XBfile *xbase_open (GsfInput *input, GOErrorInfo **ret_error);
void    xbase_close (XBfile *file);

#endif
