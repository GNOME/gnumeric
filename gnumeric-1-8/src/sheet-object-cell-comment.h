/* vim: set sw=8: -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
#ifndef _GNM_SHEET_OBJECT_CELL_COMMENT_H_
# define _GNM_SHEET_OBJECT_CELL_COMMENT_H_

#include "sheet-object.h"

G_BEGIN_DECLS

#define CELL_COMMENT_TYPE     (cell_comment_get_type ())
#define CELL_COMMENT(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), CELL_COMMENT_TYPE, GnmComment))
#define CELL_COMMENT_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), CELL_COMMENT_TYPE, CellCommentClass))
#define IS_CELL_COMMENT(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), CELL_COMMENT_TYPE))

GType	     cell_comment_get_type (void);

char const  *cell_comment_author_get (GnmComment const *cc);
void         cell_comment_author_set (GnmComment       *cc, char const *author);
char const  *cell_comment_text_get   (GnmComment const *cc);
void         cell_comment_text_set   (GnmComment       *cc, char const *text);

/* convenience routine */
void	     cell_comment_set_pos   (GnmComment *cc, GnmCellPos const *pos);
GnmComment  *cell_set_comment	    (Sheet *sheet, GnmCellPos const *pos,
				     char const *author, char const *text);

G_END_DECLS

#endif /* _GNM_SHEET_OBJECT_CELL_COMMENT_H_ */
