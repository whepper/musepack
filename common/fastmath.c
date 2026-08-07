/*
 * Musepack audio compression
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

#include "mpc/mpcmath.h"

#ifdef FAST_MATH

// Tables are filled at runtime by Init_FastMath(), so they must not be const.
float  tabatan2   [ 2*TABSTEP+1] [2];
float  tabcos     [26*TABSTEP+1] [2];
float  tabsqrt_ex [256];
float  tabsqrt_m  [   TABSTEP+1] [2];


void   Init_FastMath ( void )
{
    int i; double xm, x0, xp, x, y; float* p;

    p = (float*) tabatan2;
    for ( i = -TABSTEP; i <= TABSTEP; i++ ) {
        xm = atan ((i-0.5)/TABSTEP);
        x0 = atan ((i+0.0)/TABSTEP);
        xp = atan ((i+0.5)/TABSTEP);
        x  = x0/2 + (xm + xp)/4;
        y  = xp - xm;
        *p++ = x;
        *p++ = y;
    }

    p = (float*) tabcos;
    for ( i = -13*TABSTEP; i <= 13*TABSTEP; i++ ) {
        xm = cos ((i-0.5)/TABSTEP);
        x0 = cos ((i+0.0)/TABSTEP);
        xp = cos ((i+0.5)/TABSTEP);
        x  = x0/2 + (xm + xp)/4;
        y  = xp - xm;
        *p++ = x;
        *p++ = y;
    }

    p = (float*) tabsqrt_ex;
    for ( i = 0; i < 255; i++ ) {
        *p++ = sqrtf (mpc_bits_to_float ((mpc_uint32_t) (i << 23)));
    }
    *p++ = sqrtf (mpc_bits_to_float ((mpc_uint32_t) ((255 << 23) - 1)));

    p = (float*) tabsqrt_m;
    for ( i = 1*TABSTEP; i <= 2*TABSTEP; i++ ) {
        xm = sqrt ((i-0.5)/TABSTEP);
        x0 = sqrt ((i+0.0)/TABSTEP);
        xp = sqrt ((i+0.5)/TABSTEP);
        x  = x0/2 + (xm + xp)/4;
        y  = xp - xm;
        *p++ = x;
        *p++ = y;
    }
}

#endif
