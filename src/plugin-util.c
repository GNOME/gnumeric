/*
 * plugin-util.c: Utility functions for gnumeric plugins
 *
 * Author:
 *  Almer. S. Tigelaar. <almer1@dds.nl>
 *
 */
 
#include <unistd.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "command-context.h"

#include "plugin-util.h"

/**
 * gnumeric_fopen:
 * @context: a gnumeric command context
 * @path: the file to open
 * @mode: the file mode
 * 
 * a wrapper around fopen (). It will handle
 * error reporting for you.
 * for more info on the parameters see 'man 3 fopen'
 * 
 * Return value: a pointer to a FILE struct if successful or NULL if not
 **/
FILE *
gnumeric_fopen (CommandContext *context, const char *path, const char *mode)
{
	FILE *f;

	g_return_val_if_fail (context != NULL, NULL);
	g_return_val_if_fail (path != NULL, NULL);
	g_return_val_if_fail (mode != NULL, NULL);
	
	f = fopen (path, mode);

	if (!f) {
	
		/* Always report as read error
		 */
	        gnumeric_error_read (context, g_strerror (errno));
		return NULL;
	}

	return f;
}

/**
 * gnumeric_open:
 * @context: a gnumeric command context
 * @pathname: the path to the file
 * @flags: the flags
 * 
 * wrapper around open (), will handle error
 * reporting to the command context.
 * for more info on parameters see 'man 2 open'
 * 
 * Return value: a file descriptor on success or -1 on error
 **/
int
gnumeric_open (CommandContext *context, const char *pathname, int flags)
{
	int fd;

	g_return_val_if_fail (context != NULL, -1);
	g_return_val_if_fail (pathname != NULL, -1);
	
	fd = open(pathname, flags);
	
	if (fd < 0) {

		/* Report every error as a read error if O_RDONLY or O_RDWR
		 * has been set. if O_WRONLY report as write error
		 */
		if (flags & O_WRONLY)
			gnumeric_error_save (context, g_strerror (errno));
		else
			gnumeric_error_read (context, g_strerror (errno));
	  
		return -1;
	}

	return fd;
}

/**
 * gnumeric_mmap_close:
 * @context: a gnumeric command context
 * @data: a pointer to the memory mapped data
 * @fdesc: the file descriptor associated with @data
 * @file_size: the size of the @data
 * 
 * first unmaps and the closes a file.
 * it will report error back to the command context.
 * useful after a call to gnumeric_mmap_open.
 **/
void
gnumeric_mmap_close (CommandContext *context, const unsigned char *data, int fdesc, int file_size)
{
	g_return_if_fail (context != NULL);
	g_return_if_fail (data != NULL);

	if (munmap ((char *)data, file_size) == -1) {
	     char *message;

	     message = g_strdup_printf (_("Unable to unmap the file, error : %s"), g_strerror (errno));
	     gnumeric_error_read (context, message);

	     g_free (message);
	}

	if (close (fdesc) < 0) {
	     char *message;

	     message = g_strdup_printf (_("Error while closing file, error : %s"), g_strerror (errno));
	     gnumeric_error_read (context, message);

	     g_free (message);
	}
}

/**
 * gnumeric_mmap_open:
 * @context: a gnumeric command context
 * @filename: the name of the file to mmap
 * @fdesc: the file descriptor will be RETURNED here
 * @file_size: the file size will be RETURNED here
 * 
 * Opens and mmaps a file into memory. Will report
 * errors back to the command context.
 * you need @fdesc and @file_size later if you want to close
 * and unmap the file again
 *
 * NOTE : don't rely on the values of @fdesc and @file_size
 *        if the function fails (returns NULL).
 * 
 * Return value: a pointer to the mmaped data or NULL on failure.
 **/
const unsigned char *
gnumeric_mmap_open (CommandContext *context, const char *filename, int *fdesc, int *file_size)
{
	caddr_t m;
	struct stat sbuf;
	int fd, len;

	g_return_val_if_fail (context != NULL, NULL);
	g_return_val_if_fail (filename != NULL, NULL);
	g_return_val_if_fail (fdesc != NULL, NULL);
	g_return_val_if_fail (file_size != NULL, NULL);
	
	fd = gnumeric_open (context, filename, O_RDONLY);
	
	if (fd < 0)
		return NULL;

	if (fstat(fd, &sbuf) < 0) {
	
		close (fd);
		gnumeric_error_read (context, g_strerror (errno));
		return NULL;
	}

	len = sbuf.st_size;
	m = mmap (0, len, PROT_READ, MAP_PRIVATE, fd, 0);
	
	if (m == (caddr_t) -1) {
		char *message;

		close (fd);
		message = g_strdup_printf (_("Unable to mmap the file, error : %s"), g_strerror (errno));
		gnumeric_error_read (context, message);
		
		g_free (message);
		return NULL;
	}

	*fdesc = fd;
	*file_size = len;
	
	return (unsigned char *) m;
}



