#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdio.h>

/* Uncomment below if compiling Pintos kernel */
#include <debug.h>

/* Uncomment below if compiling fixed-test.c */
//#include <assert.h>
//#define ASSERT assert

/* Parameters. */
#define FIX_BITS 32             /* Total bits per fixed-point number. */
#define FIX_P 16                /* Number of integer bits. */
#define FIX_Q 16                /* Number of fractional bits. */
#define FIX_F (1 << FIX_Q)      /* pow(2, FIX_Q). */

#define FIX_MIN_INT (-FIX_MAX_INT)      /* Smallest representable integer. */
#define FIX_MAX_INT ((1 << FIX_P) - 1)  /* Largest representable integer. */

/* A fixed-point number. */
typedef struct
{
	int f;
} fixed_point_t;

/* Returns a fixed-point number with F as its internal value. */
static inline fixed_point_t
__mk_fix(int f)
{
	fixed_point_t x;
	x.f = f;
	return x;
}

/* Returns fixed-point number corresponding to integer N. */
static inline fixed_point_t
fix_int(int n)
{
	ASSERT(n >= FIX_MIN_INT && n <= FIX_MAX_INT);
	return __mk_fix(n * FIX_F);
}

/* Returns fixed-point number corresponding to N divided by D. */
static inline fixed_point_t
fix_frac(int n, int d)
{
	ASSERT(d != 0);
	ASSERT(n / d >= FIX_MIN_INT && n / d <= FIX_MAX_INT);
	return __mk_fix((long long) n * FIX_F / d);
}

/* Returns X rounded to the nearest integer. */
static inline int
fix_round(fixed_point_t x)
{
	return (x.f + FIX_F / 2) / FIX_F;
}

/* Returns X truncated down to the nearest integer. */
static inline int
fix_trunc(fixed_point_t x)
{
	return x.f / FIX_F;
}

/* Returns X + Y. */
static inline fixed_point_t
fix_add(fixed_point_t x, fixed_point_t y)
{
	return __mk_fix(x.f + y.f);
}

/* Returns X - Y. */
static inline fixed_point_t
fix_sub(fixed_point_t x, fixed_point_t y)
{
	return __mk_fix(x.f - y.f);
}

/* Returns X * Y. */
static inline fixed_point_t
fix_mul(fixed_point_t x, fixed_point_t y)
{
	return __mk_fix((long long) x.f * y.f / FIX_F);
}

/* Returns X * N. */
static inline fixed_point_t
fix_scale(fixed_point_t x, int n)
{
	ASSERT(n >= 0);
	return __mk_fix(x.f * n);
}

/* Returns X / Y. */
static inline fixed_point_t
fix_div(fixed_point_t x, fixed_point_t y)
{
	return __mk_fix((long long) x.f * FIX_F / y.f);
}

/* Returns X / N. */
static inline fixed_point_t
fix_unscale(fixed_point_t x, int n)
{
	ASSERT(n > 0);
	return __mk_fix(x.f / n);
}

/* Returns 1 / X. */
static inline fixed_point_t
fix_inv(fixed_point_t x)
{
	return fix_div(fix_int(1), x);
}

/* Returns -1 if X < Y, 0 if X == Y, 1 if X > Y. */
static inline int
fix_compare(fixed_point_t x, fixed_point_t y)
{
	return x.f < y.f ? -1 : x.f > y.f;
}

static inline double
fix_todouble(fixed_point_t x)
{
	return (double) x.f / FIX_F;
}

static inline char *
fix_tostring(fixed_point_t x, char *buf, size_t size)
{
	int i;
	int rem;
	int div;
	long long value;
	long long lrem;
	char fracstr[4];

	  div = (int) x.f / FIX_F;
	  rem = (int) x.f % FIX_F;
	  lrem = (long long) rem;

	  for (i = 0; i < 3; i++)
	  {
		  value = ((long long) lrem * 10) / FIX_F;
		  lrem  = ((long long) lrem * 10) % FIX_F;
		  fracstr[i] = '0' + (char) value;
	  }

	  fracstr[3] = '\0';
	  snprintf(buf, size, "%d.%s", div, fracstr);

	  return buf;
}

#endif /* threads/fixed-point.h */
