/* (C) 2007-2008 Jean-Marc Valin, CSIRO
   (C) 2008-2009 Gregory Maxwell */
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

#include <math.h>
#include "bands.h"
#include "modes.h"
#include "vq.h"
#include "cwrs.h"
#include "stack_alloc.h"
#include "os_support.h"
#include "mathops.h"
#include "rate.h"

const celt_word16_t sqrtC_1[2] = {QCONST16(1.f, 14), QCONST16(1.414214f, 14)};



#ifdef FIXED_POINT
/* Compute the amplitude (sqrt energy) in each of the bands */
void compute_band_energies(const CELTMode *m, const celt_sig_t *X, celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word32_t maxval=0;
         celt_word32_t sum = 0;
         
         j=eBands[i]; do {
            maxval = MAX32(maxval, X[j+c*N]);
            maxval = MAX32(maxval, -X[j+c*N]);
         } while (++j<eBands[i+1]);
         
         if (maxval > 0)
         {
            int shift = celt_ilog2(maxval)-10;
            j=eBands[i]; do {
               sum = MAC16_16(sum, EXTRACT16(VSHR32(X[j+c*N],shift)),
                                   EXTRACT16(VSHR32(X[j+c*N],shift)));
            } while (++j<eBands[i+1]);
            /* We're adding one here to make damn sure we never end up with a pitch vector that's
               larger than unity norm */
            bank[i+c*m->nbEBands] = EPSILON+VSHR32(EXTEND32(celt_sqrt(sum)),-shift);
         } else {
            bank[i+c*m->nbEBands] = EPSILON;
         }
         /*printf ("%f ", bank[i+c*m->nbEBands]);*/
      }
   }
   /*printf ("\n");*/
}

/* Normalise each band such that the energy is one. */
void normalise_bands(const CELTMode *m, const celt_sig_t * restrict freq, celt_norm_t * restrict X, const celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   for (c=0;c<C;c++)
   {
      i=0; do {
         celt_word16_t g;
         int j,shift;
         celt_word16_t E;
         shift = celt_zlog2(bank[i+c*m->nbEBands])-13;
         E = VSHR32(bank[i+c*m->nbEBands], shift);
         g = EXTRACT16(celt_rcp(SHL32(E,3)));
         j=eBands[i]; do {
            X[j*C+c] = MULT16_16_Q15(VSHR32(freq[j+c*N],shift-1),g);
         } while (++j<eBands[i+1]);
      } while (++i<m->nbEBands);
   }
}

#else /* FIXED_POINT */
/* Compute the amplitude (sqrt energy) in each of the bands */
void compute_band_energies(const CELTMode *m, const celt_sig_t *X, celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word32_t sum = 1e-10;
         for (j=eBands[i];j<eBands[i+1];j++)
            sum += X[j+c*N]*X[j+c*N];
         bank[i+c*m->nbEBands] = sqrt(sum);
         /*printf ("%f ", bank[i+c*m->nbEBands]);*/
      }
   }
   /*printf ("\n");*/
}

#ifdef EXP_PSY
void compute_noise_energies(const CELTMode *m, const celt_sig_t *X, const celt_word16_t *tonality, celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word32_t sum = 1e-10;
         for (j=eBands[i];j<eBands[i+1];j++)
            sum += X[j*C+c]*X[j+c*N]*tonality[j];
         bank[i+c*m->nbEBands] = sqrt(sum);
         /*printf ("%f ", bank[i+c*m->nbEBands]);*/
      }
   }
   /*printf ("\n");*/
}
#endif

/* Normalise each band such that the energy is one. */
void normalise_bands(const CELTMode *m, const celt_sig_t * restrict freq, celt_norm_t * restrict X, const celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word16_t g = 1.f/(1e-10+bank[i+c*m->nbEBands]);
         for (j=eBands[i];j<eBands[i+1];j++)
            X[j*C+c] = freq[j+c*N]*g;
      }
   }
}

#endif /* FIXED_POINT */

#ifndef DISABLE_STEREO
void renormalise_bands(const CELTMode *m, celt_norm_t * restrict X)
{
   int i, c;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   for (c=0;c<C;c++)
   {
      i=0; do {
         renormalise_vector(X+C*eBands[i]+c, QCONST16(0.70711f, 15), eBands[i+1]-eBands[i], C);
      } while (++i<m->nbEBands);
   }
}
#endif /* DISABLE_STEREO */

/* De-normalise the energy to produce the synthesis from the unit-energy bands */
void denormalise_bands(const CELTMode *m, const celt_norm_t * restrict X, celt_sig_t * restrict freq, const celt_ener_t *bank)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   if (C>2)
      celt_fatal("denormalise_bands() not implemented for >2 channels");
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word32_t g = SHR32(bank[i+c*m->nbEBands],1);
         j=eBands[i]; do {
            freq[j+c*N] = SHL32(MULT16_32_Q15(X[j*C+c], g),2);
         } while (++j<eBands[i+1]);
      }
      for (i=eBands[m->nbEBands];i<eBands[m->nbEBands+1];i++)
         freq[i+c*N] = 0;
   }
}

int compute_new_pitch(const CELTMode *m, const celt_sig_t *X, const celt_sig_t *P, int norm_rate, int *gain_id)
{
   int j ;
   celt_word16_t g;
   const int C = CHANNELS(m);
   celt_word32_t Sxy=0, Sxx=0, Syy=0;
   int len = 20*C;
#ifdef FIXED_POINT
   int shift = 0;
   celt_word32_t maxabs=0;
   for (j=0;j<len;j++)
   {
      maxabs = MAX32(maxabs, ABS32(X[j]));
      maxabs = MAX32(maxabs, ABS32(P[j]));
   }
   shift = celt_ilog2(maxabs)-12;
   if (shift<0)
      shift = 0;
#endif
   for (j=0;j<len;j++)
   {
      celt_word16_t gg = Q15ONE-DIV32_16(MULT16_16(Q15ONE,j),len);
      celt_word16_t Xj, Pj;
      Xj = EXTRACT16(SHR32(X[j], shift));
      Pj = MULT16_16_P15(gg,EXTRACT16(SHR32(P[j], shift)));
      Sxy = MAC16_16(Sxy, Xj, Pj);
      Sxx = MAC16_16(Sxx, Pj, Pj);
      Syy = MAC16_16(Syy, Xj, Xj);
   }
#ifdef FIXED_POINT
   {
      celt_word32_t num, den;
      celt_word16_t fact;
      fact = MULT16_16(QCONST16(.04, 14), norm_rate);
      if (fact < QCONST16(1., 14))
         fact = QCONST16(1., 14);
      num = Sxy;
      den = EPSILON+Sxx+MULT16_32_Q15(QCONST16(.03,15),Syy);
      shift = celt_ilog2(Sxy)-16;
      if (shift < 0)
         shift = 0;
      g = DIV32(SHL32(SHR32(num,shift),14),SHR32(den,shift));
      if (Sxy < MULT16_32_Q15(fact, MULT16_16(celt_sqrt(EPSILON+Sxx),celt_sqrt(EPSILON+Syy))))
         g = 0;
      /* This MUST round down so that we don't over-estimate the gain */
      *gain_id = EXTRACT16(SHR32(MULT16_16(20,(g-QCONST16(.5,14))),14));
   }
#else
   {
      float fact = .04*norm_rate;
      if (fact < 1)
         fact = 1;
      g = Sxy/(.1+Sxx+.03*Syy);
      if (Sxy < .5*fact*celt_sqrt(1+Sxx*Syy))
         g = 0;
      /* This MUST round down so that we don't over-estimate the gain */
      *gain_id = floor(20*(g-.5));
   }
#endif
   if (*gain_id < 0)
   {
      *gain_id = 0;
      return 0;
   } else {
      if (*gain_id > 15)
         *gain_id = 15;
      return 1;
   }
}

void apply_new_pitch(const CELTMode *m, celt_sig_t *X, const celt_sig_t *P, int gain_id, int pred)
{
   int j;
   celt_word16_t gain;
   const int C = CHANNELS(m);
   int len = 20*C;
   gain = ADD16(QCONST16(.5,14), MULT16_16_16(QCONST16(.05,14),gain_id));
   if (pred)
      gain = -gain;
   for (j=0;j<len;j++)
   {
      celt_word16_t gg = SUB16(gain, DIV32_16(MULT16_16(gain,j),len));
      X[j] += SHL(MULT16_32_Q15(gg,P[j]),1);
   }
}

void recombine_decisions(const CELTMode *m, celt_sig_t *X, int *flags)
{
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   const celt_word16_t f = QCONST16(0.7071068f,16);
   for (c=0;c<C;c++)
   {
      /* FIXME: For frame sizes with more than 2 MDCTs, apply a DCT-IV or something similar */
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         celt_word32_t sum1=0, sum2=0;
         for (j=eBands[i];j<eBands[i+1];j+=2)
         {
            celt_norm_t x1, x2;
            x1 = X[j*C+c];
            x2 = X[(j+1)*C+c];
            sum1 += x1*x1;
            sum2 += x2*x2;
         }
         if ((.01+sum2)/(.01+sum1) < 2)
            flags[i] = 1;
         else
            flags[i] = 0;
         //printf ("%f ", (.01+sum2)/(.01+sum1));
      }
   }
   //printf ("\n");
}

void recombine_bands(const CELTMode *m, celt_sig_t *X, int dir, int *flags)
{
   //return;
   int i, c, N;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   N = FRAMESIZE(m);
   const celt_word16_t f = QCONST16(0.7071068f,16);
   for (c=0;c<C;c++)
   {
      for (i=0;i<m->nbEBands;i++)
      {
         int j;
         if (flags[i]==0)
            continue;
         for (j=eBands[i];j<eBands[i+1];j+=2)
         {
            celt_norm_t x1, x2;
            x1 = X[j*C+c];
            x2 = X[(j+1)*C+c];
            X[j*C+c] = MULT16_16_Q15(f,x1) + MULT16_16_Q15(f,x2);
            X[(j+1)*C+c] = MULT16_16_Q15(f,x1) + MULT16_16_Q15(-f,x2);
         }
      }
   }
}

#ifndef DISABLE_STEREO

static void stereo_band_mix(const CELTMode *m, celt_norm_t *X, const celt_ener_t *bank, int stereo_mode, int bandID, int dir)
{
   int i = bandID;
   const celt_int16_t *eBands = m->eBands;
   const int C = CHANNELS(m);
   int j;
   celt_word16_t a1, a2;
   if (stereo_mode==0)
   {
      /* Do mid-side when not doing intensity stereo */
      a1 = QCONST16(.70711f,14);
      a2 = dir*QCONST16(.70711f,14);
   } else {
      celt_word16_t left, right;
      celt_word16_t norm;
#ifdef FIXED_POINT
      int shift = celt_zlog2(MAX32(bank[i], bank[i+m->nbEBands]))-13;
#endif
      left = VSHR32(bank[i],shift);
      right = VSHR32(bank[i+m->nbEBands],shift);
      norm = EPSILON + celt_sqrt(EPSILON+MULT16_16(left,left)+MULT16_16(right,right));
      a1 = DIV32_16(SHL32(EXTEND32(left),14),norm);
      a2 = dir*DIV32_16(SHL32(EXTEND32(right),14),norm);
   }
   for (j=eBands[i];j<eBands[i+1];j++)
   {
      celt_norm_t r, l;
      l = X[j*C];
      r = X[j*C+1];
      X[j*C] = MULT16_16_Q14(a1,l) + MULT16_16_Q14(a2,r);
      X[j*C+1] = MULT16_16_Q14(a1,r) - MULT16_16_Q14(a2,l);
   }
}


void interleave(celt_norm_t *x, int N)
{
   int i;
   VARDECL(celt_norm_t, tmp);
   SAVE_STACK;
   ALLOC(tmp, N, celt_norm_t);
   
   for (i=0;i<N;i++)
      tmp[i] = x[i];
   for (i=0;i<N>>1;i++)
   {
      x[i<<1] = tmp[i];
      x[(i<<1)+1] = tmp[i+(N>>1)];
   }
   RESTORE_STACK;
}

void deinterleave(celt_norm_t *x, int N)
{
   int i;
   VARDECL(celt_norm_t, tmp);
   SAVE_STACK;
   ALLOC(tmp, N, celt_norm_t);
   
   for (i=0;i<N;i++)
      tmp[i] = x[i];
   for (i=0;i<N>>1;i++)
   {
      x[i] = tmp[i<<1];
      x[i+(N>>1)] = tmp[(i<<1)+1];
   }
   RESTORE_STACK;
}

#endif /* DISABLE_STEREO */

int folding_decision(const CELTMode *m, celt_norm_t *X, celt_word16_t *average, int *last_decision)
{
   int i;
   int NR=0;
   celt_word32_t ratio = EPSILON;
   const celt_int16_t * restrict eBands = m->eBands;
   for (i=0;i<m->nbEBands;i++)
   {
      int j, N;
      int max_i=0;
      celt_word16_t max_val=EPSILON;
      celt_word32_t floor_ener=EPSILON;
      celt_norm_t * restrict x = X+eBands[i];
      N = eBands[i+1]-eBands[i];
      for (j=0;j<N;j++)
      {
         if (ABS16(x[j])>max_val)
         {
            max_val = ABS16(x[j]);
            max_i = j;
         }
      }
#if 0
      for (j=0;j<N;j++)
      {
         if (abs(j-max_i)>2)
            floor_ener += x[j]*x[j];
      }
#else
      floor_ener = QCONST32(1.,28)-MULT16_16(max_val,max_val);
      if (max_i < N-1)
         floor_ener -= MULT16_16(x[max_i+1], x[max_i+1]);
      if (max_i < N-2)
         floor_ener -= MULT16_16(x[max_i+2], x[max_i+2]);
      if (max_i > 0)
         floor_ener -= MULT16_16(x[max_i-1], x[max_i-1]);
      if (max_i > 1)
         floor_ener -= MULT16_16(x[max_i-2], x[max_i-2]);
      floor_ener = MAX32(floor_ener, EPSILON);
#endif
      if (N>7 && eBands[i] >= m->pitchEnd)
      {
         celt_word16_t r;
         celt_word16_t den = celt_sqrt(floor_ener);
         den = MAX32(QCONST16(.02, 15), den);
         r = DIV32_16(SHL32(EXTEND32(max_val),8),den);
         ratio = ADD32(ratio, EXTEND32(r));
         NR++;
      }
   }
   if (NR>0)
      ratio = DIV32_16(ratio, NR);
   ratio = ADD32(HALF32(ratio), HALF32(*average));
   if (!*last_decision)
   {
      *last_decision = (ratio < QCONST16(1.8,8));
   } else {
      *last_decision = (ratio < QCONST16(3.,8));
   }
   *average = EXTRACT16(ratio);
   return *last_decision;
}

/* Quantisation of the residual */
void quant_bands(const CELTMode *m, celt_norm_t * restrict X, celt_norm_t *P, celt_mask_t *W, int pitch_used, celt_pgain_t *pgains, const celt_ener_t *bandE, int *pulses, int shortBlocks, int fold, int total_bits, ec_enc *enc)
{
   int i, j, remaining_bits, balance;
   const celt_int16_t * restrict eBands = m->eBands;
   celt_norm_t * restrict norm;
   VARDECL(celt_norm_t, _norm);
   int B;
   SAVE_STACK;

   B = shortBlocks ? m->nbShortMdcts : 1;
   ALLOC(_norm, eBands[m->nbEBands+1], celt_norm_t);
   norm = _norm;

   balance = 0;
   for (i=0;i<m->nbEBands;i++)
   {
      int tell;
      int N;
      int q;
      celt_word16_t n;
      const celt_int16_t * const *BPbits;
      
      int curr_balance, curr_bits;
      
      N = eBands[i+1]-eBands[i];
      BPbits = m->bits;

      tell = ec_enc_tell(enc, BITRES);
      if (i != 0)
         balance -= tell;
      remaining_bits = (total_bits<<BITRES)-tell-1;
      curr_balance = (m->nbEBands-i);
      if (curr_balance > 3)
         curr_balance = 3;
      curr_balance = balance / curr_balance;
      q = bits2pulses(m, BPbits[i], N, pulses[i]+curr_balance);
      curr_bits = pulses2bits(BPbits[i], N, q);
      remaining_bits -= curr_bits;
      while (remaining_bits < 0 && q > 0)
      {
         remaining_bits += curr_bits;
         q--;
         curr_bits = pulses2bits(BPbits[i], N, q);
         remaining_bits -= curr_bits;
      }
      balance += pulses[i] + tell;
      
      n = SHL16(celt_sqrt(eBands[i+1]-eBands[i]),11);

      if (q > 0)
      {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_quant(X+eBands[i], eBands[i+1]-eBands[i], q, spread, enc);
      } else {
         intra_fold(m, X+eBands[i], eBands[i+1]-eBands[i], norm, X+eBands[i], eBands[i], B);
      }
      for (j=eBands[i];j<eBands[i+1];j++)
         norm[j] = MULT16_16_Q15(n,X[j]);
   }
   RESTORE_STACK;
}

#ifndef DISABLE_STEREO

void quant_bands_stereo(const CELTMode *m, celt_norm_t * restrict X, celt_norm_t *P, celt_mask_t *W, int pitch_used, celt_pgain_t *pgains, const celt_ener_t *bandE, int *pulses, int shortBlocks, int fold, int total_bits, ec_enc *enc)
{
   int i, j, remaining_bits, balance;
   const celt_int16_t * restrict eBands = m->eBands;
   celt_norm_t * restrict norm;
   VARDECL(celt_norm_t, _norm);
   const int C = CHANNELS(m);
   int pband=-1;
   int B;
   celt_word16_t mid, side;
   SAVE_STACK;

   B = shortBlocks ? m->nbShortMdcts : 1;
   ALLOC(_norm, C*eBands[m->nbEBands+1], celt_norm_t);
   norm = _norm;

   balance = 0;
   for (i=0;i<m->nbEBands;i++)
   {
      int c;
      int tell;
      int q1, q2;
      celt_word16_t n;
      const celt_int16_t * const *BPbits;
      int b, qb;
      int N;
      int curr_balance, curr_bits;
      int imid, iside, itheta;
      int mbits, sbits, delta;
      int qalloc;
      
      BPbits = m->bits;

      N = eBands[i+1]-eBands[i];
      tell = ec_enc_tell(enc, BITRES);
      if (i != 0)
         balance -= tell;
      remaining_bits = (total_bits<<BITRES)-tell-1;
      curr_balance = (m->nbEBands-i);
      if (curr_balance > 3)
         curr_balance = 3;
      curr_balance = balance / curr_balance;
      b = IMIN(remaining_bits+1,pulses[i]+curr_balance);
      if (b<0)
         b = 0;

      qb = (b-2*(N-1)*(QTHETA_OFFSET-log2_frac(N,BITRES)))/(32*(N-1));
      if (qb > (b>>BITRES)-1)
         qb = (b>>BITRES)-1;
      if (qb<0)
         qb = 0;
      if (qb>14)
         qb = 14;

      stereo_band_mix(m, X, bandE, qb==0, i, 1);

      mid = renormalise_vector(X+C*eBands[i], Q15ONE, N, C);
      side = renormalise_vector(X+C*eBands[i]+1, Q15ONE, N, C);
#ifdef FIXED_POINT
      itheta = MULT16_16_Q15(QCONST16(0.63662,15),celt_atan2p(side, mid));
#else
      itheta = floor(.5+16384*0.63662*atan2(side,mid));
#endif
      qalloc = log2_frac((1<<qb)+1,BITRES);
      if (qb==0)
      {
         itheta=0;
      } else {
         int shift;
         shift = 14-qb;
         itheta = (itheta+(1<<shift>>1))>>shift;
         ec_enc_uint(enc, itheta, (1<<qb)+1);
         itheta <<= shift;
      }
      if (itheta == 0)
      {
         imid = 32767;
         iside = 0;
         delta = -10000;
      } else if (itheta == 16384)
      {
         imid = 0;
         iside = 32767;
         delta = 10000;
      } else {
         imid = bitexact_cos(itheta);
         iside = bitexact_cos(16384-itheta);
         delta = (N-1)*(log2_frac(iside,BITRES+2)-log2_frac(imid,BITRES+2))>>2;
      }
      n = SHL16(celt_sqrt((eBands[i+1]-eBands[i])),11);

      if (N==2)
      {
         int c2;
         int sign=1;
         celt_norm_t v[2], w[2];
         celt_norm_t *x2 = X+C*eBands[i];
         mbits = b-qalloc;
         sbits = 0;
         if (itheta != 0 && itheta != 16384)
            sbits = 1<<BITRES;
         mbits -= sbits;
         c = itheta > 8192 ? 1 : 0;
         c2 = 1-c;

         v[0] = x2[c];
         v[1] = x2[c+C];
         w[0] = x2[c2];
         w[1] = x2[c2+C];
         q1 = bits2pulses(m, BPbits[i], N, mbits);
         curr_bits = pulses2bits(BPbits[i], N, q1)+qalloc+sbits;
         remaining_bits -= curr_bits;
         while (remaining_bits < 0 && q1 > 0)
         {
            remaining_bits += curr_bits;
            q1--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+qalloc;
            remaining_bits -= curr_bits;
         }

         if (q1 > 0)
         {
            int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
            alg_quant(v, N, q1, spread, enc);
         } else {
            v[0] = QCONST16(1.f, 14);
            v[1] = 0;
         }
         if (sbits)
         {
            if (v[0]*w[1] - v[1]*w[0] > 0)
               sign = 1;
            else
               sign = -1;
            ec_enc_bits(enc, sign==1, 1);
         } else {
            sign = 1;
         }
         w[0] = -sign*v[1];
         w[1] = sign*v[0];
         if (c==0)
         {
            x2[0] = v[0];
            x2[1] = v[1];
            x2[2] = w[0];
            x2[3] = w[1];
         } else {
            x2[0] = w[0];
            x2[1] = w[1];
            x2[2] = v[0];
            x2[3] = v[1];
         }
      } else {
         
      mbits = (b-qalloc/2-delta)/2;
      if (mbits > b-qalloc)
         mbits = b-qalloc;
      if (mbits<0)
         mbits=0;
      sbits = b-qalloc-mbits;
      q1 = bits2pulses(m, BPbits[i], N, mbits);
      q2 = bits2pulses(m, BPbits[i], N, sbits);
      curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
      remaining_bits -= curr_bits;
      while (remaining_bits < 0 && (q1 > 0 || q2 > 0))
      {
         remaining_bits += curr_bits;
         if (q1>q2)
         {
            q1--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
         } else {
            q2--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
         }
         remaining_bits -= curr_bits;
      }

      /* If pitch isn't available, use intra-frame prediction */
      if (q1+q2==0)
      {
         intra_fold(m, X+C*eBands[i], eBands[i+1]-eBands[i], norm, P+C*eBands[i], eBands[i], B);
         deinterleave(P+C*eBands[i], C*N);
      }
      deinterleave(X+C*eBands[i], C*N);
      if (q1 > 0) {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_quant(X+C*eBands[i], N, q1, spread, enc);
      } else
         for (j=C*eBands[i];j<C*eBands[i]+N;j++)
            X[j] = P[j];
      if (q2 > 0) {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_quant(X+C*eBands[i]+N, N, q2, spread, enc);
      } else
         for (j=C*eBands[i]+N;j<C*eBands[i+1];j++)
            X[j] = 0;
      }
      
      balance += pulses[i] + tell;

#ifdef FIXED_POINT
      mid = imid;
      side = iside;
#else
      mid = (1./32768)*imid;
      side = (1./32768)*iside;
#endif
      for (c=0;c<C;c++)
         for (j=0;j<N;j++)
            norm[C*(eBands[i]+j)+c] = MULT16_16_Q15(n,X[C*eBands[i]+c*N+j]);

      for (j=0;j<N;j++)
         X[C*eBands[i]+j] = MULT16_16_Q15(X[C*eBands[i]+j], mid);
      for (j=0;j<N;j++)
         X[C*eBands[i]+N+j] = MULT16_16_Q15(X[C*eBands[i]+N+j], side);

      interleave(X+C*eBands[i], C*N);


      stereo_band_mix(m, X, bandE, 0, i, -1);
      renormalise_vector(X+C*eBands[i], Q15ONE, N, C);
      renormalise_vector(X+C*eBands[i]+1, Q15ONE, N, C);
   }
   RESTORE_STACK;
}
#endif /* DISABLE_STEREO */

/* Decoding of the residual */
void unquant_bands(const CELTMode *m, celt_norm_t * restrict X, celt_norm_t *P, int pitch_used, celt_pgain_t *pgains, const celt_ener_t *bandE, int *pulses, int shortBlocks, int fold, int total_bits, ec_dec *dec)
{
   int i, j, remaining_bits, balance;
   const celt_int16_t * restrict eBands = m->eBands;
   celt_norm_t * restrict norm;
   VARDECL(celt_norm_t, _norm);
   int B;
   SAVE_STACK;

   B = shortBlocks ? m->nbShortMdcts : 1;
   ALLOC(_norm, eBands[m->nbEBands+1], celt_norm_t);
   norm = _norm;

   balance = 0;
   for (i=0;i<m->nbEBands;i++)
   {
      int tell;
      int N;
      int q;
      celt_word16_t n;
      const celt_int16_t * const *BPbits;
      
      int curr_balance, curr_bits;

      N = eBands[i+1]-eBands[i];
      BPbits = m->bits;

      tell = ec_dec_tell(dec, BITRES);
      if (i != 0)
         balance -= tell;
      remaining_bits = (total_bits<<BITRES)-tell-1;
      curr_balance = (m->nbEBands-i);
      if (curr_balance > 3)
         curr_balance = 3;
      curr_balance = balance / curr_balance;
      q = bits2pulses(m, BPbits[i], N, pulses[i]+curr_balance);
      curr_bits = pulses2bits(BPbits[i], N, q);
      remaining_bits -= curr_bits;
      while (remaining_bits < 0 && q > 0)
      {
         remaining_bits += curr_bits;
         q--;
         curr_bits = pulses2bits(BPbits[i], N, q);
         remaining_bits -= curr_bits;
      }
      balance += pulses[i] + tell;

      n = SHL16(celt_sqrt(eBands[i+1]-eBands[i]),11);

      if (q > 0)
      {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_unquant(X+eBands[i], eBands[i+1]-eBands[i], q, spread, dec);
      } else {
         intra_fold(m, X+eBands[i], eBands[i+1]-eBands[i], norm, X+eBands[i], eBands[i], B);
      }
      for (j=eBands[i];j<eBands[i+1];j++)
         norm[j] = MULT16_16_Q15(n,X[j]);
   }
   RESTORE_STACK;
}

#ifndef DISABLE_STEREO

void unquant_bands_stereo(const CELTMode *m, celt_norm_t * restrict X, celt_norm_t *P, int pitch_used, celt_pgain_t *pgains, const celt_ener_t *bandE, int *pulses, int shortBlocks, int fold, int total_bits, ec_dec *dec)
{
   int i, j, remaining_bits, balance;
   const celt_int16_t * restrict eBands = m->eBands;
   celt_norm_t * restrict norm;
   VARDECL(celt_norm_t, _norm);
   const int C = CHANNELS(m);
   int pband=-1;
   int B;
   celt_word16_t mid, side;
   SAVE_STACK;

   B = shortBlocks ? m->nbShortMdcts : 1;
   ALLOC(_norm, C*eBands[m->nbEBands+1], celt_norm_t);
   norm = _norm;

   balance = 0;
   for (i=0;i<m->nbEBands;i++)
   {
      int c;
      int tell;
      int q1, q2;
      celt_word16_t n;
      const celt_int16_t * const *BPbits;
      int b, qb;
      int N;
      int curr_balance, curr_bits;
      int imid, iside, itheta;
      int mbits, sbits, delta;
      int qalloc;
      
      BPbits = m->bits;

      N = eBands[i+1]-eBands[i];
      tell = ec_dec_tell(dec, BITRES);
      if (i != 0)
         balance -= tell;
      remaining_bits = (total_bits<<BITRES)-tell-1;
      curr_balance = (m->nbEBands-i);
      if (curr_balance > 3)
         curr_balance = 3;
      curr_balance = balance / curr_balance;
      b = IMIN(remaining_bits+1,pulses[i]+curr_balance);
      if (b<0)
         b = 0;

      qb = (b-2*(N-1)*(QTHETA_OFFSET-log2_frac(N,BITRES)))/(32*(N-1));
      if (qb > (b>>BITRES)-1)
         qb = (b>>BITRES)-1;
      if (qb>14)
         qb = 14;
      if (qb<0)
         qb = 0;
      qalloc = log2_frac((1<<qb)+1,BITRES);
      if (qb==0)
      {
         itheta=0;
      } else {
         int shift;
         shift = 14-qb;
         itheta = ec_dec_uint(dec, (1<<qb)+1);
         itheta <<= shift;
      }
      if (itheta == 0)
      {
         imid = 32767;
         iside = 0;
         delta = -10000;
      } else if (itheta == 16384)
      {
         imid = 0;
         iside = 32767;
         delta = 10000;
      } else {
         imid = bitexact_cos(itheta);
         iside = bitexact_cos(16384-itheta);
         delta = (N-1)*(log2_frac(iside,BITRES+2)-log2_frac(imid,BITRES+2))>>2;
      }
      n = SHL16(celt_sqrt((eBands[i+1]-eBands[i])),11);

      if (N==2)
      {
         int c2;
         int sign=1;
         celt_norm_t v[2], w[2];
         celt_norm_t *x2 = X+C*eBands[i];
         mbits = b-qalloc;
         sbits = 0;
         if (itheta != 0 && itheta != 16384)
            sbits = 1<<BITRES;
         mbits -= sbits;
         c = itheta > 8192 ? 1 : 0;
         c2 = 1-c;

         v[0] = x2[c];
         v[1] = x2[c+C];
         w[0] = x2[c2];
         w[1] = x2[c2+C];
         q1 = bits2pulses(m, BPbits[i], N, mbits);
         curr_bits = pulses2bits(BPbits[i], N, q1)+qalloc+sbits;
         remaining_bits -= curr_bits;
         while (remaining_bits < 0 && q1 > 0)
         {
            remaining_bits += curr_bits;
            q1--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+qalloc;
            remaining_bits -= curr_bits;
         }

         if (q1 > 0)
         {
            int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
            alg_unquant(v, N, q1, spread, dec);
         } else {
            v[0] = QCONST16(1.f, 14);
            v[1] = 0;
         }
         if (sbits)
            sign = 2*ec_dec_bits(dec, 1)-1;
         else
            sign = 1;
         w[0] = -sign*v[1];
         w[1] = sign*v[0];
         if (c==0)
         {
            x2[0] = v[0];
            x2[1] = v[1];
            x2[2] = w[0];
            x2[3] = w[1];
         } else {
            x2[0] = w[0];
            x2[1] = w[1];
            x2[2] = v[0];
            x2[3] = v[1];
         }
      } else {
      mbits = (b-qalloc/2-delta)/2;
      if (mbits > b-qalloc)
         mbits = b-qalloc;
      if (mbits<0)
         mbits=0;
      sbits = b-qalloc-mbits;
      q1 = bits2pulses(m, BPbits[i], N, mbits);
      q2 = bits2pulses(m, BPbits[i], N, sbits);
      curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
      remaining_bits -= curr_bits;
      while (remaining_bits < 0 && (q1 > 0 || q2 > 0))
      {
         remaining_bits += curr_bits;
         if (q1>q2)
         {
            q1--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
         } else {
            q2--;
            curr_bits = pulses2bits(BPbits[i], N, q1)+pulses2bits(BPbits[i], N, q2)+qalloc;
         }
         remaining_bits -= curr_bits;
      }
      


      /* If pitch isn't available, use intra-frame prediction */
      if (q1+q2==0)
      {
         intra_fold(m, X+C*eBands[i], eBands[i+1]-eBands[i], norm, P+C*eBands[i], eBands[i], B);
         deinterleave(P+C*eBands[i], C*N);
      }
      deinterleave(X+C*eBands[i], C*N);
      if (q1 > 0)
      {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_unquant(X+C*eBands[i], N, q1, spread, dec);
      } else
         for (j=C*eBands[i];j<C*eBands[i]+N;j++)
            X[j] = P[j];
      if (q2 > 0)
      {
         int spread = (eBands[i] >= m->pitchEnd && fold) ? B : 0;
         alg_unquant(X+C*eBands[i]+N, N, q2, spread, dec);
      } else
         for (j=C*eBands[i]+N;j<C*eBands[i+1];j++)
            X[j] = 0;
      /*orthogonalize(X+C*eBands[i], X+C*eBands[i]+N, N);*/
      }
      balance += pulses[i] + tell;

#ifdef FIXED_POINT
      mid = imid;
      side = iside;
#else
      mid = (1./32768)*imid;
      side = (1./32768)*iside;
#endif
      for (c=0;c<C;c++)
         for (j=0;j<N;j++)
            norm[C*(eBands[i]+j)+c] = MULT16_16_Q15(n,X[C*eBands[i]+c*N+j]);

      for (j=0;j<N;j++)
         X[C*eBands[i]+j] = MULT16_16_Q15(X[C*eBands[i]+j], mid);
      for (j=0;j<N;j++)
         X[C*eBands[i]+N+j] = MULT16_16_Q15(X[C*eBands[i]+N+j], side);

      interleave(X+C*eBands[i], C*N);

      stereo_band_mix(m, X, bandE, 0, i, -1);
      renormalise_vector(X+C*eBands[i], Q15ONE, N, C);
      renormalise_vector(X+C*eBands[i]+1, Q15ONE, N, C);
   }
   RESTORE_STACK;
}

#endif /* DISABLE_STEREO */
