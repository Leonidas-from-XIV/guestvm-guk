/*
 * Copyright (c) 2009 Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, California 95054, U.S.A. All rights reserved.
 * 
 * U.S. Government Rights - Commercial software. Government users are
 * subject to the Sun Microsystems, Inc. standard license agreement and
 * applicable provisions of the FAR and its supplements.
 * 
 * Use is subject to license terms.
 * 
 * This distribution may include materials developed by third parties.
 * 
 * Parts of the product may be derived from Berkeley BSD systems,
 * licensed from the University of California. UNIX is a registered
 * trademark in the U.S.  and in other countries, exclusively licensed
 * through X/Open Company, Ltd.
 * 
 * Sun, Sun Microsystems, the Sun logo and Java are trademarks or
 * registered trademarks of Sun Microsystems, Inc. in the U.S. and other
 * countries.
 * 
 * This product is covered and controlled by U.S. Export Control laws and
 * may be subject to the export or import laws in other
 * countries. Nuclear, missile, chemical biological weapons or nuclear
 * maritime end uses or end users, whether direct or indirect, are
 * strictly prohibited. Export or reexport to countries subject to
 * U.S. embargo or to entities identified on U.S. export exclusion lists,
 * including, but not limited to, the denied persons and specially
 * designated nationals lists is strictly prohibited.
 * 
 */
/*****************************************************************************/
/* Port of pseudo random number generator found at:
 * http://www.bedaux.net/mtrand/ */
/*****************************************************************************/
/*
  Author: Grzegorz Milos, Sun Microsystems, Inc., summer intern 2007.
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

