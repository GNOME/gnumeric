#ifndef __SC_FIN_H__
#define __SC_FIN_H__

GnmValue *get_amordegrc (gnm_float fCost, GDate *nDate, GDate *nFirstPer,
			 gnm_float fRestVal, gint nPer, gnm_float fRate,
			 gint nBase);
GnmValue *get_amorlinc  (gnm_float fCost, GDate *nDate, GDate *nFirstPer,
			 gnm_float fRestVal, gint nPer, gnm_float fRate,
			 gint nBase);
GnmValue *get_yieldmat  (GDate *nSettle, GDate *nMat, GDate *nIssue,
			 gnm_float fRate, gnm_float fPrice, gint nBase);
GnmValue *get_duration  (GDate *nSettle, GDate *nMat, gnm_float fCoup,
			 gnm_float fYield, gint nFreq, gint nBase,
			 gnm_float fNumOfCoups);
GnmValue *get_mduration (GDate *nSettle, GDate *nMat, gnm_float fCoup,
			 gnm_float fYield, gint nFreq, gint nBase,
			 gnm_float fNumOfCoups);
GnmValue *get_cumprinc  (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
			 gint nStartPer, gint nEndPer, gint nPayType);
GnmValue *get_cumipmt   (gnm_float fRate, gint nNumPeriods, gnm_float fVal,
			 gint nStartPer, gint nEndPer, gint nPayType);
GnmValue *get_vdb       (gnm_float cost, gnm_float salvage, gnm_float life,
			 gnm_float start_period, gnm_float end_period,
			 gnm_float factor, gboolean flag);

#endif

