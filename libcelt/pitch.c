/* Copyright (c) 2007-2008 CSIRO
   Copyright (c) 2007-2009 Xiph.Org Foundation
   Written by Jean-Marc Valin */
/**
   @file pitch.c
   @brief Pitch analysis
 */

/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "pitch.h"
#include "os_support.h"
#include "modes.h"
#include "stack_alloc.h"
#include "mathops.h"

static void find_best_pitch(celt_word32 *xcorr, celt_word32 maxcorr, celt_word16 *y,
                            int yshift, int len, int max_pitch, int best_pitch[2],
                            celt_word32 *best_gain)
{
   int i, j;
   celt_word32 Syy=1;
   celt_word16 best_num[2];
   celt_word32 best_den[2];
#ifdef FIXED_POINT
   int xshift;

   xshift = celt_ilog2(maxcorr)-14;
#endif

   best_num[0] = -1;
   best_num[1] = -1;
   best_den[0] = 0;
   best_den[1] = 0;
   best_pitch[0] = 0;
   best_pitch[1] = 1;
   for (j=0;j<len;j++)
      Syy = MAC16_16(Syy, y[j],y[j]);
   for (i=0;i<max_pitch;i++)
   {
      if (xcorr[i]>0)
      {
         celt_word16 num;
         celt_word32 xcorr16;
         xcorr16 = EXTRACT16(VSHR32(xcorr[i], xshift));
         num = MULT16_16_Q15(xcorr16,xcorr16);
         if (MULT16_32_Q15(num,best_den[1]) > MULT16_32_Q15(best_num[1],Syy))
         {
            if (MULT16_32_Q15(num,best_den[0]) > MULT16_32_Q15(best_num[0],Syy))
            {
               best_num[1] = best_num[0];
               best_den[1] = best_den[0];
               best_pitch[1] = best_pitch[0];
               best_num[0] = num;
               best_den[0] = Syy;
               best_pitch[0] = i;
            } else {
               best_num[1] = num;
               best_den[1] = Syy;
               best_pitch[1] = i;
            }
         }
      }
      Syy += SHR32(MULT16_16(y[i+len],y[i+len]),yshift) - SHR32(MULT16_16(y[i],y[i]),yshift);
      Syy = MAX32(1, Syy);
   }
   if (best_gain)
   {
      *best_gain = xcorr[best_pitch[0]]/best_den[0];
   }
}

void pitch_downsample(celt_sig * restrict x[], celt_word16 * restrict x_lp, int len, int end, int _C, celt_sig * restrict xmem, celt_word16 * restrict filt_mem)
{
   int i;
   const int C = CHANNELS(_C);
   for (i=1;i<len>>1;i++)
      x_lp[i] = SHR32(HALF32(HALF32(x[0][(2*i-1)]+x[0][(2*i+1)])+x[0][2*i]), SIG_SHIFT);
   x_lp[0] = SHR32(HALF32(HALF32(*xmem+x[0][1])+x[0][0]), SIG_SHIFT);
   *xmem = x[0][end-1];
   if (C==2)
   {
      for (i=1;i<len>>1;i++)
      x_lp[i] = SHR32(HALF32(HALF32(x[1][(2*i-1)]+x[1][(2*i+1)])+x[1][2*i]), SIG_SHIFT);
      x_lp[0] += SHR32(HALF32(HALF32(x[1][1])+x[1][0]), SIG_SHIFT);
      *xmem += x[1][end-1];
   }
}

void pitch_search(const CELTMode *m, const celt_word16 * restrict x_lp, celt_word16 * restrict y,
                  int len, int max_pitch, int *pitch, celt_sig *xmem, int M, celt_word16 *gain)
{
   int i, j;
   int lag;
   int best_pitch[2]={0};
   VARDECL(celt_word16, x_lp4);
   VARDECL(celt_word16, y_lp4);
   VARDECL(celt_word32, xcorr);
   celt_word32 maxcorr=1;
   int offset;
   int shift=0;

   SAVE_STACK;

   lag = len+max_pitch;

   ALLOC(x_lp4, len>>2, celt_word16);
   ALLOC(y_lp4, lag>>2, celt_word16);
   ALLOC(xcorr, max_pitch>>1, celt_word32);

   /* Downsample by 2 again */
   for (j=0;j<len>>2;j++)
      x_lp4[j] = x_lp[2*j];
   for (j=0;j<lag>>2;j++)
      y_lp4[j] = y[2*j];

#ifdef FIXED_POINT
   shift = celt_ilog2(MAX16(celt_maxabs16(x_lp4, len>>2), celt_maxabs16(y_lp4, lag>>2)))-11;
   if (shift>0)
   {
      for (j=0;j<len>>2;j++)
         x_lp4[j] = SHR16(x_lp4[j], shift);
      for (j=0;j<lag>>2;j++)
         y_lp4[j] = SHR16(y_lp4[j], shift);
      /* Use double the shift for a MAC */
      shift *= 2;
   } else {
      shift = 0;
   }
#endif

   /* Coarse search with 4x decimation */

   for (i=0;i<max_pitch>>2;i++)
   {
      celt_word32 sum = 0;
      for (j=0;j<len>>2;j++)
         sum = MAC16_16(sum, x_lp4[j],y_lp4[i+j]);
      xcorr[i] = MAX32(-1, sum);
      maxcorr = MAX32(maxcorr, sum);
   }
   find_best_pitch(xcorr, maxcorr, y_lp4, 0, len>>2, max_pitch>>2, best_pitch, NULL);

   /* Finer search with 2x decimation */
   maxcorr=1;
   for (i=0;i<max_pitch>>1;i++)
   {
      celt_word32 sum=0;
      xcorr[i] = 0;
      if (abs(i-2*best_pitch[0])>2 && abs(i-2*best_pitch[1])>2)
         continue;
      for (j=0;j<len>>1;j++)
         sum += SHR32(MULT16_16(x_lp[j],y[i+j]), shift);
      xcorr[i] = MAX32(-1, sum);
      maxcorr = MAX32(maxcorr, sum);
   }
   find_best_pitch(xcorr, maxcorr, y, shift, len>>1, max_pitch>>1, best_pitch, gain);

   /* Refine by pseudo-interpolation */
   if (best_pitch[0]>0 && best_pitch[0]<(max_pitch>>1)-1)
   {
      celt_word32 a, b, c;
      a = xcorr[best_pitch[0]-1];
      b = xcorr[best_pitch[0]];
      c = xcorr[best_pitch[0]+1];
      if ((c-a) > MULT16_32_Q15(QCONST16(.7f,15),b-a))
         offset = 1;
      else if ((a-c) > MULT16_32_Q15(QCONST16(.7f,15),b-c))
         offset = -1;
      else 
         offset = 0;
   } else {
      offset = 0;
   }
   *pitch = 2*best_pitch[0]-offset;

   //CELT_MOVE(y, y+(N>>1), (lag-N)>>1);
   //CELT_MOVE(y+((lag-N)>>1), x_lp, N>>1);

   RESTORE_STACK;

   /*printf ("%d\n", *pitch);*/
}

float remove_doubling(celt_word32 *pre[2], int COMBFILTER_MAXPERIOD, int N, int *_T0)
{
   int k, i, T, T0, k0;
   float g, g0;
   float *x;
   float xy,xx,yy;

   T = T0 = *_T0;
   x = pre[0]+COMBFILTER_MAXPERIOD;
   xx=xy=yy=0;
   for (i=0;i<N;i++)
   {
      xy += x[i]*x[i-T0];
      xx += x[i]*x[i];
      yy += x[i-T0]*x[i-T0];
   }
   g = g0 = xy/sqrt(1+xx*yy);
   k0 = 1;
   for (k=2;k<=5;k++)
   {
      int T1;
      float g1;
      T1 = (2*T0+k)/(2*k);
      xx=xy=yy=0;
      for (i=0;i<N;i++)
      {
         xy += x[i]*x[i-T1];
         xx += x[i]*x[i];
         yy += x[i-T1]*x[i-T1];
      }
      g1 = xy/sqrt(1+xx*yy);
      if (T1 > 50 && (g1 > .85*g0 || g1 > .8 || (k==2*k0 && g1 > .7)))
      {
         g = g1;
         T = T1;
      }
   }
   /*printf ("%d %f\n", T, g);*/
   *_T0 = T;
   return g;
}

