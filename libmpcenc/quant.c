/*
 * Musepack audio compression
 * Copyright (c) 2005-2009, The Musepack Development Team
 * Copyright (C) 1999-2004 Buschmann/Klemm/Piecha/Wolf
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

#include "libmpcenc.h"
#include <mpc/minimax.h>
#include <mpc/mpcmath.h>

/* V A R I A B L E S */
float  __SCF    [128 + 6];   // tabulated scalefactors
#define SCF             ( __SCF + 6 )
float  __invSCF [128 + 6];   // inverted scalefactors
#define invSCF  (__invSCF + 6)


// Quantization-coefficients: step/65536 bzw. (2*D[Res]+1)/65536
static const float  __A [1 + 18] = {
    0.0000762939453125f,
    0.0000000000000000f, 0.0000457763671875f, 0.0000762939453125f, 0.0001068115234375f,
    0.0001373291015625f, 0.0002288818359375f, 0.0004730224609375f, 0.0009613037109375f,
    0.0019378662109375f, 0.0038909912109375f, 0.0077972412109375f, 0.0156097412109375f,
    0.0312347412109375f, 0.0624847412109375f, 0.1249847412109375f, 0.2499847412109375f,
    0.4999847412109375f
};


// Requantization-coefficients: 65536/step bzw. 1/A[Res]
static const float  __C [1 + 18] = {
    13107.200000000001f,
    65535.000000000000f, 21845.333333333332f, 13107.200000000001f, 9362.285714285713f,
     7281.777777777777f,  4369.066666666666f,  2114.064516129032f, 1040.253968253968f,
      516.031496062992f,   257.003921568627f,   128.250489236790f,   64.062561094819f,
       32.015632633121f,    16.003907203907f,     8.000976681723f,    4.000244155527f,
        2.000061037018f,     1.000015259022f
};


// Requantization-Offset: 2*D+1 = steps of quantizer
static const int  __D [1 + 18] = {
    2,
    0,     1,     2,     3,     4,     7,    15,    31,    63,
  127,   255,   511,  1023,  2047,  4095,  8191, 16383, 32767
};

#define A   (__A + 1)
#define C   (__C + 1)
#define D   (__D + 1)

// Generation of the scalefactors and their inverses
void
Init_Skalenfaktoren ( void )
{
    int  n;

    for ( n = -6; n < 128; n++ ) {
        SCF[n]    = (float) ( pow(10.,-0.1*(n-1)/1.26) );
        invSCF[n] = (float) ( pow(10., 0.1*(n-1)/1.26) );
    }
}

#ifdef _MSC_VER
#pragma warning ( disable : 4305 )
#endif

static float  NoiseInjectionCompensation1D [18] = {
    1.f,
    0.884621,
    0.935711,
    0.970829,
    0.987941,
    0.994315,
    0.997826,
    0.999744,
    1., 1., 1., 1., 1., 1., 1., 1., 1., 1.
} ;

#ifdef _MSC_VER
#pragma warning ( default : 4305 )
#endif

void
NoiseInjectionComp ( void )
{
    size_t  i;

    for ( i = 0; i < sizeof(NoiseInjectionCompensation1D)/sizeof(*NoiseInjectionCompensation1D); i++ )
        NoiseInjectionCompensation1D [i] = 1.f;
}


// Quantizes a subband and calculates iSNR
float
ISNR_Schaetzer ( const float* input, const float SNRcomp, const int res )
{
	const float  fac    = A [res] * NoiseInjectionCompensation1D [res];
	const float  invfac = C [res] / NoiseInjectionCompensation1D [res];
    float  signal = 1.e-30f;
    float  error = 1.e-30f;
	const float * in_end = input + 36;

    // Summation of the absolute power and the quadratic error
	do {
		float  err;
		err = mpc_nearbyintf(input[0] * fac) * invfac - input[0];
        error += err * err;
		signal += input[0] * input[0];

		err = mpc_nearbyintf(input[1] * fac) * invfac - input[1];
		error += err * err;
		signal += input[1] * input[1];

		err = mpc_nearbyintf(input[2] * fac) * invfac - input[2];
		error += err * err;
		signal += input[2] * input[2];

		err = mpc_nearbyintf(input[3] * fac) * invfac - input[3];
		error += err * err;
		signal += input[3] * input[3];

		input += 4;

	} while (input < in_end);

	error *= NoiseInjectionCompensation1D [res] * NoiseInjectionCompensation1D [res];
	signal *= NoiseInjectionCompensation1D [res] * NoiseInjectionCompensation1D [res];

    // Utilization of SNRcomp only if SNR > 1 !!!
    return signal > error  ?  error / (SNRcomp * signal)  :  error / signal;
}


float
ISNR_Schaetzer_Trans ( const float* input, const float SNRcomp, const int res )
{
    int    k;
    float  fac    = A [res];
    float  invfac = C [res];
	float  signal, error, ret, err, sig;

    // Summation of the absolute power and the quadratic error
	k = 0;
	signal = error = 1.e-30f;
    for ( ; k < 12; k++ ) {
        sig = input[k] * NoiseInjectionCompensation1D [res];
		err = mpc_nearbyintf(sig * fac) * invfac - sig;

        error += err * err;
        signal += sig * sig;
    }
    err = signal > error  ?  error / (SNRcomp * signal)  :  error / signal;
    ret = err;
    signal = error = 1.e-30f;
    for ( ; k < 24; k++ ) {
        sig = input[k] * NoiseInjectionCompensation1D [res];
		err = mpc_nearbyintf(sig * fac) * invfac - sig;

        error += err * err;
        signal += sig * sig;
    }
    err = signal > error  ?  error / (SNRcomp * signal)  :  error / signal;
	ret = maxf(ret, err);

    signal = error = 1.e-30f;
    for ( ; k < 36; k++ ) {
        sig = input[k] * NoiseInjectionCompensation1D [res];
		err = mpc_nearbyintf(sig * fac) * invfac - sig;

        error += err * err;
        signal += sig * sig;
    }
    err = signal > error  ?  error / (SNRcomp * signal)  :  error / signal;
	ret = maxf(ret, err);

    return ret;
}


// Linear quantizer for a subband
void
QuantizeSubband ( mpc_int16_t* qu_output, const float* input, const int res, float* errors, const int maxNsOrder )
{
	int    n, quant;
    int    offset  = D [res];
    float  mult    = A [res] * NoiseInjectionCompensation1D [res];
    float  invmult = C [res];
    float  signal;

	for ( n = 0; n < 36 - maxNsOrder; n++) {
		quant = (unsigned int)(mpc_lrintf(input[n] * mult) + offset);

        // limitation to 0...2D
        if ((unsigned int)quant > (unsigned int)offset * 2 ) {
            quant = mini ( quant, offset * 2 );
            quant = maxi ( quant, 0 );
        }
        qu_output[n] = quant;
    }

    for ( ; n < 36; n++) {
        signal = input[n] * mult;
		quant = (unsigned int)(mpc_lrintf(signal) + offset);

        // calculate the current error and save it for error refeeding
        errors [n + 6] = invmult * (quant - offset) - signal * NoiseInjectionCompensation1D [res];

        // limitation to 0...2D
        if ((unsigned int)quant > (unsigned int)offset * 2 ) {
            quant = mini ( quant, offset * 2 );
            quant = maxi ( quant, 0 );
        }
        qu_output[n] = quant;
    }
}


// NoiseShaper for a subband
void
QuantizeSubbandWithNoiseShaping ( mpc_int16_t* qu_output, const float* input, const int res, float* errors, const float* FIR )
{
    float  signal;
    float  mult    = A [res];
    float  invmult = C [res];
    int    offset  = D [res];
    int    n, quant;

	memset(errors, 0, 6 * sizeof *errors);       // arghh, it produces pops on each frame boundary!

    for ( n = 0; n < 36; n++) {
        signal = input[n] * NoiseInjectionCompensation1D [res] - (FIR[5]*errors[n+0] + FIR[4]*errors[n+1] + FIR[3]*errors[n+2] + FIR[2]*errors[n+3] + FIR[1]*errors[n+4] + FIR[0]*errors[n+5]);
		quant = mpc_lrintf(signal * mult);

        // calculate the current error and save it for error refeeding
        errors [n + 6] = invmult * quant - signal * NoiseInjectionCompensation1D [res];

        // limitation to +/-D
        quant = minf ( quant, +offset );
        quant = maxf ( quant, -offset );

        qu_output[n] = (unsigned int)(quant + offset);
    }
}

/* end of quant.c */

// pfk@schnecke.offl.uni-jena.de@EMAIL, Andree.Buschmann@web.de@EMAIL, BuschmannA@becker.de@EMAIL, miyaguch@eskimo.com@EMAIL, r3mix@irc.openprojects.net@EMAIL, dibrom@users.sourceforge.net@EMAIL, m.p.bakker-10@student.utwente.nl@EMAIL, djmrob@essex.ac.uk@EMAIL, dim@psytel-research.co.yu@EMAIL, lerch@zplane.de@EMAIL, takehiro@users.sourceforge.net@EMAIL, aleidinger@users.sourceforge.net@EMAIL, Robert.Hegemann@gmx.de@EMAIL, bouvigne@mp3-tech.org@EMAIL, monty@xiph.org@EMAIL, Pumpkinz99@aol.com@EMAIL, spase@outerspase.net@EMAIL, mt@wildpuppy.com@EMAIL, juha.laaksonheimo@tut.fi@EMAIL, speek@myrealbox.com@EMAIL, w.speek@12move.nl@EMAIL, martin@spueler.de@EMAIL, nicolaus.berglmeir@t-online.de@EMAIL, thomas.a.juerges@ruhr-uni-bochum.de@EMAIL, HelH@mpex.net@EMAIL, garf@roadum.demon.co.uk@EMAIL, gcp@sjeng.org@EMAIL, mike@naivesoftware.com@EMAIL, case@mobiili.net@EMAIL, steve.lhomme@free.fr@EMAIL, walter@binity.com@EMAIL
