/**
 * ms-ole.h: MS Office OLE support for Gnumeric
 *
 * Author:
 *    Michael Meeks (michael@imaginator.com)
 **/
#ifndef GNUMERIC_MsOle_H
#define GNUMERIC_MsOle_H

#include <glib.h>

/* Whether to use memory mapped IO */
#define OLE_MMAP  1

/* Block pointer */
typedef guint32 BLP;

/* Forward declarations of types */
typedef struct _MsOle           MsOle;
typedef struct _MsOleStream    MsOleStream;
typedef struct _MsOleDirectory MsOleDirectory;

typedef enum { MsOleSeekSet, MsOleSeekCur } MsOleSeek;
#ifdef G_HAVE_GINT64
        typedef guint32 MsOlePos;
#else
        typedef guint32 MsOlePos;
#endif

typedef guint32 PPS_IDX ;
typedef enum _PPSType { MsOlePPSStorage = 1,
			MsOlePPSStream  = 2,
			MsOlePPSRoot    = 5} PPSType ;

/**
 * Structure describing an OLE file
 **/
struct _MsOle
{
	guint8    *mem ;
	guint32    length ;

	/**
	 * To be considered private
	 **/
	char       mode;
	int        file_des;
	int        dirty;
	GArray    *bb;     /* Big  blocks status  */
#if !OLE_MMAP
	GPtrArray *bbattr; /* Pointers to block structures */
#endif
	GArray    *sb;     /* Small block status  */
	GArray    *sbf;    /* The small block file */
	guint32    num_pps;/* Count of number of property sets */
	GList     *pps;    /* Property Storage -> struct _PPS, always 1 valid entry or NULL */
};

/* Create new OLE file */
extern MsOle           *ms_ole_create  (const char *name) ;
/* Open existing OLE file */
extern MsOle           *ms_ole_open    (const char *name) ;
/* Get a root directory handle */
extern MsOleDirectory  *ms_ole_get_root (MsOle *);
extern void             ms_ole_destroy (MsOle *ptr) ;

struct _MsOleDirectory
{
	char     *name;
	MsOlePos  length;
	PPSType   type;
	GList    *pps;
	int       first;
	/* Private */
	MsOle    *file ;
};

extern MsOleDirectory *ms_ole_directory_new     (MsOle *) ;
extern int             ms_ole_directory_next    (MsOleDirectory *) ;
extern void            ms_ole_directory_enter   (MsOleDirectory *) ;
/* Pointer to the directory in which to create a new stream / storage object */
extern MsOleDirectory *ms_ole_directory_create  (MsOleDirectory *d,
						 char *name,
						 PPSType type) ;
extern MsOleDirectory *ms_ole_directory_copy    (const MsOleDirectory *);
extern void            ms_ole_directory_unlink  (MsOleDirectory *) ;
extern void            ms_ole_directory_destroy (MsOleDirectory *) ;

struct _MsOleStream
{
	GArray    *blocks;   /* A list of the blocks in the file if NULL: no file */
	MsOlePos   position; /* Current offset into file. Points to the next byte to read */
	MsOlePos   size;
	enum { MsOleSmallBlock, MsOleLargeBlock } strtype; /* Type of stream */

	/**
	 * Attempts to copy length bytes into *ptr, returns true if
	 * successful, _does_ advance the stream pointer.
	 **/
	gboolean (*read_copy )(MsOleStream *, guint8 *ptr, guint32 length) ;
	/**
	 * Acertains whether there is a contiguous block length bytes,
	 * if so returns a pointer to it and _does_ advance the stream pointer.
	 * otherwise returns NULL and does _not_ advance the stream pointer.
	 **/
	guint8*  (*read_ptr  )(MsOleStream *, guint32 length) ;
	void     (*lseek     )(MsOleStream *, gint32 bytes, MsOleSeek type) ;
	MsOlePos (*tell      )(MsOleStream *);
	/**
	 * This writes length bytes at *ptr to the stream, and advances
	 * the stream pointer.
	 **/
	void     (*write     )(MsOleStream *, guint8 *ptr, guint32 length) ;

	/**
	 * PRIVATE
	 **/
	MsOle     *file ;
	void      *pps ;   /* Straight PPS * */
};

/* Mode = 'r' or 'w' */
extern MsOleStream *ms_ole_stream_open      (MsOleDirectory *d, char mode) ;
extern MsOleStream *ms_ole_stream_open_name (MsOleDirectory *d, char *name, char mode) ;
extern MsOleStream *ms_ole_stream_copy      (MsOleStream *);
extern void ms_ole_stream_close             (MsOleStream *) ;

extern void dump (guint8 const *ptr, guint32 len) ;

extern void ms_ole_debug (MsOle *, int magic);
#endif


