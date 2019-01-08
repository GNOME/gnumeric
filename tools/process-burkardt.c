/*
 * Include this file of test values from
 *     http://people.sc.fsu.edu/~jburkardt/c_src/test_values/test_values.c
 *
 * Omega(0) is overridden to be 0.  This is a matter of definition.
 */
#include "test_values.c"

#include <string.h>

#define FMT "%.17g"

static int row;

typedef enum {
	GT_D_D,
	GT_DD_D,
	GT_DDD_D,
	GT_DI_D,
	GT_I_D,
	GT_ID_D,
	GT_IDI_D,
	GT_IID_D,
	GT_IIII_D,
	GT_I_I,
	GT_II_I,
	GT_III_I
} GenType;

static void
test_func (const char *func_name,
	   void *generator_,
	   GenType gentype,
	   const char *order)
{
	double xd[4], yd;
	int xi[4], yi;
	void (*generator)() = generator_;
	int n_data = 0;
	int first = 1;
	int a, n_args;
	const char *types;
	int special_args = order && strchr (order, '%') != NULL;

	while (1) {
		int r = row + 1;
		char restype;

		switch (gentype) {
		case GT_D_D:
			generator (&n_data, &xd[0], &yd);
			types = "D:D";
			break;
		case GT_DD_D:
			generator (&n_data, &xd[0], &xd[1], &yd);
			types = "DD:D";
			break;
		case GT_DDD_D:
			generator (&n_data, &xd[0], &xd[1], &xd[2], &yd);
			types = "DDD:D";
			break;
		case GT_DI_D:
			generator (&n_data, &xd[0], &xi[1], &yd);
			types = "DI:D";
			break;
		case GT_I_D:
			generator (&n_data, &xi[0], &yd);
			types = "I:D";
			break;
		case GT_ID_D:
			generator (&n_data, &xi[0], &xd[1], &yd);
			types = "ID:D";
			break;
		case GT_IDI_D:
			generator (&n_data, &xi[0], &xd[1], &xi[2], &yd);
			types = "IDI:D";
			break;
		case GT_IID_D:
			generator (&n_data, &xi[0], &xi[1], &xd[2], &yd);
			types = "IID:D";
			break;
		case GT_IIII_D:
			generator (&n_data, &xi[0], &xi[1], &xi[2], &xi[3], &yd);
			types = "IIII:D";
			break;
		case GT_I_I:
			generator (&n_data, &xi[0], &yi);
			types = "I:I";
			break;
		case GT_II_I:
			generator (&n_data, &xi[0], &xi[1], &yi);
			types = "II:I";
			break;
		case GT_III_I:
			generator (&n_data, &xi[0], &xi[1], &xi[2], &yi);
			types = "III:I";
			break;
		default:
			abort ();
		}

		if (n_data == 0)
			break;

		if (first)
			printf ("%s,", func_name);
		else
			printf ("\"\",");
		first = 0;

		printf ("\"=%s(", func_name);
		n_args = strlen (types) - 2;
		if (special_args) {
			char *argstr = strdup (order);
			const char *pct;
			while ((pct = strchr (argstr, '%'))) {
				size_t prelen = pct - argstr;
				int o = pct[1] - '1';
				char a1[4 * sizeof (int) + 100];
				char *newargstr;

				switch (types[o]) {
				case 'D':
					sprintf (a1, FMT, xd[o]);
					break;
				case 'I':
					sprintf (a1, "%d", xi[o]);
					break;
				}

				newargstr = malloc (strlen (argstr) + strlen (a1));
				memcpy (newargstr, argstr, prelen);
				strcpy (newargstr + prelen, a1);
				strcat (newargstr + prelen, pct + 2);
				free (argstr);
				argstr = newargstr;
			}
			printf ("%s", argstr);
			free (argstr);
		} else {
			for (a = 0; a < n_args; a++) {
				int o = order ? order[a] - '1' : a;
				if (a)
					printf (",");
				switch (types[o]) {
				case 'D':
					printf (FMT, xd[o]);
					break;
				case 'I':
					printf ("%d", xi[o]);
					break;
				}
			}
		}
		printf (")\",");

		restype = types[n_args + 1];
		switch (restype) {
		case 'D':
			printf (FMT ",", yd);
			break;
		case 'I':
			printf ("%d,", yi);
			break;
		}

		switch (restype) {
		case 'D':
			printf ("\"=IF(B%d=C%d,\"\"\"\",IF(C%d=0,-LOG10(ABS(B%d)),-LOG10(ABS((B%d-C%d)/C%d))))\"", r, r, r, r, r, r, r);
			break;
		case 'I':
			printf ("\"=IF(B%d=C%d,\"\"\"\",0)\"", r, r);
			break;
		}

		printf ("\n");

		row++;
	}
}


int
main (int argc, char **argv)
{
	row = 0;
	int do_slow = 0;

	if (argc >= 2 && strcmp (argv[1], "--slow") == 0)
		do_slow = 1;

	printf ("WORST,\"\",\"\",=MIN(D3:D65525)\n");
	row++;
	printf ("\"\",\"\",\"\",\"\"\n");
	row++;

	test_func ("acos", arccos_values, GT_D_D, NULL);
	test_func ("acosh", arccosh_values, GT_D_D, NULL);
	test_func ("agm", agm_values, GT_DD_D, NULL);
	test_func ("asin", arcsin_values, GT_D_D, NULL);
	test_func ("asinh", arcsinh_values, GT_D_D, NULL);
	test_func ("atan", arctan_values, GT_D_D, NULL);
	test_func ("atan2", arctan2_values, GT_DD_D, NULL);
	test_func ("atanh", arctanh_values, GT_D_D, NULL);
	test_func ("besseli", bessel_i0_values, GT_D_D, "%1,0");
	test_func ("besseli", bessel_i1_values, GT_D_D, "%1,1");
	test_func ("besseli", bessel_in_values, GT_ID_D, "21");
	test_func ("besseli", bessel_ix_values, GT_DD_D, "21");
	test_func ("besselj", bessel_j0_values, GT_D_D, "%1,0");
	test_func ("besselj", bessel_j1_values, GT_D_D, "%1,1");
	test_func ("besselj", bessel_jn_values, GT_ID_D, "21");
	// Our besselj truncates the order.
	if (0) test_func ("besselj", bessel_jx_values, GT_DD_D, "21");
	test_func ("besselk", bessel_k0_values, GT_D_D, "%1,0");
	test_func ("besselk", bessel_k1_values, GT_D_D, "%1,1");
	test_func ("besselk", bessel_kn_values, GT_ID_D, "21");
	// Our besselk truncates the order.
	if (0) test_func ("besselk", bessel_kx_values, GT_DD_D, "21");
	test_func ("bessely", bessel_y0_values, GT_D_D, "%1,0");
	test_func ("bessely", bessel_y1_values, GT_D_D, "%1,1");
	test_func ("bessely", bessel_yn_values, GT_ID_D, "21");
	// Our bessely truncates the order.
	if (0) test_func ("bessely", bessel_yx_values, GT_DD_D, "21");
	test_func ("r.pbeta", beta_cdf_values, GT_DDD_D, "312");
	test_func ("beta", beta_values, GT_DD_D, NULL);
	test_func ("combin", binomial_values, GT_II_I, NULL);
	test_func ("r.pbinom", binomial_cdf_values, GT_IDI_D, "312");
	test_func ("r.pcauchy", cauchy_cdf_values, GT_DDD_D, "312");
	test_func ("power", cbrt_values, GT_D_D, "%1,1,3");
	test_func ("r.pchisq", chi_square_cdf_values, GT_ID_D, "21");
	test_func ("cos", cos_values, GT_D_D, NULL);
	test_func ("cosh", cosh_values, GT_D_D, NULL);
	test_func ("cot", cot_values, GT_D_D, NULL);
	test_func ("erf", erf_values, GT_D_D, NULL);
	test_func ("erfc", erfc_values, GT_D_D, NULL);
	test_func ("exp", exp_values, GT_D_D, NULL);
	test_func ("r.pexp", exponential_cdf_values, GT_DD_D, "%2,1/%1");

	test_func ("r.pf", f_cdf_values, GT_IID_D, "312");
	// f_noncentral_cdf_values ( int *n_data, int *n1, int *n2, double *lambda,
	test_func ("fact", i4_factorial_values, GT_I_I, NULL);
	test_func ("fact", r8_factorial_values, GT_I_D, NULL);
	test_func ("factdouble", i4_factorial2_values, GT_I_I, NULL);
	// factorial_rising_values ( int *n_data, int *m, int *n, int *fmn )
	// fresnel_cos_values ( int *n_data, double *x, double *fx )
	// fresnel_sin_values ( int *n_data, double *x, double *fx )
	// frobenius_number_data_values ( int *n_data, int order, int c[], int *f )
	// frobenius_number_order_values ( int *n_data, int *order )
	// frobenius_number_order2_values ( int *n_data, int *c1, int *c2, int *f )
	test_func ("gamma", gamma_values, GT_D_D, NULL);
	test_func ("r.pgamma", gamma_cdf_values, GT_DDD_D, "312");
	// gamma_inc_p_values ( int *n_data, double *a, double *x, double *fx )
	// gamma_inc_q_values ( int *n_data, double *a, double *x, double *fx )
	// gamma_inc_tricomi_values ( int *n_data, double *a, double *x, double *fx )
	// gamma_inc_values ( int *n_data, double *a, double *x, double *fx )
	test_func ("gammaln", gamma_log_values, GT_D_D, NULL);
	// gegenbauer_poly_values ( int *n_data, int *n, double *a, double *x,
	test_func ("r.pgeom", geometric_cdf_values, GT_ID_D, NULL);
	// goodwin_values ( int *n_data, double *x, double *fx )
	test_func ("gd", gud_values, GT_D_D, NULL);
	// hermite_function_values ( int *n_data, int *n, double *x, double *fx )
	// hermite_poly_phys_values ( int *n_data, int *n, double *x, double *fx )
	// hermite_poly_prob_values ( int *n_data, int *n, double *x, double *fx )
	// hyper_1f1_values ( int *n_data, double *a, double *b, double *x,
	// hyper_2f1_values ( int *n_data, double *a, double *b, double *c,
	test_func ("r.phyper", hypergeometric_cdf_values, GT_IIII_D, "%4,%2,%3-%2,%1");
	test_func ("r.dhyper", hypergeometric_pdf_values, GT_IIII_D, "%4,%2,%3-%2,%1");
	// hypergeometric_u_values ( int *n_data, double *a, double *b, double *x,
	// i0ml0_values ( int *n_data, double *x, double *fx )
	// i1ml1_values ( int *n_data, double *x, double *fx )
	// void int_values ( int *n_data, double *x, double *fx )
	// jacobi_cn_values ( int *n_data, double *a, double *x, double *fx )
	// jacobi_dn_values ( int *n_data, double *a, double *x, double *fx )
	// jacobi_poly_values ( int *n_data, int *n, double *a, double *b, double *x,
	// jacobi_sn_values ( int *n_data, double *a, double *x, double *fx )
	// jed_ce_values ( int *n_data, double *jed, int *y, int *m, int *d,
	// jed_mjd_values ( int *n_data, double *jed, double *mjd )
	// jed_rd_values ( int *n_data, double *jed, double *rd )
	// jed_weekday_values ( int *n_data, double *jed, int *weekday )
	// kei0_values ( int *n_data, double *x, double *fx )
	// kei1_values ( int *n_data, double *x, double *fx )
	// ker0_values ( int *n_data, double *x, double *fx )
	// ker1_values ( int *n_data, double *x, double *fx )
	// laguerre_associated_values ( int *n_data, int *n, int *m, double *x,
	// laguerre_general_values ( int *n_data, int *n, double *a, double *x,
	// laguerre_polynomial_values ( int *n_data, int *n, double *x, double *fx )
	test_func ("lambertw", lambert_w_values, GT_D_D, NULL);
	// laplace_cdf_values ( int *n_data, double *mu, double *beta, double *x,
	// legendre_associated_values ( int *n_data, int *n, int *m, double *x,
	// legendre_associated_normalized_sphere_values ( int *n_data, int *n, int *m,
	// legendre_associated_normalized_values ( int *n_data, int *n, int *m,
	// legendre_function_q_values ( int *n_data, int *n, double *x, double *fx )
	// legendre_poly_values ( int *n_data, int *n, double *x, double *fx )
	// lerch_values ( int *n_data, double *z, int *s, double *a, double *fx )
	// lobachevsky_values ( int *n_data, double *x, double *fx )
	test_func ("ln", log_values, GT_D_D, NULL);
	test_func ("r.plnorm", log_normal_cdf_values, GT_DDD_D, "312");
	// log_series_cdf_values ( int *n_data, double *t, int *n, double *fx )
	test_func ("log10", log10_values, GT_D_D, NULL);
	// logarithmic_integral_values ( int *n_data, double *x, double *fx )
	// logistic_cdf_values ( int *n_data, double *mu, double *beta, double *x,
	// mertens_values ( int *n_data, int *n, int *c )
	test_func ("nt_mu", moebius_values, GT_I_I, NULL);
	test_func ("r.pnbinom", negative_binomial_cdf_values, GT_IID_D, NULL);
	// nine_j_values ( int *n_data, double *j1, double *j2, double *j3,
	test_func ("r.pnorm", normal_cdf_values, GT_DDD_D, "312");
	test_func ("normsdist", normal_01_cdf_values, GT_D_D, NULL);
	test_func ("nt_omega", omega_values, GT_I_I, NULL);
	test_func ("owent", owen_values, GT_DD_D, NULL);
	// partition_count_values ( int *n_data, int *n, int *c )
	// partition_distinct_count_values ( int *n_data, int *n, int *c )
	test_func ("nt_phi", phi_values, GT_I_I, NULL);
	if (do_slow) test_func ("nt_pi", pi_values, GT_I_I, NULL);
	test_func ("pochhammer", i4_rise_values, GT_II_I, NULL);
	test_func ("pochhammer", r8_rise_values, GT_DI_D, NULL);
	test_func ("r.ppois", poisson_cdf_values, GT_DI_D, "21");
	// polylogarithm_values ( int *n_data, int *n, double *z, double *fx )
	// prandtl_values ( int *n_data, double *tc, double *p, double *pr )
	if (do_slow) test_func ("ithprime", prime_values, GT_I_I, NULL);
	// psat_values ( int *n_data, double *tc, double *p )
	test_func ("digamma", psi_values, GT_D_D, NULL);
	// r8_factorial_log_values ( int *n_data, int *n, double *fn )
	// r8_factorial_values ( int *n_data, int *n, double *fn )
	// rayleigh_cdf_values ( int *n_data, double *sigma, double *x, double *fx )
	// secvir_values ( int *n_data, double *tc, double *vir )
	// shi_values ( int *n_data, double *x, double *fx )
	// si_values ( int *n_data, double *x, double *fx )
	test_func ("nt_sigma", sigma_values, GT_I_I, NULL);
	// sin_power_int_values ( int *n_data, double *a, double *b, int *n,
	test_func ("sin", sin_values, GT_D_D, NULL);
	test_func ("sinh", sinh_values, GT_D_D, NULL);
	// six_j_values ( int *n_data, double *j1, double *j2, double *j3,
	// sound_values ( int *n_data, double *tc, double *p, double *c )
	// sphere_unit_area_values ( int *n_data, int *n, double *area )
	// sphere_unit_volume_values ( int *n_data, int *n, double *volume )
	// spherical_harmonic_values ( int *n_data, int *l, int *m, double *theta,
	test_func ("sqrt", sqrt_values, GT_D_D, NULL);
	// stirling1_values ( int *n_data, int *n, int *m, int *fx )
	// stirling2_values ( int *n_data, int *n, int *m, int *fx )
	// stromgen_values ( int *n_data, double *x, double *fx )
	// struve_h0_values ( int *n_data, double *x, double *fx )
	// struve_h1_values ( int *n_data, double *x, double *fx )
	// struve_l0_values ( int *n_data, double *x, double *fx )
	// struve_l1_values ( int *n_data, double *x, double *fx )
	test_func ("r.pt", student_cdf_values, GT_DD_D, "21");
	// student_noncentral_cdf_values ( int *n_data, int *df, double *lambda,
	// subfactorial_values ( int *n_data, int *n, int *fn )
	// surten_values ( int *n_data, double *tc, double *sigma )
	// synch1_values ( int *n_data, double *x, double *fx )
	// synch2_values ( int *n_data, double *x, double *fx )
	test_func ("tan", tan_values, GT_D_D, NULL);
	test_func ("tanh", tanh_values, GT_D_D, NULL);
	test_func ("nt_d", tau_values, GT_I_I, NULL);
	// thercon_values ( int *n_data, double *tc, double *p, double *lambda )
	// three_j_values ( int *n_data, double *j1, double *j2, double *j3,
	// tran02_values ( int *n_data, double *x, double *fx )
	// tran03_values ( int *n_data, double *x, double *fx )
	// tran04_values ( int *n_data, double *x, double *fx )
	// tran05_values ( int *n_data, double *x, double *fx )
	// tran06_values ( int *n_data, double *x, double *fx )
	// tran07_values ( int *n_data, double *x, double *fx )
	// tran08_values ( int *n_data, double *x, double *fx )
	// tran09_values ( int *n_data, double *x, double *fx )
	// trigamma_values ( int *n_data, double *x, double *fx )
	// tsat_values ( int *n_data, double *p, double *tc )
	// van_der_corput_values ( int *n_data, int *base, int *seed, double *value )
	// viscosity_values ( int *n_data, double *tc, double *p, double *eta )
	// von_mises_cdf_values ( int *n_data, double *a, double *b, double *x,

	// Differences in year interpretation.
	if (0) test_func ("weekday", weekday_values, GT_III_I, "date(%1,%2,%3)");

	test_func ("r.pweibull", weibull_cdf_values, GT_DDD_D, "312");
	// zeta_values ( int *n_data, int *n, double *zeta )


	return 0;
}
