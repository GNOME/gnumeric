#ifndef GNUMERIC_GOAL_SEEK_H
#define GNUMERIC_GOAL_SEEK_H

#include <numbers.h>
#include <glib.h>

typedef struct {
	gnm_float xmin;           /* Minimum allowed value for x.  */
	gnm_float xmax;           /* Maximum allowed value for x.  */
	gnm_float precision;      /* Desired relative precision.  */

	gboolean havexpos;        /* Do we have a valid xpos?  */
	gnm_float xpos;           /* Value for which f(xpos) > 0.  */
	gnm_float ypos;           /* f(xpos).  */

	gboolean havexneg;        /* Do we have a valid xneg?  */
	gnm_float xneg;           /* Value for which f(xneg) < 0.  */
	gnm_float yneg;           /* f(xneg).  */

	gboolean have_root;       /* Do we have a valid root?  */
	gnm_float root;           /* Value for which f(root) == 0.  */
} GnmGoalSeekData;

typedef enum { GOAL_SEEK_OK, GOAL_SEEK_ERROR } GnmGoalSeekStatus;

typedef GnmGoalSeekStatus (*GnmGoalSeekFunction) (gnm_float x, gnm_float *y, void *user_data);

void goal_seek_initialize (GnmGoalSeekData *data);

GnmGoalSeekStatus goal_seek_point (GnmGoalSeekFunction f,
				GnmGoalSeekData *data,
				void *user_data,
				gnm_float x0);

GnmGoalSeekStatus goal_seek_newton (GnmGoalSeekFunction f,
				 GnmGoalSeekFunction df,
				 GnmGoalSeekData *data,
				 void *user_data,
				 gnm_float x0);

GnmGoalSeekStatus goal_seek_bisection (GnmGoalSeekFunction f,
				    GnmGoalSeekData *data,
				    void *user_data);

GnmGoalSeekStatus goal_seek_trawl_uniformly (GnmGoalSeekFunction f,
					  GnmGoalSeekData *data,
					  void *user_data,
					  gnm_float xmin, gnm_float xmax,
					  int points);

GnmGoalSeekStatus goal_seek_trawl_normally (GnmGoalSeekFunction f,
					 GnmGoalSeekData *data,
					 void *user_data,
					 gnm_float mu, gnm_float sigma,
					 int points);

#endif
