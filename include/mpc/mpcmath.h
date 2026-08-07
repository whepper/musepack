/*
 * Musepack audio compression
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <math.h>
#include <string.h>

#include <mpc/mpc_types.h>

/// Convert between float and its IEEE 754 bit pattern without union
/// type-punning (which is undefined behavior).
static mpc_inline mpc_uint32_t mpc_float_to_bits(float f)
{
	mpc_uint32_t n;
	memcpy(&n, &f, sizeof n);
	return n;
}

static mpc_inline float mpc_bits_to_float(mpc_uint32_t n)
{
	float f;
	memcpy(&f, &n, sizeof f);
	return f;
}

/// Convert between double and its IEEE 754 bit pattern (as two 32-bit halves,
/// low half first) without union type-punning.
static mpc_inline void mpc_double_to_bits(double d, mpc_uint32_t n[2])
{
	memcpy(n, &d, sizeof d);
}

static mpc_inline double mpc_bits_to_double(mpc_uint32_t n[2])
{
	double d;
	memcpy(&d, n, sizeof d);
	return d;
}

static mpc_inline mpc_int32_t mpc_lrintf(float fVal)
{
	return (mpc_int32_t) mpc_float_to_bits(fVal + 0x00FF8000) - 0x4B7F8000;
}

#define mpc_round32		mpc_lrintf
#define mpc_nearbyintf	mpc_lrintf


#ifndef M_PI
# define M_PI            3.1415926535897932384626433832795029     // 4*atan(1)
# define M_PIl           3.1415926535897932384626433832795029L
# define M_LN2           0.6931471805599453094172321214581766     // ln(2)
# define M_LN2l          0.6931471805599453094172321214581766L
# define M_LN10          2.3025850929940456840179914546843642     // ln 10 */
# define M_LN10l         2.3025850929940456840179914546843642L
#endif

// fast but maybe more inaccurate, use if you need speed
#if defined(__GNUC__) && !defined(__APPLE__)
#  define SIN(x)      sinf ((float)(x))
#  define COS(x)      cosf ((float)(x))
#  define ATAN2(x,y)  atan2f ((float)(x), (float)(y))
#  define SQRT(x)     sqrtf ((float)(x))
#  define LOG(x)      logf ((float)(x))
#  define LOG10(x)    log10f ((float)(x))
#  define POW(x,y)    expf (logf(x) * (y))
#  define POW10(x)    expf (M_LN10 * (x))
#  define FLOOR(x)    floorf ((float)(x))
#  define IFLOOR(x)   (int) floorf ((float)(x))
#  define FABS(x)     fabsf ((float)(x))
#else
# define SIN(x)      (float) sin (x)
# define COS(x)      (float) cos (x)
# define ATAN2(x,y)  (float) atan2 (x, y)
# define SQRT(x)     (float) sqrt (x)
# define LOG(x)      (float) log (x)
# define LOG10(x)    (float) log10 (x)
# define POW(x,y)    (float) pow (x,y)
# define POW10(x)    (float) pow (10., (x))
# define FLOOR(x)    (float) floor (x)
# define IFLOOR(x)   (int)   floor (x)
# define FABS(x)     (float) fabs (x)
#endif

#define SQRTF(x)      SQRT (x)
#ifdef FAST_MATH
# define TABSTEP      64
# define COSF(x)      my_cos ((float)(x))
# define ATAN2F(x,y)  my_atan2 ((float)(x), (float)(y))
# define IFLOORF(x)   my_ifloor ((float)(x))

void   Init_FastMath ( void );
extern float  tabatan2   [] [2];
extern float  tabcos     [] [2];
extern float  tabsqrt_ex [];
extern float  tabsqrt_m  [] [2];

static mpc_inline float my_atan2 ( float x, float y )
{
	float t, ret; int i;
	mpc_uint32_t mx = mpc_float_to_bits(x);
	mpc_uint32_t my = mpc_float_to_bits(y);

	if ( (mx & 0x7FFFFFFF) < (my & 0x7FFFFFFF) ) {
		i   = mpc_round32 (t = TABSTEP * (x / y));
		ret = tabatan2 [1*TABSTEP+i][0] + tabatan2 [1*TABSTEP+i][1] * (t-i);
		if ( (mpc_int32_t) my < 0 )
			ret = (float)(ret - M_PI);
	}
	else if ( (mpc_int32_t) mx < 0 ) {
		i   = mpc_round32 (t = TABSTEP * (y / x));
		ret = - M_PI/2 - tabatan2 [1*TABSTEP+i][0] + tabatan2 [1*TABSTEP+i][1] * (i-t);
	}
	else if ( (mpc_int32_t) mx > 0 ) {
		i   = mpc_round32 (t = TABSTEP * (y / x));
		ret = + M_PI/2 - tabatan2 [1*TABSTEP+i][0] + tabatan2 [1*TABSTEP+i][1] * (i-t);
	}
	else {
		ret = 0.;
	}
	return ret;
}


static mpc_inline float my_cos ( float x )
{
	float t, ret; int i;
	i   = mpc_round32 (t = TABSTEP * x);
	ret = tabcos [13*TABSTEP+i][0] + tabcos [13*TABSTEP+i][1] * (t-i);
	return ret;
}


static mpc_inline int my_ifloor ( float x )
{
	return (mpc_int32_t) mpc_float_to_bits((float) (x + (0x0C00000L + 0.500000001))) - 1262485505;
}


static mpc_inline float my_sqrt ( float x )
{
	float  ret; int i, ex;
	mpc_uint32_t mx = mpc_float_to_bits(x);
	ex   = (int) ((mpc_int32_t) mx >> 23);      // get the exponent
	mx   = (mx & 0x7FFFFF) | 0x42800000;        // delete the exponent
	i    = mpc_round32 (mpc_bits_to_float(mx)); // Integer-part of the mantissa  (round ????????????)
	ret  = tabsqrt_m [i-TABSTEP][0] + tabsqrt_m [i-TABSTEP][1] * (mpc_bits_to_float(mx)-i); // calculate value
	ret *= tabsqrt_ex [ex];
	return ret;
}
#else
# define COSF(x)      COS (x)
# define ATAN2F(x,y)  ATAN2 (x,y)
# define IFLOORF(x)   IFLOOR (x)
#endif

