/*
 * fn-financial.c:  Built in financial functions and functions registration
 *
 * Author:
 *  Vladimir Vuksan (vuksan@veus.hr)
 *  

 */
#include <config.h>
#include <gnome.h>
#include "math.h"
#include "gnumeric.h"
#include "gnumeric-sheet.h"
#include "utils.h"
#include "func.h"

double calculate_pvif (double rate, int nper);
double calculate_pvifa (double rate, int nper);
double calculate_fvif (double rate, int nper);
double calculate_fvifa (double rate, int nper);

/* Some forward declarations */

/*

Below are some of the functions that are used quite often in
financial analysis.

Present value interest factor 

	              1
	 PVIF = ( ---------- ) ^ n
	            1 + k

Future value interest factor 

         FVIF = 1 / PVIF

Present value interest factor of annuities

                  1          1
	 PVIFA = --- - -----------
                  k     k*(1+k)^n

Future value interest factor of annuities

                  (1+k)^n - 1
         FVIFA = ----------------
	                k



	 PV * PVIF(k%, nper) + PMT * ( 1 + rate * type ) * FVIFA(k%, nper) + FV = 0

 */

double calculate_pvif (double rate, int nper) 
{

  return ( pow ( 1 + rate, nper) );

}

double calculate_fvif (double rate, int nper) 
{

  return ( 1.0 / calculate_pvif(rate,nper) );

}

double calculate_pvifa (double rate, int nper) 
{

  return ( ( 1.0 / rate ) - ( 1.0 / ( rate * pow(1+rate, nper) ))) ;

}

double calculate_fvifa (double rate, int nper)
{
  return (  (pow(1+rate, nper) - 1) / rate); 
}


static char *help_effect = {
	N_("@FUNCTION=EFFECT\n"
	   "@SYNTAX=EFFECT(b1,b2)\n"
	   "@DESCRIPTION=Calculates the effective interest rate from "
	   "a given nominal rate.\n"
	   "Effective interest rate is calculated using this formulae:\n"
	   "\n"
	   "         r"
           "( 1 + ------ ) ^ nper - 1"
           "       nper"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = nominal interest rate (stated in yearly terms)"
	   "nper = number of periods used for compounding"
	   "\n"
	   "For example credit cards will list an APR (annual percentage rate) which "
	   "is a nominal interest rate."
	   "\n"
	   "For example if you wanted to find out how much you are actually paying interest "
	   "on your credit card that states an APR of 19% that is compounded monthly "
	   "you would type in:"
	   "\n"
	   "=EFFECT(.19,12) and you would get .2075 or 20.75%. That is the effective percentage you will "
	   "pay on your loan."
	   "\n"
	   "@SEEALSO=NOMINAL")
};


static Value *
gnumeric_effect (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate;
	int nper;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) ){
		*error_string = _("effect - domain error");
		return NULL;
	}

        return value_float  ( pow( (1 + rate/nper) , nper) - 1 );

}


static char *help_nominal = {
	N_("@FUNCTION=NOMINAL\n"
	   "@SYNTAX=NOMINAL(b1,b2)\n"
	   "@DESCRIPTION=Calculates the nominal interest rate from "
	   "a given effective rate.\n"
	   "Nominal interest rate is given by a formula:\n"
	   "\n"
           "nper * (( 1 + r ) ^ (1 / nper) - 1 )"
	   "\n"
	   "where:\n"
	   "\n"
	   "r = effective interest rate"
	   "nper = number of periods used for compounding"
	   "\n"
	   "@SEEALSO=EFFECT")
};

static Value *
gnumeric_nominal (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate;
	int nper;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);

	/* Rate or number of periods cannot be negative */
	if ( (rate < 0) || (nper <= 0) ){
		*error_string = _("nominal - domain error");
		return NULL;
	}

        return value_float  ( nper * ( pow( 1 + rate, 1.0/nper ) - 1 ) );

}




static char *help_sln = {
	N_("@FUNCTION=SLN\n"
	   "@SYNTAX=SLN(cost,salvage value,life)\n"

	   "@DESCRIPTION=Calculates the straight line depriciation for an"
	   "asset based on its cost, salvage value and anticipated life."
	   "\n"
	   "Formula for straight line depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) / life"
	   "\n"	
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "@SEEALSO=SYD")
};


static Value *
gnumeric_sln (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double cost,salvage_value,life;

	cost = value_get_as_double (argv [0]);
	salvage_value = value_get_as_int (argv [1]);
	life = value_get_as_double (argv [2]);

	/* Life of an asset cannot be negative */

	if ( life < 0 ){
		*error_string = _("sln - domain error");
		return NULL;
	}

        return value_float  ( (cost - salvage_value) / life ) ;

}

static char *help_syd = {
	N_("@FUNCTION=SYD\n"
	   "@SYNTAX=SYD(cost,salvage value,life,period)\n"

	   "@DESCRIPTION=Calculates the sum-of-years digits depriciation for an"
	   "asset based on its cost, salvage value, anticipated life and a"
	   "particular period."
	   "\n"
	   "Formula for sum-of-years digits depriciation is:"
	   "\n"
	   "Depriciation expense = ( cost - salvage value ) * (life-period+1) * 2 / life * (life + 1)"
	   "\n"
	   "\tcost = cost of an asset when acquired (market value)"
	   "\tsalvage_value = amount you get when asset sold at the end of life"
	   "\tlife = anticipated life of an asset"
	   "\tperiod = period for which we need the expense"
	   "@SEEALSO=SLN")
};

static Value *
gnumeric_syd (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double cost,salvage_value,life,period;

	cost = value_get_as_double (argv [0]);
	salvage_value = value_get_as_int (argv [1]);
	life = value_get_as_double (argv [2]);
	period = value_get_as_double (argv [3]);

        return value_float  ( ( (cost - salvage_value) * (life-period+1) * 2 ) / ( life * (life + 1) )) ;

}



static char *help_pv = {
	N_("@FUNCTION=PV\n"
	   "@SYNTAX=PV(rate,nper,pmt,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=FV")
};


static Value *
gnumeric_pv (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate,pmt,fv;
	int nper,type;
	
	double pvif,fvifa;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);
	pmt = value_get_as_double (argv [2]);
	fv = value_get_as_double (argv [3]);
	type = value_get_as_int (argv [4]);

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif(rate,nper);

	fvifa = calculate_fvifa(rate,nper);

        return value_float  ( ( -fv - pmt * ( 1 + rate * type ) * fvifa ) / pvif );

}


static char *help_fv = {
	N_("@FUNCTION=FV\n"
	   "@SYNTAX=FV(rate,nper,pmt,pv,type)\n"
	   "@DESCRIPTION=Calculates the future value of an investment."
	   "@SEEALSO=PV,PMT,PPMT")
};


static Value *
gnumeric_fv (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate,pv,pmt;
	int nper,type;
	
	double pvif,fvifa;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);
	pmt = value_get_as_double (argv [2]);
	pv = value_get_as_double (argv [3]);
	type = value_get_as_int (argv [4]);

	pvif = calculate_pvif(rate,nper);
	fvifa = calculate_fvifa(rate,nper);

        return value_float  ( -1.0 * ( ( pv * pvif ) + pmt * ( 1 + rate * type ) * fvifa )  );

}



static char *help_pmt = {
	N_("@FUNCTION=PMT\n"
	   "@SYNTAX=PMT(rate,nper,pv,fv,type)\n"
	   "@DESCRIPTION=Calculates the present value of an investment."
	   "@SEEALSO=PPMT,PV,FV")
};

static Value *
gnumeric_pmt (struct FunctionDefinition *i, Value *argv [], char **error_string)
{
	double rate,pv,fv;
	int nper,type;
	
	double pvif,fvifa;

	rate = value_get_as_double (argv [0]);
	nper = value_get_as_int (argv [1]);
	pv = value_get_as_double (argv [2]);
	fv = value_get_as_double (argv [3]);
	type = value_get_as_double (argv [4]);

	/* Calculate the PVIF and FVIFA */

	pvif = calculate_pvif(rate,nper);
	fvifa = calculate_fvifa(rate,nper);

        return value_float  (  ( -pv * pvif  - fv ) / ( ( 1 + rate*type) * fvifa )); 

}

      


FunctionDefinition finance_functions [] = {
	{ "effect", "ff",    "rate,nper",    &help_effect,   NULL, gnumeric_effect},
	{ "nominal", "ff",    "rate,nper",    &help_nominal,   NULL, gnumeric_nominal},
	{ "sln", "fff", "cost,salvagevalue,life", &help_sln, NULL, gnumeric_sln},
	{ "syd", "ffff", "cost,salvagevalue,life,period", &help_syd, NULL, gnumeric_syd},
	{ "pv", "fiffi", "rate,nper,pmt,fv,type", &help_pv, NULL, gnumeric_pv},	
	{ "fv", "fiffi", "rate,nper,pmt,pv,type", &help_fv, NULL, gnumeric_fv},	
	{ "pmt", "ffffi", "rate,nper,pv,fv,type", &help_pmt, NULL, gnumeric_pmt},
 	{ NULL, NULL }

};
