/**
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_MS_OLE_H
#define GNUMERIC_MS_OLE_H

#include <glib.h>

/* Whether to use memory mapped IO */
#define OLE_MMAP  1

/* Block pointer */
typedef guint32 BLP;

/* Forward declarations of types */
typedef struct _MS_OLE           MS_OLE;
typedef struct _MS_OLE_STREAM    MS_OLE_STREAM;
typedef struct _MS_OLE_DIRECTORY MS_OLE_DIRECTORY;

typedef enum { MS_OLE_SEEK_SET, MS_OLE_SEEK_CUR } ms_ole_seek_t;
#ifdef G_HAVE_GINT64
        typedef guint32 ms_ole_pos_t;
#else
        typedef guint32 ms_ole_pos_t;
#endif

typedef guint32 PPS_IDX ;
typedef enum _PPS_TYPE { MS_OLE_PPS_STORAGE = 1,
			 MS_OLE_PPS_STREAM  = 2,
			 MS_OLE_PPS_ROOT    = 5} PPS_TYPE ;

/**
 * Structure describing an OLE file
 **/
struct _MS_OLE
{
	guint8  *mem ;
	guint32 length ;

	/**
	 * To be considered private
	 **/
	char mode;
	int file_descriptor;
	int dirty;
	GArray    *bb;     /* Big  blocks status  */
#ifndef OLE_MMAP
	GPtrArray *bbptr;  /* Pointers to blocks  NULL if not read in */
#endif
	GArray    *sb;     /* Small block status  */
	GArray    *sbf;    /* The small block file */
	guint32    num_pps;/* Count of number of property sets */
	GList     *pps;    /* Property Storage -> struct _PPS, always 1 valid entry or NULL */
};

/* Create new OLE file */
extern MS_OLE           *ms_ole_create  (const char *name) ;
/* Open existing OLE file */
extern MS_OLE           *ms_ole_open    (const char *name) ;
/* Get a root directory handle */
extern MS_OLE_DIRECTORY *ms_ole_get_root (MS_OLE *);
extern void              ms_ole_destroy (MS_OLE *ptr) ;

struct _MS_OLE_DIRECTORY
{
	char        *name;
	ms_ole_pos_t length;
	PPS_TYPE     type;
	GList       *pps;
	int          first;
	/* Private */
	MS_OLE      *file ;
};

extern MS_OLE_DIRECTORY *ms_ole_directory_new (MS_OLE *) ;
extern int  ms_ole_directory_next (MS_OLE_DIRECTORY *) ;
extern void ms_ole_directory_enter (MS_OLE_DIRECTORY *) ;
/* Pointer to the directory in which to create a new stream / storage object */
extern MS_OLE_DIRECTORY *ms_ole_directory_create (MS_OLE_DIRECTORY *d,
						  char *name,
						  PPS_TYPE type) ;
extern void ms_ole_directory_unlink (MS_OLE_DIRECTORY *) ;
extern void ms_ole_directory_destroy (MS_OLE_DIRECTORY *) ;

struct _MS_OLE_STREAM
{
	GArray *blocks;        /* A list of the blocks in the file if NULL: no file */
	ms_ole_pos_t position; /* Current offset into file. Points to the next byte to read */
	ms_ole_pos_t size;
	enum { MS_OLE_SMALL_BLOCK, MS_OLE_LARGE_BLOCK } strtype; /* Type of stream */

	/**
	 * Attempts to copy length bytes into *ptr, returns true if
	 * successful, _does_ advance the stream pointer.
	 **/
	gboolean     (*read_copy )(MS_OLE_STREAM *, guint8 *ptr, guint32 length) ;
	/**
	 * Acertains whether there is a contiguous block length bytes,
	 * if so returns a pointer to it and _does_ advance the stream pointer.
	 * otherwise returns NULL and does _not_ advance the stream pointer.
	 **/
	guint8*      (*read_ptr  )(MS_OLE_STREAM *, guint32 length) ;
	void         (*lseek     )(MS_OLE_STREAM *, gint32 BYTES, ms_ole_seek_t type) ;
	ms_ole_pos_t (*tell      )(MS_OLE_STREAM *);
	/**
	 * This writes length bytes at *ptr to the stream, and advances
	 * the stream pointer.
	 **/
	void         (*write     )(MS_OLE_STREAM *, guint8 *ptr, guint32 length) ;

	/**
	 * PRIVATE
	 **/
	MS_OLE *file ;
	void   *pps ;   /* Straight PPS * */
};

/* Mode = 'r' or 'w' */
extern MS_OLE_STREAM *ms_ole_stream_open (MS_OLE_DIRECTORY *d, char mode) ;
extern MS_OLE_STREAM *ms_ole_stream_copy (MS_OLE_STREAM *);
extern void ms_ole_stream_close  (MS_OLE_STREAM *) ;

extern void dump (guint8 *ptr, guint32 len) ;

extern void ms_ole_debug (MS_OLE *, int magic);
#endif


