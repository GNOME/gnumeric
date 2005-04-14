#ifndef GNUMERIC_GOAL_SEEK_H
#define GNUMERIC_GOAL_SEEK_H

#include "numbers.h"
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
} GoalSeekData;

typedef enum { GOAL_SEEK_OK, GOAL_SEEK_ERROR } GoalSeekStatus;

typedef GoalSeekStatus (*GoalSeekFunction) (gnm_float x, gnm_float *y, void *user_data);

void goal_seek_initialize (GoalSeekData *data);

GoalSeekStatus goal_seek_point (GoalSeekFunction f,
				GoalSeekData *data,
				void *user_data,
				gnm_float x0);

GoalSeekStatus goal_seek_newton (GoalSeekFunction f,
				 GoalSeekFunction df,
				 GoalSeekData *data,
				 void *user_data,
				 gnm_float x0);

GoalSeekStatus goal_seek_bisection (GoalSeekFunction f,
				    GoalSeekData *data,
				    void *user_data);

GoalSeekStatus goal_seek_trawl_uniformly (GoalSeekFunction f,
					  GoalSeekData *data,
					  void *user_data,
					  gnm_float xmin, gnm_float xmax,
					  int points);

GoalSeekStatus goal_seek_trawl_normally (GoalSeekFunction f,
					 GoalSeekData *data,
					 void *user_data,
					 gnm_float mu, gnm_float sigma,
					 int points);

#endif
