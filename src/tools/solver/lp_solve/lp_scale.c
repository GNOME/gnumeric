
#include "commonlib.h"
#include "lp_lib.h"
#include "lp_scale.h"

#ifdef FORTIFY
# include "lp_fortify.h"
#endif


/*
    Scaling routines for lp_solve v5.0+
   ----------------------------------------------------------------------------------
    Author:        Kjell Eikland
    Contact:       kjell.eikland@broadpark.no
    License terms: LGPL.

    Requires:      lp_lib.h, lp_scale.h

    Release notes:
    v5.0.0  1 January 2004      Significantly expanded and repackaged scaling
                                routines.
    v5.0.1 20 February 2004     Modified rounding behaviour in several areas.

   ----------------------------------------------------------------------------------
*/

/* First define scaling and unscaling primitives */

REAL scaled_value(lprec *lp, REAL value, int index)
{
  if(fabs(value) < lp->infinite) {
    if(lp->scaling_used)
      if(index > lp->rows)
        value /= lp->scalars[index];
      else
        value *= lp->scalars[index];
  }
  else
    value = my_sign(value)*lp->infinite;
  return(value);
}

REAL unscaled_value(lprec *lp, REAL value, int index)
{
  if(fabs(value) < lp->infinite) {
    if(lp->scaling_used)
      if(index > lp->rows)
        value *= lp->scalars[index];
      else
        value /= lp->scalars[index];
  }
  else
    value = my_sign(value)*lp->infinite;
  return(value);
}

STATIC REAL scaled_mat(lprec *lp, REAL value, int row, int col)
{
  if(lp->scaling_used)
    value *= lp->scalars[row] * lp->scalars[lp->rows + col];
  return( value );
}

STATIC REAL unscaled_mat(lprec *lp, REAL value, int row, int col)
{
  if(lp->scaling_used)
    value /= lp->scalars[row] * lp->scalars[lp->rows + col];
  return( value );
}

/* Compute the scale factor by the formulae:
      FALSE: SUM (log |Aij|) ^ 2
      TRUE:  SUM (log |Aij| - RowScale[i] - ColScale[j]) ^ 2 */
REAL CurtisReidMeasure(lprec *lp, MYBOOL _Advanced, REAL *FRowScale, REAL *FColScale)
{
  int  row, col, ent, colMax;
  REAL value, logvalue, Result;

  Result=0;
  colMax = lp->columns;
  for(col=1; col<=colMax; col++) {
    for(ent=lp->matA->col_end[col-1]; ent<lp->matA->col_end[col]; ent++) {
      row=lp->matA->col_mat[ent].row_nr;
      value=fabs(lp->matA->col_mat[ent].value);
      if(value>0) {
        logvalue=log(value);
        if(_Advanced)
          logvalue-= FRowScale[row] + FColScale[col];
        Result+=logvalue*logvalue;
      }
    }
  }
  return(Result);
}

/* Implementation of the Curtis-Reid scaling based on the paper
   "On the Automatic Scaling of Matrices for Gaussian
    Elimination," Journal of the Institute of Mathematics and
    Its Applications (1972) 10, 118-124.

    Solve the system | M   E | (r)   (sigma)
                     |       | ( ) = (     )
                     | E^T N | (c)   ( tau )

    by the conjugate gradient method (clever recurrences).

    E is the matrix A with all elements = 1

    M is diagonal matrix of row    counts (RowCount)
    N is diagonal matrix of column counts (ColCount)

    sigma is the vector of row    logarithm sums (RowSum)
    tau   is the vector of column logarithm sums (ColSum)

    r, c are resulting row and column scalings (RowScale, ColScale) */

int CurtisReidScales(lprec *lp, MYBOOL _Advanced, REAL *FRowScale, REAL *FColScale)
{
  int    rowbase, row, col, ent;
  REAL   *RowScalem2, *ColScalem2,
         *RowSum, *ColSum,
         *residual_even, *residual_odd;
  REAL   sk,   qk,     ek,
         skm1, qkm1,   ekm1,
         qkm2, qkqkm1, ekm2, ekekm1,
         value, logvalue,
         StopTolerance;
  int    *RowCount, *ColCount, colMax;
  int    Result;
  MATrec *mat = lp->matA;

  if(CurtisReidMeasure(lp, _Advanced, FRowScale, FColScale)<0.1*get_nonzeros(lp))
	return(0);

  /* Allocate temporary memory and find RowSum and ColSum measures */

  colMax = lp->columns;

  allocREAL(lp, &RowSum, lp->rows+1, TRUE);
  allocINT(lp, &RowCount, lp->rows+1, TRUE);
  allocREAL(lp, &residual_odd, lp->rows+1, TRUE);

  allocREAL(lp, &ColSum, colMax+1, TRUE);
  allocINT(lp, &ColCount, colMax+1, TRUE);
  allocREAL(lp, &residual_even, colMax+1, TRUE);

  allocREAL(lp, &RowScalem2, lp->rows+1, FALSE);
  allocREAL(lp, &ColScalem2, colMax+1, FALSE);

  /* Set origin for row scaling (1=constraints only, 0=include OF) */
  rowbase = 0;
  for(row=0; row < rowbase; row++)
    FRowScale[row] = 1;

  for(col=1; col<=colMax; col++) {
	  for(ent=mat->col_end[col-1]; ent<mat->col_end[col]; ent++) {
      row=mat->col_mat[ent].row_nr;
	    if(row<rowbase) continue;
      value=fabs(mat->col_mat[ent].value);
      if(value>0) {
        logvalue=log(value);
        ColSum[col]+=logvalue;
        RowSum[row]+=logvalue;
        ColCount[col]++;
        RowCount[row]++;
      }
    }
  }

  /* Make sure we dont't have division by zero errors */
  for(row=rowbase; row<=lp->rows; row++)
    if(RowCount[row]==0)
      RowCount[row]=1;
  for(col=1; col<=colMax; col++)
    if(ColCount[col]==0)
      ColCount[col]=1;

  /* Initialize to RowScale = RowCount-1 RowSum
                   ColScale = 0.0
                   residual = ColSum - ET RowCount-1 RowSum */

  StopTolerance= my_max(lp->scalelimit-floor(lp->scalelimit), DEF_SCALINGEPS);
  StopTolerance*= (REAL) get_nonzeros(lp);
  for(row=rowbase; row<=lp->rows; row++) {
    FRowScale[row]=RowSum[row] / (REAL) RowCount[row];
    RowScalem2[row]=FRowScale[row];
  }

  /* Compute initial residual */
  for(col=1; col<=colMax; col++) {
    FColScale[col]=0;
    ColScalem2[col]=0;
    residual_even[col]=ColSum[col];
	  for(ent=mat->col_end[col-1]; ent<mat->col_end[col]; ent++) {
      row=mat->col_mat[ent].row_nr;
	    if(row<rowbase) continue;
      residual_even[col]-=RowSum[row] / (REAL) RowCount[row];
    }
  }

  /* Compute sk */
  sk=0;
  skm1=0;
  for(col=1; col<=colMax; col++)
    sk+=(residual_even[col]*residual_even[col]) / (REAL) ColCount[col];

  Result=0;
  qk=1; qkm1=0; qkm2=0;
  ek=0; ekm1=0; ekm2=0;

  while(sk>StopTolerance) {
  /* Given the values of residual and sk, construct
     ColScale (when pass is even)
     RowScale (when pass is odd)  */

    qkqkm1 = qk * qkm1;
    ekekm1 = ek * ekm1;
	  if((Result % 2) == 0) { /* pass is even; construct RowScale[pass+1] */
	    if(Result != 0) {
 	      for(row=rowbase; row<=lp->rows; row++)
          RowScalem2[row]=FRowScale[row];
        if(qkqkm1 != 0) {
          for(row=rowbase; row<=lp->rows; row++)
	          FRowScale[row]*=(1 + ekekm1 / qkqkm1);
		      for(row=rowbase; row<=lp->rows; row++)
	          FRowScale[row]+=(residual_odd[row] / (qkqkm1 * (REAL) RowCount[row]) -
                            RowScalem2[row] * ekekm1 / qkqkm1);
        }
      }
    }
    else { /* pass is odd; construct ColScale[pass+1] */
      for(col=1; col<=colMax; col++)
 	      ColScalem2[col]=FColScale[col];
      if(qkqkm1 != 0) {
        for(col=1; col<=colMax; col++)
          FColScale[col]*=(1 + ekekm1 / qkqkm1);
        for(col=1; col<=colMax; col++)
          FColScale[col]+=(residual_even[col] / ((REAL) ColCount[col] * qkqkm1) -
	                        ColScalem2[col] * ekekm1 / qkqkm1);
      }
    }

    /* update residual and sk (pass + 1) */
    if((Result % 2) == 0) { /* even */
       /* residual */
	    for(row=rowbase; row<=lp->rows; row++)
        residual_odd[row]*=ek;
      for(col=1; col<=colMax; col++)
	      for(ent=mat->col_end[col-1]; ent<mat->col_end[col]; ent++) {
	        row=mat->col_mat[ent].row_nr;
    	    if(row<rowbase) continue;
          residual_odd[row]+=(residual_even[col] / (REAL) ColCount[col]);
        }
	    for(row=rowbase; row<=lp->rows; row++)
        residual_odd[row]*=(-1 / qk);

      /* sk */
      skm1=sk;
      sk=0;
	    for(row=rowbase; row<=lp->rows; row++)
	      sk+=(residual_odd[row]*residual_odd[row]) / (REAL) RowCount[row];
    }
	  else { /* odd */
      /* residual */
      for(col=1; col<=colMax; col++)
        residual_even[col]*=ek;
      for(col=1; col<=colMax; col++)
	      for(ent=mat->col_end[col-1]; ent<mat->col_end[col]; ent++) {
	        row=mat->col_mat[ent].row_nr;
 	        if(row<rowbase) continue;
          residual_even[col]+=(residual_odd[row] / (REAL) RowCount[row]);
        }
      for(col=1; col<=colMax; col++)
        residual_even[col]*=(-1 / qk);

      /* sk */
      skm1=sk;
      sk=0;
      for(col=1; col<=colMax; col++)
	      sk+=(residual_even[col]*residual_even[col]) / (REAL) ColCount[col];
    }

    /* Compute ek and qk */
    ekm2=ekm1;
    ekm1=ek;
    ek=qk * sk / skm1;

    qkm2=qkm1;
    qkm1=qk;
    qk=1-ek;

    Result++;
  }

  /* Synchronize the RowScale and ColScale vectors */
  ekekm1 = ek * ekm1;
  if(qkm1 != 0)
  if((Result % 2) == 0) { /* pass is even, compute RowScale */
    for(row=rowbase; row<=lp->rows; row++)
      FRowScale[row]*=(1.0 + ekekm1 / qkm1);
    for(row=rowbase; row<=lp->rows; row++)
      FRowScale[row]+=(residual_odd[row] / (qkm1 * (REAL) RowCount[row]) -
                      RowScalem2[row] * ekekm1 / qkm1);
  }
  else { /* pass is odd, compute ColScale */
    for(col=1; col<=colMax; col++)
      FColScale[col]*=(1 + ekekm1 / qkm1);
    for(col=1; col<=colMax; col++)
      FColScale[col]+=(residual_even[col] / ((REAL) ColCount[col] * qkm1) -
                       ColScalem2[col] * ekekm1 / qkm1);
  }

  /* Do validation, if indicated */
  if(FALSE && mat_validate(mat)){
    double check, error;

    /* CHECK: M RowScale + E ColScale = RowSum */
    error = 0;
    for(row = rowbase; row <= lp->rows; row++) {
      check = (REAL) RowCount[row] * FRowScale[row];
      if(rowbase == 0)
        ent = 0;
      else
        ent = mat->row_end[row-1];
      for(; ent < mat->row_end[row]; ent++) {
        col = ROW_MAT_COL(mat->row_mat[ent]);
        check += FColScale[col];
      }
      check -= RowSum[row];
      error += check*check;
    }

    /* CHECK: E^T RowScale + N ColScale = ColSum */
    error = 0;
    for(col = 1; col <= colMax; col++) {
      check=(REAL) ColCount[col] * FColScale[col];
      for(ent = mat->col_end[col-1]; ent < mat->col_end[col]; ent++) {
        row = mat->col_mat[ent].row_nr;
	    if(row < rowbase) continue;
        check += FRowScale[row];
      }
      check -= ColSum[col];
      error += check*check;
    }
  }

  /* Convert to scaling factors (rounding to nearest power
     of 2 can optionally be done as a separate step later) */
  for(col=1; col<=colMax; col++) {
    value=exp(-FColScale[col]);
    if(value<MIN_SCALAR) value=MIN_SCALAR;
    if(value>MAX_SCALAR) value=MAX_SCALAR;
	  if(!is_int(lp,col) || is_integerscaling(lp))
      FColScale[col]=value;
	  else
      FColScale[col]=1;
  }
  for(row=rowbase; row<=lp->rows; row++) {
    value=exp(-FRowScale[row]);
    if(value<MIN_SCALAR) value=MIN_SCALAR;
    if(value>MAX_SCALAR) value=MAX_SCALAR;
    FRowScale[row]=value;
  }

 /* free temporary memory */
  FREE(RowSum);
  FREE(ColSum);
  FREE(RowCount);
  FREE(ColCount);
  FREE(residual_even);
  FREE(residual_odd);
  FREE(RowScalem2);
  FREE(ColScalem2);

  return(Result);

}

STATIC MYBOOL scaleCR(lprec *lp)
{
  REAL *scalechange;
  int  Result;

  if(!lp->scaling_used) {
    allocREAL(lp, &lp->scalars, lp->sum_alloc + 1, FALSE);
    for(Result = 0; Result <= lp->sum; Result++)
      lp->scalars[Result] = 1;
    lp->scaling_used = TRUE;
  }

  allocREAL(lp, &scalechange, lp->sum + 1, FALSE);

  Result=CurtisReidScales(lp, FALSE, scalechange, &scalechange[lp->rows]);
  if(Result>0) {

    /* Do the scaling*/
    if(scale_updaterows(lp, scalechange, TRUE) ||
       scale_updatecolumns(lp, &scalechange[lp->rows], TRUE))
      lp->scalemode |= SCALE_CURTISREID;

    lp->doInvert = TRUE;
    lp->doRebase = TRUE;
    lp->doRecompute = TRUE;
  }

  FREE(scalechange);

  return((MYBOOL) (Result > 0));
}

STATIC MYBOOL transform_for_scale(lprec *lp, REAL *value)
{
  MYBOOL Accept = TRUE;
  *value = fabs(*value);
#ifdef Paranoia
  if(*value < lp->epsmachine) {
    Accept = FALSE;
    report(lp, SEVERE, "transform_for_scale: A zero-valued entry was passed\n");
  }
  else
#endif
  if(is_scalemode(lp, SCALE_LOGARITHMIC))
    *value = log(*value);
  else if(is_scalemode(lp, SCALE_QUADRATIC))
    (*value) *= (*value);
  return( Accept );
}

STATIC void accumulate_for_scale(lprec *lp, REAL *min, REAL *max, REAL value)
{
  if(transform_for_scale(lp, &value))
    if(is_scaletype(lp, SCALE_MEAN)) {
      *max += value;
      *min += 1;
    }
    else {
      *max = my_max(*max, value);
      *min = my_min(*min, value);
    }
}

STATIC REAL minmax_to_scale(lprec *lp, REAL min, REAL max)
{
  REAL scale;

  /* Initialize according to transformation / weighting model */
  if(is_scalemode(lp, SCALE_LOGARITHMIC))
    scale = 0;
  else
    scale = 1;

  /* Compute base scalar according to chosen scaling type */
  if(is_scaletype(lp, SCALE_MEAN)) {
    if(min > 0)
      scale = max / min;
  }
  else if(is_scaletype(lp, SCALE_RANGE))
    scale = (max + min) / 2;
  else if(is_scaletype(lp, SCALE_GEOMETRIC))
    scale = sqrt(min*max);
  else if(is_scaletype(lp, SCALE_EXTREME))
    scale = max;

  /* Compute final scalar according to transformation / weighting model */
  if(is_scalemode(lp, SCALE_LOGARITHMIC))
    scale = exp(-scale);
  else if(is_scalemode(lp, SCALE_QUADRATIC)) {
    if(scale == 0)
      scale = 1;
    else
      scale = 1 / sqrt(scale);
  }
  else {
    if(scale == 0)
      scale = 1;
    else
      scale = 1 / scale;
  }

  /* Make sure we are within acceptable scaling ranges */
  scale = my_max(scale, MIN_SCALAR);
  scale = my_min(scale, MAX_SCALAR);

  return(scale);
}

STATIC REAL roundPower2(REAL scale)
/* Purpose is to round a number to it nearest power of 2; in a system
   with binary number representation, this avoids rounding errors when
   scale is used to normalize another value */
{
  long int power2;
  MYBOOL   isSmall = FALSE;

  if(scale == 1)
    return( scale );

  /* Obtain the fractional power of 2 */
  if(scale < 2) {
    scale = 2 / scale;
    isSmall = TRUE;
  }
  else
    scale /= 2;
  scale = log(scale)/log(2.0);

  /* Find the desired nearest power of two and compute the associated scalar */
  power2 = (long) ceil(scale-0.5);
  scale = 1 << power2;
  if(isSmall)
    scale = 1.0 / scale;

  return( scale );

}

STATIC MYBOOL scale_updatecolumns(lprec *lp, REAL *scalechange, MYBOOL updateonly)
{
  int i, j;

  /* Verify that the scale change is significant (different from the unit) */
  for(i = lp->columns; i > 0; i--)
    if(fabs(scalechange[i]-1) > lp->epsprimal)
      break;
  if(i <= 0)
    return( FALSE );

 /* Update the pre-existing column scalar */
  if(updateonly)
    for(i = 1, j = lp->rows + 1; j <= lp->sum; i++, j++)
      lp->scalars[j] *= scalechange[i];
  else
    for(i = 1, j = lp->rows + 1; j <= lp->sum; i++, j++)
      lp->scalars[j] = scalechange[i];

  return( TRUE );
}

STATIC MYBOOL scale_updaterows(lprec *lp, REAL *scalechange, MYBOOL updateonly)
{
  int i;

  /* Verify that the scale change is significant (different from the unit) */
  for(i = lp->rows; i >= 0; i--) {
    if(fabs(scalechange[i]-1) > lp->epsprimal)
      break;
  }
  if(i < 0)
    return( FALSE );

 /* Update the pre-existing row scalar */
  if(updateonly)
    for(i = 0; i <= lp->rows; i++)
      lp->scalars[i] *= scalechange[i];
  else
    for(i = 0; i <= lp->rows; i++)
      lp->scalars[i] = scalechange[i];

  return( TRUE );
}

STATIC MYBOOL scale_columns(lprec *lp)
{
  int     i,j, colMax, nz;
  MATitem *matitem;
  REAL    *scalechange = &lp->scalars[lp->rows];

  colMax = lp->columns;

  /* Scale matrix entries (including any Lagrangean constraints) */
  mat_validate(lp->matA);
  nz = get_nonzeros(lp);
  for(i = 0, matitem = lp->matA->col_mat; i < nz; i++, matitem++)
    (*matitem).value *= scalechange[(*matitem).col_nr];

  /* Scale variable bounds as well */
  for(i = 1, j = lp->rows + 1; j <= lp->sum; i++, j++) { /* was <; changed by PN */
    if(lp->orig_lowbo[j] > -lp->infinite)
      lp->orig_lowbo[j] /= scalechange[i];
    if(lp->orig_upbo[j] < lp->infinite)
      lp->orig_upbo[j] /= scalechange[i];
    if(lp->var_is_sc[i] != 0)
      lp->var_is_sc[i] /= scalechange[i];
  }

  lp->columns_scaled = TRUE;
  lp->doInvert = TRUE;
  lp->doRebase = TRUE;
  lp->doRecompute = TRUE;

  return( TRUE );
}

STATIC MYBOOL scale_rows(lprec *lp)
{
  int     i, nz, colMax;
  MATitem *matitem;
  REAL    *scalechange = lp->scalars;

  colMax = lp->columns;

  /* First row-scale the matrix (including the objective function) */
  nz = get_nonzeros(lp);
  for(i = 0, matitem = lp->matA->col_mat; i < nz; i++, matitem++)
    (*matitem).value *= scalechange[(*matitem).row_nr];

  /* ...and scale the rhs and the row bounds (RANGES in MPS!!) */
  for(i = 0; i <= lp->rows; i++) {
    if(fabs(lp->orig_rh[i]) < lp->infinite)
      lp->orig_rh[i] *= scalechange[i];

    if(lp->orig_upbo[i] < lp->infinite)     /* This is the range */
      lp->orig_upbo[i] *= scalechange[i];

    if((lp->orig_lowbo[i] != 0) && (fabs(lp->orig_lowbo[i]) < lp->infinite))
      lp->orig_lowbo[i] *= scalechange[i];
  }

  lp->doInvert = TRUE;
  lp->doRebase = TRUE;
  lp->doRecompute = TRUE;

  return( TRUE );
}

STATIC REAL scale(lprec *lp)
{
  int    i, j, row_nr, row_count, colMax;
  REAL   *row_max, *row_min, *scalechange, absval;
  REAL   col_max, col_min;
  MYBOOL rowscaled, colscaled;
  MATrec *mat = lp->matA;

  colMax = lp->columns;

  if(is_scaletype(lp, SCALE_NONE))
      return(0.0);

  if(!lp->scaling_used) {
    allocREAL(lp, &lp->scalars, lp->sum_alloc + 1, FALSE);
    for(i = 0; i <= lp->sum; i++)
      lp->scalars[i] = 1;
    lp->scaling_used = TRUE;
  }
  allocREAL(lp, &scalechange, lp->sum + 1, FALSE);

 /* Must initialize due to computation of scaling statistic - KE */
  for(i = 0; i <= lp->sum; i++)
    scalechange[i] = 1;

  row_count = lp->rows;
  allocREAL(lp, &row_max, row_count + 1, TRUE);
  allocREAL(lp, &row_min, row_count + 1, FALSE);

  /* Initialise min and max values of rows */
  for(i = 0; i <= row_count; i++) {
    if(is_scaletype(lp, SCALE_MEAN))
      row_min[i] = 0;             /* Carries the count of elements */
    else
      row_min[i] = lp->infinite;  /* Carries the minimum element */
  }

  /* Calculate row scaling data */
  for(j = 1; j <= colMax; j++) {
    for(i = mat->col_end[j - 1]; i < mat->col_end[j]; i++) {
      row_nr = mat->col_mat[i].row_nr;
#ifdef NoRowScaleOF
      if(row_nr == 0)
        continue;
#endif
      absval = mat->col_mat[i].value;
      absval = scaled_mat(lp, absval, row_nr, j);
      accumulate_for_scale(lp, &row_min[row_nr], &row_max[row_nr], absval);
    }
  }

  /* calculate scale factors for rows */
  i = 0;
#ifdef NoRowScaleOF
  i++;
#endif
  for(; i <= lp->rows; i++) {
    scalechange[i] = minmax_to_scale(lp, row_min[i], row_max[i]);
  }

  FREE(row_max);
  FREE(row_min);

  /* Row-scale the matrix (including the objective function and Lagrangean constraints) */
  rowscaled = scale_updaterows(lp, scalechange, TRUE);

  /* Calculate column scales */
  i = 1;
  for(j = 1; j <= colMax; j++) {
    if(is_int(lp,j) && !is_integerscaling(lp)) { /* do not scale integer columns */
      scalechange[lp->rows + j] = 1;
    }
    else {
      col_max = 0;
      if(is_scaletype(lp, SCALE_MEAN))
        col_min = 0;
      else
        col_min = lp->infinite;

      for(i = mat->col_end[j - 1]; i < mat->col_end[j]; i++) {
        absval = mat->col_mat[i].value;
        absval = scaled_mat(lp, absval, mat->col_mat[i].row_nr, j);
        accumulate_for_scale(lp, &col_min, &col_max, absval);
      }
      scalechange[lp->rows + j] = minmax_to_scale(lp, col_min, col_max);
    }
  }

  /* ... and then column-scale the already row-scaled matrix */
  colscaled = scale_updatecolumns(lp, &scalechange[lp->rows], TRUE);

  /* Create a geometric mean-type measure of the extent of scaling performed; */
  /* ideally, upon successive calls to scale() the value should converge to 0 */
  if(rowscaled || colscaled) {
    col_max = 0;
    for(j = 1; j <= colMax; j++)
      col_max += log(scalechange[lp->rows + j]);
    col_max = exp(col_max/lp->columns);

    i = 0;
#ifdef NoRowScaleOF
    i++;
#endif
    col_min = 0;
    for(; i <= lp->rows; i++)
      col_min += log(scalechange[i]);
    col_min = exp(col_min/row_count);
  }
  else {
    col_max = 1;
    col_min = 1;
  }

  FREE(scalechange);

  return(1 - sqrt(col_max*col_min));
}

STATIC MYBOOL finalize_scaling(lprec *lp)
{
  int i;

  /* Check if we should equilibrate */
  if(is_scalemode(lp, SCALE_EQUILIBRATE) && !is_scaletype(lp, SCALE_CURTISREID)) {
    int oldmode;

    oldmode = lp->scalemode;
    lp->scalemode = SCALE_LINEAR + SCALE_EXTREME;
    scale(lp);
    lp->scalemode = oldmode;
  }

  /* Check if we should prevent rounding errors */
  if(is_scalemode(lp, SCALE_POWER2)) {

    for(i = 0; i <= lp->sum; i++)
      lp->scalars[i] = roundPower2(lp->scalars[i]);
  }

  /* Then transfer the scalars to the model's data */
  return( scale_rows(lp) && scale_columns(lp) );

}

STATIC REAL lp_solve_auto_scale(lprec *lp)
{
  REAL scalingmetric = 0;
  int  n = 1;

  if(lp->scaling_used)
    return( scalingmetric);

  if(lp->scalemode != SCALE_NONE) {
    if(is_scaletype(lp, SCALE_CURTISREID)) {
      scalingmetric = scaleCR(lp);
    }
    else {
      REAL scalinglimit, scalingdelta;
      int  count;

      /* Integer value of scalelimit holds the maximum number of iterations; default to 1 */
      count = (int) floor(lp->scalelimit);
      scalinglimit = lp->scalelimit;
      if((count == 0) || (scalinglimit == 0)) {
        if(scalinglimit > 0)
          count = DEF_SCALINGLIMIT;  /* A non-zero convergence has been given, default to max 5 iterations */
        else
          count = 1;
      }
      else
        scalinglimit -= count;

      /* Scale to desired relative convergence or iteration limit */
      n = 0;
      scalingdelta = 1.0;
      scalingmetric = 1.0;
      while((n < count) && (fabs(scalingdelta) > scalinglimit)) {
        n++;
        scalingdelta = scale(lp);
        scalingmetric = scalingmetric*(1+scalingdelta);
      }
      scalingmetric -= 1;
    }
  }

  /* Check if we really have to do scaling */
  if(lp->scaling_used && (fabs(scalingmetric) >= lp->epsprimal))
    /* Ok, do it */
    finalize_scaling(lp);

  else {

    /* Otherwise reset scaling variables */
    if(lp->scalars != NULL) {
      FREE(lp->scalars);
    }
    lp->scaling_used = FALSE;
    lp->columns_scaled = FALSE;
  }

  return(scalingmetric);
}

STATIC void unscale_columns(lprec *lp)
{
  int    i, j, colMax;
  MATrec *mat = lp->matA;

  if(!lp->columns_scaled)
    return;

  colMax = lp->columns;

  /* unscale mat */
  for(j = 1; j <= colMax; j++) {
    for(i = mat->col_end[j - 1]; i < mat->col_end[j]; i++)
      mat->col_mat[i].value = unscaled_mat(lp, mat->col_mat[i].value, mat->col_mat[i].row_nr, j);
  }

  /* unscale bounds as well */
  for(i = lp->rows + 1, j = 1; i <= lp->sum; i++, j++) {
    lp->orig_lowbo[i] = unscaled_value(lp, lp->orig_lowbo[i], i);
    lp->orig_upbo[i]  = unscaled_value(lp, lp->orig_upbo[i], i);
    lp->var_is_sc[j]  = unscaled_value(lp, lp->var_is_sc[j], i);
  }

  for(i = lp->rows + 1; i<= lp->sum; i++)
    lp->scalars[i] = 1;

  lp->columns_scaled = FALSE;

  lp->doInvert = TRUE;
  lp->doRebase = TRUE;
  lp->doRecompute = TRUE;
}

void undoscale(lprec *lp)
{
  int    i, j, colMax;
  MATrec *mat = lp->matA;

  colMax = lp->columns;

  if(lp->scaling_used) {
    /* unscale the matrix */
    for(j = 1; j <= colMax; j++) {
      for(i = mat->col_end[j - 1]; i < mat->col_end[j]; i++)
        mat->col_mat[i].value = unscaled_mat(lp, mat->col_mat[i].value, mat->col_mat[i].row_nr, j);
    }

    /* unscale variable bounds */
    for(i = lp->rows + 1, j = 1; i <= lp->sum; i++, j++) {
      lp->orig_lowbo[i] = unscaled_value(lp, lp->orig_lowbo[i], i);
      lp->orig_upbo[i]  = unscaled_value(lp, lp->orig_upbo[i], i);
      lp->var_is_sc[j]  = unscaled_value(lp, lp->var_is_sc[j], i);
    }

    /* unscale the rhs, upper and lower bounds... */
    for(i = 0; i <= lp->rows; i++) {
      lp->orig_rh[i] = unscaled_value(lp, lp->orig_rh[i], i);
      lp->orig_lowbo[i] = unscaled_value(lp, lp->orig_lowbo[i], i);
      lp->orig_upbo[i] = unscaled_value(lp, lp->orig_upbo[i], i);
    }

    FREE(lp->scalars);
    lp->scaling_used = FALSE;
    lp->columns_scaled = FALSE;

    lp->doInvert = TRUE;
    lp->doRebase = TRUE;
    lp->doRecompute = TRUE;
  }
}

