static const char *field_types = "CNLDMF?BGPYTI";
static const char *field_type_descriptions[] = { /* FIXME: fix array size from field_types*/
  "Character", "Number", "Logical", "Date", "Memo", "Floating point",
  "Character name variable", "Binary", "General", "Picture", "Currency",
  "DateTime", "Integer"
};

typedef struct { /* field format */
	guint8 name[11]; /* name, including terminating '\0' */
	guint8 type; /* type (single ASCII char) */
	guint8 len; /* byte length */
	guint  pos; /* position in record */
} XBfield;

typedef struct { /* database instance */
	FILE     *f; /* file handle */
	guint     records; /* number of records */
	guint     fields; /* number of fields */
	guint     fieldlen; /* bytes per record */
	XBfield **format; /* array of (pointers to) field formats */
	guint     offset; /* start of records in file */
} XBfile;

typedef struct { /* record in a db */
	XBfile *file;
	guint   row; /* record number : 1 thru file->records are valid */
	guint8 *data; /* private: all fields as binary read from file */
} XBrecord;

XBrecord *record_new  (XBfile *file);
gboolean  record_seek (XBrecord *record, int whence, glong row);
void      record_free (XBrecord *record);
guint8   *record_get_field (const XBrecord *record, guint num);

XBfile *xbase_open (const char *filename);
void    xbase_close (XBfile *file);

static gboolean xbase_read_header (XBfile *file);
static XBfield *xbase_read_field (XBfile *file);


