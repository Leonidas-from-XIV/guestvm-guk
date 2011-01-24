/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
/*****************************************************************************/
/* Port of pseudo random number generator found at:
 * http://www.bedaux.net/mtrand/ */
/*****************************************************************************/
/*
  Author: Grzegorz Milos
 */

#include <types.h>
#include <lib.h>

#define STATE_SIZE   624
#define M_CONST      397
u32 state[STATE_SIZE];
u32 p;

void seed(u32 s) 
{
    int i;
    
    memset(state, 0, sizeof(u32) * STATE_SIZE); 
    state[0] = s; 
    for (i = 1; i < STATE_SIZE; ++i) 
    {
        state[i] = 1812433253UL * (state[i - 1] ^ (state[i - 1] >> 30)) + i;
    }
    p = STATE_SIZE; // force gen_state() to be called for next random number
}

u32 inline twiddle(u32 u, u32 v) 
{
  return (((u & 0x80000000UL) | (v & 0x7FFFFFFFUL)) >> 1)
    ^ ((v & 1UL) ? 0x9908B0DFUL : 0x0UL);
}

void gen_state(void) 
{ 
    int i;

    for (i = 0; i < (STATE_SIZE - M_CONST); ++i)
        state[i] = state[i + M_CONST] ^ twiddle(state[i], state[i + 1]);
    for (i = STATE_SIZE - M_CONST; i < (STATE_SIZE - 1); ++i)
        state[i] = state[i + M_CONST - STATE_SIZE] ^ 
                   twiddle(state[i], state[i + 1]);
    state[STATE_SIZE - 1] = state[M_CONST - 1] ^ 
                            twiddle(state[STATE_SIZE - 1], state[0]);
    p = 0; // reset position
}

u32 rand_int(void) 
{
  u32 x;
  
  if (p == STATE_SIZE) gen_state(); // new state vector needed
  x = state[p++];
  x ^= (x >> 11);
  x ^= (x << 7) & 0x9D2C5680UL;
  x ^= (x << 15) & 0xEFC60000UL;

  return x ^ (x >> 18);
}

