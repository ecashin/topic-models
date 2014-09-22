/*
 * Sub-variable likelihood calculations
 * Copyright (C) 2013-2014 Jinjing Li and Wray Buntine
 * All rights reserved.
 *
 * This Source Code Form is subject to the terms of the Mozilla 
 * Public License, v. 2.0. If a copy of the MPL was not
 * distributed with this file, You can obtain one at
 *      http://mozilla.org/MPL/2.0/.
 *
 * Author: Jinjing Li <jinjingli@gmail.com>
 *         Wray Buntine (wray.buntine@monash.edu)
 *
 *  Specialised probability calcs for parts of the model,
 *  nothing is changed
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <time.h>

#include "yap.h"
#include "util.h"
#include "stable.h"
#include "lgamma.h"
#include "tca.h"
#include "stats.h"

/*************************************
 *
 *     more or less mirrored routines for the 
 *     wordXtopic versus docXtopic
 *
 ************************************/

/*
 *   probability of topic from doc level PDP  
 *
 *       t    - topic
 *       (i,mi)    -  word index and corresponding multi version
 *       pK   - input contribution to posterior from adding word with topic
 *       *dip - set to prob. indicator would be 1, if NULL leave
 */
double docfact(D_MiSi_t *dD, int t, int i, int mi, double pK, float *dip) {
  int N = dD->Mi[t], S = dD->Si[t];
  int n, s;
  assert(dip);
  *dip = 1;
  if ( !PCTL_BURSTY() ) 
    return pK;
  if ( M_multi(i) ) {
    int mii;
    // assert(mi<ddM.dim_multiind || did==ddN.D-1);
    mii = ddM.multiind[mi]-dD->mi_base;
    assert(mii>=0);
    assert(mii<ddM.MI_max);
    n = dD->Mik[mii][t];
    s = dD->Sik[mii][t];
  } else {
    n = s = 0;
  }  
  if ( s==0 ) {
    return pK * (ddP.b_burst+ddP.a_burst*S)/(ddP.b_burst+N); 
  } else {
    double one = pK * (ddP.b_burst+ddP.a_burst*S) * (s+1.0)/(n+1.0);
    double zero = (n-s+1.0)/(n+1.0);
    one *= S_UV(ddC.a_burst,n,s+1);
    if ( s==1 )
      zero *= n - ddP.a_burst;
    else
      zero *= S_U(ddC.a_burst,n,s);    
    *dip = one/(one + zero);
    return (one + zero) /(ddP.b_burst+N);
  }
  return 0;
}

/*
 *   counterpart to docfact()
 *   only used in estimation, (ddP.bdk!=NULL version)
 */
double docprob(D_MiSi_t *dD, int t, int i, int mi, double pw) {
  int N = dD->Mi[t], S = dD->Si[t];
  int n, s;
  if ( M_multi(i) ) {
    int mii;
    // assert(mi<ddM.dim_multiind || did==ddN.D-1);
    mii = ddM.multiind[mi]-dD->mi_base;
    assert(mii>=0);
    assert(mii<ddM.MI_max);
    n = dD->Mik[mii][t];
    s = dD->Sik[mii][t];
  } else {
    n = s = 0;
  }  
  if ( s==0 ) {
    return pw * (ddP.b_burst+ddP.a_burst*S)/(ddP.b_burst+N); 
  } 
  return (pw * (ddP.b_burst+ddP.a_burst*S) + (n-ddP.a_burst*s))
    /(ddP.b_burst + N);
}


//binomal (log)
double log_binom(int a, int b){
  assert(b>=a);
  int i,s;
  long result=0;
  if (b>a) {
        s=a;
  } else
        s=b;
  for (i=1; i<=s ;i++) {
    result += log(b-i+1);
    result -= log(i);
  }
  return result;
}

/*
 *    the various node probabilities for
 *           VAR_one_fact()  --  multiplicity is incremented
 *           VAR_zero_fact() --   multiplicity is unchanged
 *    have special handling for when n==0
 */
static double theta_one_fact(int d, int t) {
  int n = ddS.n_dt[d][t];
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.c_dt[d][t];
    if ( c<=0 ) 
      c=1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_theta, n, c+1) * (c+1.0)/(n+1);
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact * 
    (ddP.b_theta + ddP.a_theta * ddS.C_dT[d]) / (ddP.b_theta + ddS.N_dT[d]);
}
static double theta_zero_fact(int d, int t) {
  int n = ddS.n_dt[d][t];
  int c;
  if ( n==0 )
    return 0;
  c = ddS.c_dt[d][t];
  if ( c<=0 ) 
    c=1;
  return 
    ((n==1)?((1-ddP.a_theta)/2.0):(S_U(ddC.a_theta, n, c) * (n-c+1.0)/(n+1)))
    / (ddP.b_theta + ddS.N_dT[d]);
}
/*
 * reorganises call to save on vector lookups
 *  BUT testing show its 10% slower with mufact
 */
// #define mufact
#ifdef mufact
//    Z += mu_fact(e,t, &Y);
static double mu_fact(int e, int t, double *Y) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  double denom = (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
  double renum = (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]);
  if ( n==0 ) {
    *Y *= renum/denom;
    return 0;
  } else {
    double fact = 1.0;
    int c = ddS.cp_et[e][t];
    double oldY = *Y;
    if ( c<=0 ) c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_mu, n, c+1) * (c+1.0)/(n+1);    
    else if (n==c+1)
      fact = n/(n-1.0);
    *Y *= fact * renum/denom;
    return oldY * ((n==1)?((1-ddP.a_mu)/2.0):(S_U(ddC.a_mu, n, c) * (n-c+1.0)/(n+1)))
      / denom;
  }
}
#else
static double mu_one_fact(int e, int t) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.cp_et[e][t];
    if ( c<=0 )
      c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_mu, n, c+1) * (c+1.0)/(n+1);    
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact 
    * (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]) 
    / (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
}
static double mu_zero_fact(int e, int t) {
  int n = ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0);
  int c;
  if ( n==0 )
    return 0;
  c = ddS.cp_et[e][t];
  if ( c<=0 )
    c = 1;
  return
    ((n==1)?((1-ddP.a_mu)/2.0):(S_U(ddC.a_mu, n, c) * (n-c+1.0)/(n+1))) 
    / (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
}
#endif
static double mu0_prob(int t) {
  return (ddS.cp_et[0][t] + ddP.b_mu0/ddN.T)/(ddS.Cp_e[0]+ddP.b_mu0);
}
/*
 * reorganises call to save on vector lookups
 */
#define phifact
#ifdef phifact
//    Z += phi_fact(e,v,t, &Y);
static double phi_fact(int e, int v, int t, double *Y) {
  int n = ddS.m_evt[e][v][t] + ((e<ddN.E-1)?ddS.s_evt[e+1][v][t]:0);
  double denom = (ddP.b_phi[e][t] + ddS.M_eVt[e][t] 
                  + ((e<ddN.E-1)?ddS.S_eVt[e+1][t]:0));
  double renum = (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_eVt[e][t]);
  if ( n==0 ) {
    *Y *= renum/denom;
    return 0;
  } else {  
    int c;
    double fact = 1.0;
    double oldY = *Y;
    c = ddS.s_evt[e][v][t];
    //  repair inconsistency errors
    if ( c<=0 ) c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_phi1, n, c+1) * (c+1.0)/(n+1);
    else if (n==c+1)
      fact = n/(n-1.0);
    *Y *= fact * renum/denom;
    return oldY * S_U(ddC.a_phi1, n, c) * (n-c+1.0)/(n+1) /denom;
  }
}
#else
//    Z += Y * phi_zero_fact(e, v, t);
//    Y *= phi_one_fact(e, v, t);
static double phi_one_fact(int e, int v, int t) {
  int n = ddS.m_evt[e][v][t] + ((e<ddN.E-1)?ddS.s_evt[e+1][v][t]:0);
  double fact = 1;
  if ( n>0 ) {
    int c = ddS.s_evt[e][v][t];
    if ( c<=0 )
      c = 1;
    if ( n>c+1 )
      fact = S_UV(ddC.a_phi1, n, c+1) * (c+1.0)/(n+1);
    else if (n==c+1)
      fact = n/(n-1.0);
  }
  return fact
    * (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_eVt[e][t])
    / (ddP.b_phi[e][t] + ddS.M_eVt[e][t] + ((e<ddN.E-1)?ddS.S_eVt[e+1][t]:0));
}
static double phi_zero_fact(int e, int v, int t) {
  int n = ddS.m_evt[e][v][t] + ((e<ddN.E-1)?ddS.s_evt[e+1][v][t]:0);
  int c;
  if ( n==0 )
    return 0;
  c = ddS.s_evt[e][v][t];
  if ( c<=0 )
    c = 1;
  return 
    S_U(ddC.a_phi1, n, c) * (n-c+1.0)/(n+1) 
    / (ddP.b_phi[e][t] + ddS.M_eVt[e][t] + ((e<ddN.E-1)?ddS.S_eVt[e+1][t]:0));
}
#endif
double phi0_prob(int v) {
  double term;
  if ( ddS.S_0vT[v]==0 )
    /*   spread the zero weight over all zero */
    term = (ddP.b_phi0 + ddP.a_phi0*ddS.S_0_nz)
      /(ddN.W-ddS.S_0_nz);
  else
    term = ddS.S_0vT[v] - ddP.a_phi0;
  return term/(ddP.b_phi0 + ddS.S_0);
}

/*
 *   normally the normaliser term is forgotten for topicfact(),
 *   but we need to add it in when doing the doc PYP
 */
double topicnorm(int d) {
  return ((double)ddS.N_dT[d]+ddP.b_theta);
}

/*
 *    mu_prob() but do one epoch at a time
 */
void mu_prob_iter(int e, double *vec) {
  int t;
  if ( e<0 ) {
    for (t=0; t<ddN.T; t++)
      vec[t] = mu0_prob(t);
  } else {
    double norm = (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
    double pwght = (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e]);
    for (t=0; t<ddN.T; t++) {
      vec[t] =  ( (ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0)
                   - ddP.a_mu * ddS.cp_et[e][t]) + pwght*vec[t] ) /norm;
    }
  }
}

static double mu_prob(int e, int t) { 
  double prob;
  if ( ddP.mu )
    return ddP.mu[e][t];
  if ( e==0 )
    prob = mu0_prob(t);
  else
    prob = mu_prob(e-1,t);
  return ( (ddS.C_eDt[e][t] + ((e<ddN.E-1)?ddS.cp_et[e+1][t]:0)
	    - ddP.a_mu * ddS.cp_et[e][t])
	   + (ddP.b_mu[e] + ddP.a_mu * ddS.Cp_e[e])*prob )
    / (ddP.b_mu[e] + ddS.C_e[e] + ((e<ddN.E-1)?ddS.Cp_e[e+1]:0));
}

double word_side_prob(int e, int v, int t) { 
  double prob;
  if ( ddP.phi )
    return ddP.phi[e][v][t];
  if ( e==0 )
    prob = phi0_prob(v);
  else
    prob = word_side_prob(e-1,v,t);
  return ( (ddS.m_evt[e][v][t] + ((e<ddN.E-1)?ddS.s_evt[e+1][v][t]:0)
	    - ddP.a_phi1 * ddS.s_evt[e][v][t])
	   + (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_eVt[e][t])*prob )
    / (ddP.b_phi[e][t] + ddS.M_eVt[e][t] + ((e<ddN.E-1)?ddS.S_eVt[e+1][t]:0));					    
}

/*
 *     word_side_prob() but done iteratively by epoch
 */
void phi_prob_iter(int e, double **mtx) { 
  int v, t;
  if ( e<0 ) {
    for (v=0; v<ddN.W; v++)
      for (t=0; t<ddN.T; t++)
        mtx[v][t] = phi0_prob(v);
    return;
  } else {
    double norm[ddN.T];
    double wght[ddN.T];
    for (t=0; t<ddN.T; t++) {
      norm[t] = (ddP.b_phi[e][t] + ddS.M_eVt[e][t] 
                 + ((e<ddN.E-1)?ddS.S_eVt[e+1][t]:0));
      wght[t] = (ddP.b_phi[e][t] + ddP.a_phi1 * ddS.S_eVt[e][t]);
    }
    for (v=0; v<ddN.W; v++)
      for (t=0; t<ddN.T; t++) 
        mtx[v][t] = ( (ddS.m_evt[e][v][t] + ((e<ddN.E-1)?ddS.s_evt[e+1][v][t]:0)
                       - ddP.a_phi1 * ddS.s_evt[e][v][t]) + wght[t]*mtx[v][t] )
          / norm[t];
  }
} 

double doc_side_prob(int d, int t) { 
  int e = ddD.e[d];
  double prob = mu_prob(e, t);
  return ((ddS.n_dt[d][t]-ddP.a_theta*ddS.c_dt[d][t]) 
	  + (ddP.b_theta + ddP.a_theta * ddS.C_dT[d])*prob);
}

/*
 *   return count to place table back:
 *     -1 = no contribution to current alpha stats
 *     0 = no table back, just to current alpha
 *     1 = back to previous time
 *     e = back to initial epoch
 *   e+1 = back to root
 *
 *   cuts short if ddP.back in order, so
 *   return is >= ddP.back
 */
int doc_side_ind(int d, int t) {  
  double Z = 0;
  int e = ddD.e[d];
  double Y = 1;
  double Ze[ddN.E+1];
  int i;

  if ( ddP.mu ) {
    double Z0 = theta_zero_fact(d,t);
    Z = Z0 + theta_one_fact(d,t) * ddP.mu[e][t];
    if ( Z0 > rng_unit(rngp)*Z ) 
      return -1;
    return 0;
  }

  for (i=e ; i>=0; i--) {
#ifdef mufact
    Z += mu_fact(i, t, &Y);
#else
    Z += Y * mu_zero_fact(i, t);
    Y *= mu_one_fact(i, t);
#endif
    Ze[i+1] = Z;
    /*   cannot break if zeros so back is forced!  */
    if ( i<=e-ddP.back && ddS.cp_et[i][t]>0 ) break;
  }
  if ( i<0 ) {
    Z += Y * mu0_prob(t);
    Ze[0] = Z;
  }
  Z += theta_zero_fact(d,t)/theta_one_fact(d,t);
  Z *= rng_unit(rngp);
  if ( Z>Ze[i+1] )
    return -1;
  i++;
  for ( ; i<=e; i++) {
    if ( Z>Ze[i+1] )
      return e-i+1;
  }
  return 0;
}

// calculate p(z=t) from doc side using r1 (\sum_r1{P(z=t,r1)})
double doc_side_fact (int d, int t) {
  double Z = 0;
  int e = ddD.e[d];
  double Y = 1;

  if ( ddP.mu )
    Z = ddP.mu[e][t];
  else {
    for ( ; e>=0; e--) {
#ifdef mufact
      Z += mu_fact(e, t, &Y);
#else
      Z += Y * mu_zero_fact(e, t);
      Y *= mu_one_fact(e, t);
#endif      
    }
    Z += Y * mu0_prob(t);
  }
  return theta_zero_fact(d,t) + theta_one_fact(d,t) * Z;
}
/*
 *   return count to place table back:
 *     0 = no table back
 *     1 = back to previous time
 *     e = back to initial epoch
 *   e+1 = back to root
 *
 *   cuts short if ddP.back in order, so
 *   return is >= ddP.back
 */
int word_side_ind ( int e, int v, int t) {
  double Z = 0;
  double Y = 1;
  double Ze[ddN.E];
  int i;

  if ( ddP.phi )
    return 0;

  for (i=e ; i>=0; i--) {
#ifdef phifact
    Z += phi_fact(e,v,t, &Y);
#else
    Z += Y * phi_zero_fact(e, v, t);
    Y *= phi_one_fact(e, v, t);
#endif
    Ze[i] = Z;
    /*   cannot break if zeros so back is forced!  */
    if ( i<=e-ddP.back && ddS.s_evt[i][v][t]>0 ) break;
  }
  if ( i<0 ) {
    Z += Y * phi0_prob(v);
  }
  i++;
  Z *= rng_unit(rngp);
  for ( ; i<=e; i++) {
    if ( Z>Ze[i] )
      return e-i+1;
  }
  return 0;
}

double word_side_fact ( int e, int v, int t) {
  double Z = 0;
  double Y = 1;

  if ( ddP.phi )
    return ddP.phi[e][v][t];

  for ( ; e>=0; e--) {
#ifdef phifact
    Z += phi_fact(e,v,t, &Y);
#else
    Z += Y * phi_zero_fact(e, v, t);
    Y *= phi_one_fact(e, v, t);
#endif
  }
  Z += Y * phi0_prob(v);
  return Z;
}








