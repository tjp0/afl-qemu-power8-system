/*
 * MIPS ASE DSP Instruction emulation helpers for QEMU.
 *
 * Copyright (c) 2012  Jia Liu <proljc@gmail.com>
 *                     Dongxue Zhang <elat.era@gmail.com>
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "helper.h"

/*** MIPS DSP internal functions begin ***/
#define MIPSDSP_ABS(x) (((x) >= 0) ? x : -x)
#define MIPSDSP_OVERFLOW(a, b, c, d) (!(!((a ^ b ^ -1) & (a ^ c) & d)))

static inline void set_DSPControl_overflow_flag(uint32_t flag, int position,
                                                CPUMIPSState *env)
{
    env->active_tc.DSPControl |= (target_ulong)flag << position;
}

static inline void set_DSPControl_carryflag(uint32_t flag, CPUMIPSState *env)
{
    env->active_tc.DSPControl |= (target_ulong)flag << 13;
}

static inline uint32_t get_DSPControl_carryflag(CPUMIPSState *env)
{
    return (env->active_tc.DSPControl >> 13) & 0x01;
}

static inline void set_DSPControl_24(uint32_t flag, int len, CPUMIPSState *env)
{
  uint32_t filter;

  filter = ((0x01 << len) - 1) << 24;
  filter = ~filter;

  env->active_tc.DSPControl &= filter;
  env->active_tc.DSPControl |= (target_ulong)flag << 24;
}

static inline uint32_t get_DSPControl_24(int len, CPUMIPSState *env)
{
  uint32_t filter;

  filter = (0x01 << len) - 1;

  return (env->active_tc.DSPControl >> 24) & filter;
}

static inline void set_DSPControl_pos(uint32_t pos, CPUMIPSState *env)
{
    target_ulong dspc;

    dspc = env->active_tc.DSPControl;
#ifndef TARGET_MIPS64
    dspc = dspc & 0xFFFFFFC0;
    dspc |= pos;
#else
    dspc = dspc & 0xFFFFFF80;
    dspc |= pos;
#endif
    env->active_tc.DSPControl = dspc;
}

static inline uint32_t get_DSPControl_pos(CPUMIPSState *env)
{
    target_ulong dspc;
    uint32_t pos;

    dspc = env->active_tc.DSPControl;

#ifndef TARGET_MIPS64
    pos = dspc & 0x3F;
#else
    pos = dspc & 0x7F;
#endif

    return pos;
}

static inline void set_DSPControl_efi(uint32_t flag, CPUMIPSState *env)
{
    env->active_tc.DSPControl &= 0xFFFFBFFF;
    env->active_tc.DSPControl |= (target_ulong)flag << 14;
}

#define DO_MIPS_SAT_ABS(size)                                          \
static inline int##size##_t mipsdsp_sat_abs##size(int##size##_t a,         \
                                                  CPUMIPSState *env)   \
{                                                                      \
    if (a == INT##size##_MIN) {                                        \
        set_DSPControl_overflow_flag(1, 20, env);                      \
        return INT##size##_MAX;                                        \
    } else {                                                           \
        return MIPSDSP_ABS(a);                                         \
    }                                                                  \
}
DO_MIPS_SAT_ABS(8)
DO_MIPS_SAT_ABS(16)
DO_MIPS_SAT_ABS(32)
#undef DO_MIPS_SAT_ABS

/* get sum value */
static inline int16_t mipsdsp_add_i16(int16_t a, int16_t b, CPUMIPSState *env)
{
    int16_t tempI;

    tempI = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempI, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempI;
}

static inline int16_t mipsdsp_sat_add_i16(int16_t a, int16_t b,
                                          CPUMIPSState *env)
{
    int16_t tempS;

    tempS = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempS, 0x8000)) {
        if (a > 0) {
            tempS = 0x7FFF;
        } else {
            tempS = 0x8000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempS;
}

static inline int32_t mipsdsp_sat_add_i32(int32_t a, int32_t b,
                                          CPUMIPSState *env)
{
    int32_t tempI;

    tempI = a + b;

    if (MIPSDSP_OVERFLOW(a, b, tempI, 0x80000000)) {
        if (a > 0) {
            tempI = 0x7FFFFFFF;
        } else {
            tempI = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return tempI;
}

static inline uint8_t mipsdsp_add_u8(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;

    if (temp & 0x0100) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFF;
}

static inline uint16_t mipsdsp_add_u16(uint16_t a, uint16_t b,
                                       CPUMIPSState *env)
{
    uint32_t temp;

    temp = (uint32_t)a + (uint32_t)b;

    if (temp & 0x00010000) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFFFF;
}

static inline uint8_t mipsdsp_sat_add_u8(uint8_t a, uint8_t b,
                                         CPUMIPSState *env)
{
    uint8_t  result;
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;
    result = temp & 0xFF;

    if (0x0100 & temp) {
        result = 0xFF;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return result;
}

static inline uint16_t mipsdsp_sat_add_u16(uint16_t a, uint16_t b,
                                           CPUMIPSState *env)
{
    uint16_t result;
    uint32_t temp;

    temp = (uint32_t)a + (uint32_t)b;
    result = temp & 0xFFFF;

    if (0x00010000 & temp) {
        result = 0xFFFF;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return result;
}

static inline int32_t mipsdsp_sat32_acc_q31(int32_t acc, int32_t a,
                                            CPUMIPSState *env)
{
    int64_t temp;
    int32_t temp32, temp31, result;
    int64_t temp_sum;

#ifndef TARGET_MIPS64
    temp = ((uint64_t)env->active_tc.HI[acc] << 32) |
           (uint64_t)env->active_tc.LO[acc];
#else
    temp = (uint64_t)env->active_tc.LO[acc];
#endif

    temp_sum = (int64_t)a + temp;

    temp32 = (temp_sum >> 32) & 0x01;
    temp31 = (temp_sum >> 31) & 0x01;
    result = temp_sum & 0xFFFFFFFF;

    /* FIXME
       This sat function may wrong, because user manual wrote:
       temp127..0 ← temp + ( (signA) || a31..0
       if ( temp32 ≠ temp31 ) then
           if ( temp32 = 0 ) then
               temp31..0 ← 0x80000000
           else
                temp31..0 ← 0x7FFFFFFF
           endif
           DSPControlouflag:16+acc ← 1
       endif
     */
    if (temp32 != temp31) {
        if (temp32 == 0) {
            result = 0x7FFFFFFF;
        } else {
            result = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 16 + acc, env);
    }

    return result;
}

/* a[0] is LO, a[1] is HI. */
static inline void mipsdsp_sat64_acc_add_q63(int64_t *ret,
                                             int32_t ac,
                                             int64_t *a,
                                             CPUMIPSState *env)
{
    bool temp64;

    ret[0] = env->active_tc.LO[ac] + a[0];
    ret[1] = env->active_tc.HI[ac] + a[1];

    if (((uint64_t)ret[0] < (uint64_t)env->active_tc.LO[ac]) &&
        ((uint64_t)ret[0] < (uint64_t)a[0])) {
        ret[1] += 1;
    }
    temp64 = ret[1] & 1;
    if (temp64 != ((ret[0] >> 63) & 0x01)) {
        if (temp64) {
            ret[0] = (0x01ull << 63);
            ret[1] = ~0ull;
        } else {
            ret[0] = (0x01ull << 63) - 1;
            ret[1] = 0x00;
        }
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    }
}

static inline void mipsdsp_sat64_acc_sub_q63(int64_t *ret,
                                             int32_t ac,
                                             int64_t *a,
                                             CPUMIPSState *env)
{
    bool temp64;

    ret[0] = env->active_tc.LO[ac] - a[0];
    ret[1] = env->active_tc.HI[ac] - a[1];

    if ((uint64_t)ret[0] > (uint64_t)env->active_tc.LO[ac]) {
        ret[1] -= 1;
    }
    temp64 = ret[1] & 1;
    if (temp64 != ((ret[0] >> 63) & 0x01)) {
        if (temp64) {
            ret[0] = (0x01ull << 63);
            ret[1] = ~0ull;
        } else {
            ret[0] = (0x01ull << 63) - 1;
            ret[1] = 0x00;
        }
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    }
}

static inline int32_t mipsdsp_mul_i16_i16(int16_t a, int16_t b,
                                          CPUMIPSState *env)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;

    if ((temp > (int)0x7FFF) || (temp < (int)0xFFFF8000)) {
        set_DSPControl_overflow_flag(1, 21, env);
    }
    temp &= 0x0000FFFF;

    return temp;
}

static inline int32_t mipsdsp_mul_u16_u16(int32_t a, int32_t b)
{
    return a * b;
}

static inline int32_t mipsdsp_mul_i32_i32(int32_t a, int32_t b)
{
    return a * b;
}

static inline int32_t mipsdsp_sat16_mul_i16_i16(int16_t a, int16_t b,
                                                CPUMIPSState *env)
{
    int32_t temp;

    temp = (int32_t)a * (int32_t)b;

    if (temp > (int)0x7FFF) {
        temp = 0x00007FFF;
        set_DSPControl_overflow_flag(1, 21, env);
    } else if (temp < (int)0xffff8000) {
        temp = 0xFFFF8000;
        set_DSPControl_overflow_flag(1, 21, env);
    }
    temp &= 0x0000FFFF;

    return temp;
}

static inline int32_t mipsdsp_mul_q15_q15_overflowflag21(uint16_t a, uint16_t b,
                                                         CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = ((int32_t)(int16_t)a * (int32_t)(int16_t)b) << 1;
    }

    return temp;
}

/* right shift */
static inline uint8_t mipsdsp_rshift_u8(uint8_t a, target_ulong mov)
{
    return a >> mov;
}

static inline uint16_t mipsdsp_rshift_u16(uint16_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int8_t mipsdsp_rashift8(int8_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int16_t mipsdsp_rashift16(int16_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int32_t mipsdsp_rashift32(int32_t a, target_ulong mov)
{
    return a >> mov;
}

static inline int16_t mipsdsp_rshift1_add_q16(int16_t a, int16_t b)
{
    int32_t temp;

    temp = (int32_t)a + (int32_t)b;

    return (temp >> 1) & 0xFFFF;
}

/* round right shift */
static inline int16_t mipsdsp_rrshift1_add_q16(int16_t a, int16_t b)
{
    int32_t temp;

    temp = (int32_t)a + (int32_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFF;
}

static inline int32_t mipsdsp_rshift1_add_q32(int32_t a, int32_t b)
{
    int64_t temp;

    temp = (int64_t)a + (int64_t)b;

    return (temp >> 1) & 0xFFFFFFFF;
}

static inline int32_t mipsdsp_rrshift1_add_q32(int32_t a, int32_t b)
{
    int64_t temp;

    temp = (int64_t)a + (int64_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFF;
}

static inline uint8_t mipsdsp_rshift1_add_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rrshift1_add_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a + (uint16_t)b + 1;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rshift1_sub_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;

    return (temp >> 1) & 0x00FF;
}

static inline uint8_t mipsdsp_rrshift1_sub_u8(uint8_t a, uint8_t b)
{
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b + 1;

    return (temp >> 1) & 0x00FF;
}

static inline int64_t mipsdsp_rashift_short_acc(int32_t ac,
                                                int32_t shift,
                                                CPUMIPSState *env)
{
    int32_t sign, temp31;
    int64_t temp, acc;

    sign = (env->active_tc.HI[ac] >> 31) & 0x01;
    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);
    if (shift == 0) {
        temp = acc;
    } else {
        if (sign == 0) {
            temp = (((int64_t)0x01 << (32 - shift + 1)) - 1) & (acc >> shift);
        } else {
            temp = ((((int64_t)0x01 << (shift + 1)) - 1) << (32 - shift)) |
                   (acc >> shift);
        }
    }

    temp31 = (temp >> 31) & 0x01;
    if (sign != temp31) {
        set_DSPControl_overflow_flag(1, 23, env);
    }

    return temp;
}

/*  128 bits long. p[0] is LO, p[1] is HI. */
static inline void mipsdsp_rndrashift_short_acc(int64_t *p,
                                                int32_t ac,
                                                int32_t shift,
                                                CPUMIPSState *env)
{
    int64_t acc;

    acc = ((int64_t)env->active_tc.HI[ac] << 32) |
          ((int64_t)env->active_tc.LO[ac] & 0xFFFFFFFF);
    if (shift == 0) {
        p[0] = acc << 1;
        p[1] = (acc >> 63) & 0x01;
    } else {
        p[0] = acc >> (shift - 1);
        p[1] = 0;
    }
}

/* 128 bits long. p[0] is LO, p[1] is HI */
static inline void mipsdsp_rashift_acc(uint64_t *p,
                                       uint32_t ac,
                                       uint32_t shift,
                                       CPUMIPSState *env)
{
    uint64_t tempB, tempA;

    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];
    shift = shift & 0x1F;

    if (shift == 0) {
        p[1] = tempB;
        p[0] = tempA;
    } else {
        p[0] = (tempB << (64 - shift)) | (tempA >> shift);
        p[1] = (int64_t)tempB >> shift;
    }
}

/* 128 bits long. p[0] is LO, p[1] is HI , p[2] is sign of HI.*/
static inline void mipsdsp_rndrashift_acc(uint64_t *p,
                                          uint32_t ac,
                                          uint32_t shift,
                                          CPUMIPSState *env)
{
    int64_t tempB, tempA;

    tempB = env->active_tc.HI[ac];
    tempA = env->active_tc.LO[ac];
    shift = shift & 0x3F;

    if (shift == 0) {
        p[2] = tempB >> 63;
        p[1] = (tempB << 1) | (tempA >> 63);
        p[0] = tempA << 1;
    } else {
        p[0] = (tempB << (65 - shift)) | (tempA >> (shift - 1));
        p[1] = (int64_t)tempB >> (shift - 1);
        if (tempB >= 0) {
            p[2] = 0x0;
        } else {
            p[2] = ~0ull;
        }
    }
}

static inline int32_t mipsdsp_mul_q15_q15(int32_t ac, uint16_t a, uint16_t b,
                                          CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    } else {
        temp = ((uint32_t)a * (uint32_t)b) << 1;
    }

    return temp;
}

static inline int64_t mipsdsp_mul_q31_q31(int32_t ac, uint32_t a, uint32_t b,
                                          CPUMIPSState *env)
{
    uint64_t temp;

    if ((a == 0x80000000) && (b == 0x80000000)) {
        temp = (0x01ull << 63) - 1;
        set_DSPControl_overflow_flag(1, 16 + ac, env);
    } else {
        temp = ((uint64_t)a * (uint64_t)b) << 1;
    }

    return temp;
}

static inline uint16_t mipsdsp_mul_u8_u8(uint8_t a, uint8_t b)
{
    return (uint16_t)a * (uint16_t)b;
}

static inline uint16_t mipsdsp_mul_u8_u16(uint8_t a, uint16_t b,
                                          CPUMIPSState *env)
{
    uint32_t tempI;

    tempI = (uint32_t)a * (uint32_t)b;
    if (tempI > 0x0000FFFF) {
        tempI = 0x0000FFFF;
        set_DSPControl_overflow_flag(1, 21, env);
    }

    return tempI & 0x0000FFFF;
}

static inline uint64_t mipsdsp_mul_u32_u32(uint32_t a, uint32_t b)
{
    return (uint64_t)a * (uint64_t)b;
}

static inline int16_t mipsdsp_rndq15_mul_q15_q15(uint16_t a, uint16_t b,
                                                 CPUMIPSState *env)
{
    uint32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFF0000;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = (a * b) << 1;
        temp = temp + 0x00008000;
    }

    return (temp & 0xFFFF0000) >> 16;
}

static inline int32_t mipsdsp_sat16_mul_q15_q15(uint16_t a, uint16_t b,
                                                CPUMIPSState *env)
{
    int32_t temp;

    if ((a == 0x8000) && (b == 0x8000)) {
        temp = 0x7FFF0000;
        set_DSPControl_overflow_flag(1, 21, env);
    } else {
        temp = ((uint32_t)a * (uint32_t)b);
        temp = temp << 1;
    }

    return (temp >> 16) & 0x0000FFFF;
}

static inline uint16_t mipsdsp_trunc16_sat16_round(int32_t a,
                                                   CPUMIPSState *env)
{
    int64_t temp;

    temp = (int32_t)a + 0x00008000;

    if (a > (int)0x7fff8000) {
        temp = 0x7FFFFFFF;
        set_DSPControl_overflow_flag(1, 22, env);
    }

    return (temp >> 16) & 0xFFFF;
}

static inline uint8_t mipsdsp_sat8_reduce_precision(uint16_t a,
                                                    CPUMIPSState *env)
{
    uint16_t mag;
    uint32_t sign;

    sign = (a >> 15) & 0x01;
    mag = a & 0x7FFF;

    if (sign == 0) {
        if (mag > 0x7F80) {
            set_DSPControl_overflow_flag(1, 22, env);
            return 0xFF;
        } else {
            return (mag >> 7) & 0xFFFF;
        }
    } else {
        set_DSPControl_overflow_flag(1, 22, env);
        return 0x00;
    }
}

static inline uint8_t mipsdsp_lshift8(uint8_t a, uint8_t s, CPUMIPSState *env)
{
    uint8_t sign;
    uint8_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 7) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (8 - s)) - 1) << s) |
                      ((a >> (6 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (6 - (s - 1));
        }

        if (discard != 0x00) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}

static inline uint16_t mipsdsp_lshift16(uint16_t a, uint8_t s,
                                        CPUMIPSState *env)
{
    uint8_t  sign;
    uint16_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 15) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (16 - s)) - 1) << s) |
                      ((a >> (14 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (14 - (s - 1));
        }

        if ((discard != 0x0000) && (discard != 0xFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}


static inline uint32_t mipsdsp_lshift32(uint32_t a, uint8_t s,
                                        CPUMIPSState *env)
{
    uint32_t discard;

    if (s == 0) {
        return a;
    } else {
        discard = (int32_t)a >> (31 - (s - 1));

        if ((discard != 0x00000000) && (discard != 0xFFFFFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
        }
        return a << s;
    }
}

static inline uint16_t mipsdsp_sat16_lshift(uint16_t a, uint8_t s,
                                            CPUMIPSState *env)
{
    uint8_t  sign;
    uint16_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 15) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (16 - s)) - 1) << s) |
                      ((a >> (14 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (14 - (s - 1));
        }

        if ((discard != 0x0000) && (discard != 0xFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
            return (sign == 0) ? 0x7FFF : 0x8000;
        } else {
            return a << s;
        }
    }
}

static inline uint32_t mipsdsp_sat32_lshift(uint32_t a, uint8_t s,
                                            CPUMIPSState *env)
{
    uint8_t  sign;
    uint32_t discard;

    if (s == 0) {
        return a;
    } else {
        sign = (a >> 31) & 0x01;
        if (sign != 0) {
            discard = (((0x01 << (32 - s)) - 1) << s) |
                      ((a >> (30 - (s - 1))) & ((0x01 << s) - 1));
        } else {
            discard = a >> (30 - (s - 1));
        }

        if ((discard != 0x00000000) && (discard != 0xFFFFFFFF)) {
            set_DSPControl_overflow_flag(1, 22, env);
            return (sign == 0) ? 0x7FFFFFFF : 0x80000000;
        } else {
            return a << s;
        }
    }
}

static inline uint8_t mipsdsp_rnd8_rashift(uint8_t a, uint8_t s)
{
    uint32_t temp;

    if (s == 0) {
        temp = (uint32_t)a << 1;
    } else {
        temp = (int32_t)(int8_t)a >> (s - 1);
    }

    return (temp + 1) >> 1;
}

static inline uint16_t mipsdsp_rnd16_rashift(uint16_t a, uint8_t s)
{
    uint32_t temp;

    if (s == 0) {
        temp = (uint32_t)a << 1;
    } else {
        temp = (int32_t)(int16_t)a >> (s - 1);
    }

    return (temp + 1) >> 1;
}

static inline uint32_t mipsdsp_rnd32_rashift(uint32_t a, uint8_t s)
{
    int64_t temp;

    if (s == 0) {
        temp = (uint64_t)a << 1;
    } else {
        temp = (int64_t)(int32_t)a >> (s - 1);
    }
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_sub_i16(int16_t a, int16_t b, CPUMIPSState *env)
{
    int16_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x8000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline uint16_t mipsdsp_sat16_sub(int16_t a, int16_t b,
                                         CPUMIPSState *env)
{
    int16_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x8000)) {
        if (a > 0) {
            temp = 0x7FFF;
        } else {
            temp = 0x8000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline uint32_t mipsdsp_sat32_sub(int32_t a, int32_t b,
                                         CPUMIPSState *env)
{
    int32_t  temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x80000000)) {
        if (a > 0) {
            temp = 0x7FFFFFFF;
        } else {
            temp = 0x80000000;
        }
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_rshift1_sub_q16(int16_t a, int16_t b)
{
    int32_t  temp;

    temp = (int32_t)a - (int32_t)b;

    return (temp >> 1) & 0x0000FFFF;
}

static inline uint16_t mipsdsp_rrshift1_sub_q16(int16_t a, int16_t b)
{
    int32_t  temp;

    temp = (int32_t)a - (int32_t)b;
    temp += 1;

    return (temp >> 1) & 0x0000FFFF;
}

static inline uint32_t mipsdsp_rshift1_sub_q32(int32_t a, int32_t b)
{
    int64_t  temp;

    temp = (int64_t)a - (int64_t)b;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint32_t mipsdsp_rrshift1_sub_q32(int32_t a, int32_t b)
{
    int64_t  temp;

    temp = (int64_t)a - (int64_t)b;
    temp += 1;

    return (temp >> 1) & 0xFFFFFFFFull;
}

static inline uint16_t mipsdsp_sub_u16_u16(uint16_t a, uint16_t b,
                                           CPUMIPSState *env)
{
    uint8_t  temp16;
    uint32_t temp;

    temp = (uint32_t)a - (uint32_t)b;
    temp16 = (temp >> 16) & 0x01;
    if (temp16 == 1) {
        set_DSPControl_overflow_flag(1, 20, env);
    }
    return temp & 0x0000FFFF;
}

static inline uint16_t mipsdsp_satu16_sub_u16_u16(uint16_t a, uint16_t b,
                                                  CPUMIPSState *env)
{
    uint8_t  temp16;
    uint32_t temp;

    temp   = (uint32_t)a - (uint32_t)b;
    temp16 = (temp >> 16) & 0x01;

    if (temp16 == 1) {
        temp = 0x0000;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x0000FFFF;
}

static inline uint8_t mipsdsp_sub_u8(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint8_t  temp8;
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;
    temp8 = (temp >> 8) & 0x01;
    if (temp8 == 1) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x00FF;
}

static inline uint8_t mipsdsp_satu8_sub(uint8_t a, uint8_t b, CPUMIPSState *env)
{
    uint8_t  temp8;
    uint16_t temp;

    temp = (uint16_t)a - (uint16_t)b;
    temp8 = (temp >> 8) & 0x01;
    if (temp8 == 1) {
        temp = 0x00;
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp & 0x00FF;
}

static inline uint32_t mipsdsp_sub32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a - b;
    if (MIPSDSP_OVERFLOW(a, -b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline int32_t mipsdsp_add_i32(int32_t a, int32_t b, CPUMIPSState *env)
{
    int32_t temp;

    temp = a + b;

    if (MIPSDSP_OVERFLOW(a, b, temp, 0x80000000)) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    return temp;
}

static inline int32_t mipsdsp_cmp_eq(int32_t a, int32_t b)
{
    return a == b;
}

static inline int32_t mipsdsp_cmp_le(int32_t a, int32_t b)
{
    return a <= b;
}

static inline int32_t mipsdsp_cmp_lt(int32_t a, int32_t b)
{
    return a < b;
}

static inline int32_t mipsdsp_cmpu_eq(uint32_t a, uint32_t b)
{
    return a == b;
}

static inline int32_t mipsdsp_cmpu_le(uint32_t a, uint32_t b)
{
    return a <= b;
}

static inline int32_t mipsdsp_cmpu_lt(uint32_t a, uint32_t b)
{
    return a < b;
}
/*** MIPS DSP internal functions end ***/

#define MIPSDSP_LHI 0xFFFFFFFF00000000ull
#define MIPSDSP_LLO 0x00000000FFFFFFFFull
#define MIPSDSP_HI  0xFFFF0000
#define MIPSDSP_LO  0x0000FFFF
#define MIPSDSP_Q3  0xFF000000
#define MIPSDSP_Q2  0x00FF0000
#define MIPSDSP_Q1  0x0000FF00
#define MIPSDSP_Q0  0x000000FF

#define MIPSDSP_SPLIT32_8(num, a, b, c, d)  \
    do {                                    \
        a = (num >> 24) & MIPSDSP_Q0;       \
        b = (num >> 16) & MIPSDSP_Q0;       \
        c = (num >> 8) & MIPSDSP_Q0;        \
        d = num & MIPSDSP_Q0;               \
    } while (0)

#define MIPSDSP_SPLIT32_16(num, a, b)       \
    do {                                    \
        a = (num >> 16) & MIPSDSP_LO;       \
        b = num & MIPSDSP_LO;               \
    } while (0)

#define MIPSDSP_RETURN32(a)             ((target_long)(int32_t)a)
#define MIPSDSP_RETURN32_8(a, b, c, d)  ((target_long)(int32_t) \
                                         (((uint32_t)a << 24) | \
                                         (((uint32_t)b << 16) | \
                                         (((uint32_t)c << 8) |  \
                                          ((uint32_t)d & 0xFF)))))
#define MIPSDSP_RETURN32_16(a, b)       ((target_long)(int32_t) \
                                         (((uint32_t)a << 16) | \
                                          ((uint32_t)b & 0xFFFF)))

#ifdef TARGET_MIPS64
#define MIPSDSP_SPLIT64_16(num, a, b, c, d)  \
    do {                                     \
        a = (num >> 48) & MIPSDSP_LO;        \
        b = (num >> 32) & MIPSDSP_LO;        \
        c = (num >> 16) & MIPSDSP_LO;        \
        d = num & MIPSDSP_LO;                \
    } while (0)

#define MIPSDSP_SPLIT64_32(num, a, b)       \
    do {                                    \
        a = (num >> 32) & MIPSDSP_LLO;      \
        b = num & MIPSDSP_LLO;              \
    } while (0)

#define MIPSDSP_RETURN64_16(a, b, c, d) (((uint64_t)a << 48) | \
                                         ((uint64_t)b << 32) | \
                                         ((uint64_t)c << 16) | \
                                         (uint64_t)d)
#define MIPSDSP_RETURN64_32(a, b)       (((uint64_t)a << 32) | (uint64_t)b)
#endif

/** DSP Arithmetic Sub-class insns **/
#define ARITH_PH(name, func)                                      \
target_ulong helper_##name##_ph(target_ulong rs, target_ulong rt) \
{                                                                 \
    uint16_t  rsh, rsl, rth, rtl, temph, templ;                   \
                                                                  \
    MIPSDSP_SPLIT32_16(rs, rsh, rsl);                             \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                             \
                                                                  \
    temph = mipsdsp_##func(rsh, rth);                             \
    templ = mipsdsp_##func(rsl, rtl);                             \
                                                                  \
    return MIPSDSP_RETURN32_16(temph, templ);                     \
}

#define ARITH_PH_ENV(name, func)                                  \
target_ulong helper_##name##_ph(target_ulong rs, target_ulong rt, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint16_t  rsh, rsl, rth, rtl, temph, templ;                   \
                                                                  \
    MIPSDSP_SPLIT32_16(rs, rsh, rsl);                             \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                             \
                                                                  \
    temph = mipsdsp_##func(rsh, rth, env);                        \
    templ = mipsdsp_##func(rsl, rtl, env);                        \
                                                                  \
    return MIPSDSP_RETURN32_16(temph, templ);                     \
}


ARITH_PH_ENV(addq, add_i16);
ARITH_PH_ENV(addq_s, sat_add_i16);
ARITH_PH_ENV(addu, add_u16);
ARITH_PH_ENV(addu_s, sat_add_u16);

ARITH_PH(addqh, rshift1_add_q16);
ARITH_PH(addqh_r, rrshift1_add_q16);

ARITH_PH_ENV(subq, sub_i16);
ARITH_PH_ENV(subq_s, sat16_sub);
ARITH_PH_ENV(subu, sub_u16_u16);
ARITH_PH_ENV(subu_s, satu16_sub_u16_u16);

ARITH_PH(subqh, rshift1_sub_q16);
ARITH_PH(subqh_r, rrshift1_sub_q16);

#undef ARITH_PH
#undef ARITH_PH_ENV

#ifdef TARGET_MIPS64
#define ARITH_QH_ENV(name, func) \
target_ulong helper_##name##_qh(target_ulong rs, target_ulong rt, \
                                CPUMIPSState *env)           \
{                                                            \
    uint16_t rs3, rs2, rs1, rs0;                             \
    uint16_t rt3, rt2, rt1, rt0;                             \
    uint16_t tempD, tempC, tempB, tempA;                     \
                                                             \
    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);              \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);              \
                                                             \
    tempD = mipsdsp_##func(rs3, rt3, env);                   \
    tempC = mipsdsp_##func(rs2, rt2, env);                   \
    tempB = mipsdsp_##func(rs1, rt1, env);                   \
    tempA = mipsdsp_##func(rs0, rt0, env);                   \
                                                             \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);  \
}

ARITH_QH_ENV(addq, add_i16);
ARITH_QH_ENV(addq_s, sat_add_i16);
ARITH_QH_ENV(addu, add_u16);
ARITH_QH_ENV(addu_s, sat_add_u16);

ARITH_QH_ENV(subq, sub_i16);
ARITH_QH_ENV(subq_s, sat16_sub);
ARITH_QH_ENV(subu, sub_u16_u16);
ARITH_QH_ENV(subu_s, satu16_sub_u16_u16);

#undef ARITH_QH_ENV

#endif

#define ARITH_W(name, func) \
target_ulong helper_##name##_w(target_ulong rs, target_ulong rt) \
{                                                                \
    uint32_t rd;                                                 \
    rd = mipsdsp_##func(rs, rt);                                 \
    return MIPSDSP_RETURN32(rd);                                 \
}

#define ARITH_W_ENV(name, func) \
target_ulong helper_##name##_w(target_ulong rs, target_ulong rt, \
                               CPUMIPSState *env)                \
{                                                                \
    uint32_t rd;                                                 \
    rd = mipsdsp_##func(rs, rt, env);                            \
    return MIPSDSP_RETURN32(rd);                                 \
}

ARITH_W_ENV(addq_s, sat_add_i32);

ARITH_W(addqh, rshift1_add_q32);
ARITH_W(addqh_r, rrshift1_add_q32);

ARITH_W_ENV(subq_s, sat32_sub);

ARITH_W(subqh, rshift1_sub_q32);
ARITH_W(subqh_r, rrshift1_sub_q32);

#undef ARITH_W
#undef ARITH_W_ENV

target_ulong helper_absq_s_w(target_ulong rt, CPUMIPSState *env)
{
    uint32_t rd;

    rd = mipsdsp_sat_abs32(rt, env);

    return (target_ulong)rd;
}


#if defined(TARGET_MIPS64)

#define ARITH_PW_ENV(name, func) \
target_ulong helper_##name##_pw(target_ulong rs, target_ulong rt, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint32_t rs1, rs0;                                            \
    uint32_t rt1, rt0;                                            \
    uint32_t tempB, tempA;                                        \
                                                                  \
    MIPSDSP_SPLIT64_32(rs, rs1, rs0);                             \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    tempB = mipsdsp_##func(rs1, rt1, env);                        \
    tempA = mipsdsp_##func(rs0, rt0, env);                        \
                                                                  \
    return MIPSDSP_RETURN64_32(tempB, tempA);                     \
}

ARITH_PW_ENV(addq, add_i32);
ARITH_PW_ENV(addq_s, sat_add_i32);
ARITH_PW_ENV(subq, sub32);
ARITH_PW_ENV(subq_s, sat32_sub);

#undef ARITH_PW_ENV

#endif

#define ARITH_QB(name, func) \
target_ulong helper_##name##_qb(target_ulong rs, target_ulong rt) \
{                                                                 \
    uint8_t  rs0, rs1, rs2, rs3;                                  \
    uint8_t  rt0, rt1, rt2, rt3;                                  \
    uint8_t  temp0, temp1, temp2, temp3;                          \
                                                                  \
    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);                    \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                    \
                                                                  \
    temp0 = mipsdsp_##func(rs0, rt0);                             \
    temp1 = mipsdsp_##func(rs1, rt1);                             \
    temp2 = mipsdsp_##func(rs2, rt2);                             \
    temp3 = mipsdsp_##func(rs3, rt3);                             \
                                                                  \
    return MIPSDSP_RETURN32_8(temp3, temp2, temp1, temp0);        \
}

#define ARITH_QB_ENV(name, func) \
target_ulong helper_##name##_qb(target_ulong rs, target_ulong rt, \
                                CPUMIPSState *env)          \
{                                                           \
    uint8_t  rs0, rs1, rs2, rs3;                            \
    uint8_t  rt0, rt1, rt2, rt3;                            \
    uint8_t  temp0, temp1, temp2, temp3;                    \
                                                            \
    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);              \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);              \
                                                            \
    temp0 = mipsdsp_##func(rs0, rt0, env);                  \
    temp1 = mipsdsp_##func(rs1, rt1, env);                  \
    temp2 = mipsdsp_##func(rs2, rt2, env);                  \
    temp3 = mipsdsp_##func(rs3, rt3, env);                  \
                                                            \
    return MIPSDSP_RETURN32_8(temp3, temp2, temp1, temp0);  \
}

ARITH_QB(adduh, rshift1_add_u8);
ARITH_QB(adduh_r, rrshift1_add_u8);

ARITH_QB_ENV(addu, add_u8);
ARITH_QB_ENV(addu_s, sat_add_u8);

#undef ADDU_QB
#undef ADDU_QB_ENV

#if defined(TARGET_MIPS64)
#define ARITH_OB(name, func) \
target_ulong helper_##name##_ob(target_ulong rs, target_ulong rt) \
{                                                                 \
    int i;                                                        \
    uint8_t rs_t[8], rt_t[8];                                     \
    uint8_t temp[8];                                              \
    uint64_t result;                                              \
                                                                  \
    result = 0;                                                   \
                                                                  \
    for (i = 0; i < 8; i++) {                                     \
        rs_t[i] = (rs >> (8 * i)) & MIPSDSP_Q0;                   \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                   \
        temp[i] = mipsdsp_##func(rs_t[i], rt_t[i]);               \
        result |= (uint64_t)temp[i] << (8 * i);                   \
    }                                                             \
                                                                  \
    return result;                                                \
}

#define ARITH_OB_ENV(name, func) \
target_ulong helper_##name##_ob(target_ulong rs, target_ulong rt, \
                                CPUMIPSState *env)                \
{                                                                 \
    int i;                                                        \
    uint8_t rs_t[8], rt_t[8];                                     \
    uint8_t temp[8];                                              \
    uint64_t result;                                              \
                                                                  \
    result = 0;                                                   \
                                                                  \
    for (i = 0; i < 8; i++) {                                     \
        rs_t[i] = (rs >> (8 * i)) & MIPSDSP_Q0;                   \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                   \
        temp[i] = mipsdsp_##func(rs_t[i], rt_t[i], env);          \
        result |= (uint64_t)temp[i] << (8 * i);                   \
    }                                                             \
                                                                  \
    return result;                                                \
}

ARITH_OB_ENV(addu, add_u8);
ARITH_OB_ENV(addu_s, sat_add_u8);

ARITH_OB(adduh, rshift1_add_u8);
ARITH_OB(adduh_r, rrshift1_add_u8);

ARITH_OB_ENV(subu, sub_u8);
ARITH_OB_ENV(subu_s, satu8_sub);

ARITH_OB(subuh, rshift1_sub_u8);
ARITH_OB(subuh_r, rrshift1_sub_u8);

#undef ARITH_OB
#undef ARITH_OB_ENV

#endif

#define SUBU_QB(name, func) \
target_ulong helper_##name##_qb(target_ulong rs,               \
                                target_ulong rt,               \
                                CPUMIPSState *env)             \
{                                                              \
    uint8_t rs3, rs2, rs1, rs0;                                \
    uint8_t rt3, rt2, rt1, rt0;                                \
    uint8_t tempD, tempC, tempB, tempA;                        \
                                                               \
    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);                 \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                 \
                                                               \
    tempD = mipsdsp_##func(rs3, rt3, env);                     \
    tempC = mipsdsp_##func(rs2, rt2, env);                     \
    tempB = mipsdsp_##func(rs1, rt1, env);                     \
    tempA = mipsdsp_##func(rs0, rt0, env);                     \
                                                               \
    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);     \
}

SUBU_QB(subu, sub_u8);
SUBU_QB(subu_s, satu8_sub);

#undef SUBU_QB

#define SUBUH_QB(name, var) \
target_ulong helper_##name##_qb(target_ulong rs, target_ulong rt) \
{                                                                 \
    uint8_t rs3, rs2, rs1, rs0;                                   \
    uint8_t rt3, rt2, rt1, rt0;                                   \
    uint8_t tempD, tempC, tempB, tempA;                           \
                                                                  \
    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);                    \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                    \
                                                                  \
    tempD = ((uint16_t)rs3 - (uint16_t)rt3 + var) >> 1;           \
    tempC = ((uint16_t)rs2 - (uint16_t)rt2 + var) >> 1;           \
    tempB = ((uint16_t)rs1 - (uint16_t)rt1 + var) >> 1;           \
    tempA = ((uint16_t)rs0 - (uint16_t)rt0 + var) >> 1;           \
                                                                  \
    return ((uint32_t)tempD << 24) | ((uint32_t)tempC << 16) |    \
        ((uint32_t)tempB << 8) | ((uint32_t)tempA);               \
}

SUBUH_QB(subuh, 0);
SUBUH_QB(subuh_r, 1);

#undef SUBUH_QB

target_ulong helper_addsc(target_ulong rs, target_ulong rt, CPUMIPSState *env)
{
    uint64_t temp, tempRs, tempRt;
    int32_t flag;

    tempRs = (uint64_t)rs & MIPSDSP_LLO;
    tempRt = (uint64_t)rt & MIPSDSP_LLO;

    temp = tempRs + tempRt;
    flag = (temp & 0x0100000000ull) >> 32;
    set_DSPControl_carryflag(flag, env);

    return (target_long)(int32_t)(temp & MIPSDSP_LLO);
}

target_ulong helper_addwc(target_ulong rs, target_ulong rt, CPUMIPSState *env)
{
    uint32_t rd;
    int32_t temp32, temp31;
    int64_t tempL;

    tempL = (int64_t)(int32_t)rs + (int64_t)(int32_t)rt +
        get_DSPControl_carryflag(env);
    temp31 = (tempL >> 31) & 0x01;
    temp32 = (tempL >> 32) & 0x01;

    if (temp31 != temp32) {
        set_DSPControl_overflow_flag(1, 20, env);
    }

    rd = tempL & MIPSDSP_LLO;

    return (target_long)(int32_t)rd;
}

target_ulong helper_modsub(target_ulong rs, target_ulong rt)
{
    int32_t decr;
    uint16_t lastindex;
    target_ulong rd;

    decr = rt & MIPSDSP_Q0;
    lastindex = (rt >> 8) & MIPSDSP_LO;

    if ((rs & MIPSDSP_LLO) == 0x00000000) {
        rd = (target_ulong)lastindex;
    } else {
        rd = rs - decr;
    }

    return rd;
}

target_ulong helper_raddu_w_qb(target_ulong rs)
{
    uint8_t  rs3, rs2, rs1, rs0;
    uint16_t temp;

    MIPSDSP_SPLIT32_8(rs, rs3, rs2, rs1, rs0);

    temp = (uint16_t)rs3 + (uint16_t)rs2 + (uint16_t)rs1 + (uint16_t)rs0;

    return (target_ulong)temp;
}

#if defined(TARGET_MIPS64)
target_ulong helper_raddu_l_ob(target_ulong rs)
{
    int i;
    uint16_t rs_t[8];
    uint64_t temp;

    temp = 0;

    for (i = 0; i < 8; i++) {
        rs_t[i] = (rs >> (8 * i)) & MIPSDSP_Q0;
        temp += (uint64_t)rs_t[i];
    }

    return temp;
}
#endif

target_ulong helper_absq_s_qb(target_ulong rt, CPUMIPSState *env)
{
    uint8_t tempD, tempC, tempB, tempA;

    MIPSDSP_SPLIT32_8(rt, tempD, tempC, tempB, tempA);

    tempD = mipsdsp_sat_abs8(tempD, env);
    tempC = mipsdsp_sat_abs8(tempC, env);
    tempB = mipsdsp_sat_abs8(tempB, env);
    tempA = mipsdsp_sat_abs8(tempA, env);

    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);
}

target_ulong helper_absq_s_ph(target_ulong rt, CPUMIPSState *env)
{
    uint16_t tempB, tempA;

    MIPSDSP_SPLIT32_16(rt, tempB, tempA);

    tempB = mipsdsp_sat_abs16 (tempB, env);
    tempA = mipsdsp_sat_abs16 (tempA, env);

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

#if defined(TARGET_MIPS64)
target_ulong helper_absq_s_ob(target_ulong rt, CPUMIPSState *env)
{
    int i;
    int8_t temp[8];
    uint64_t result;

    for (i = 0; i < 8; i++) {
        temp[i] = (rt >> (8 * i)) & MIPSDSP_Q0;
        temp[i] = mipsdsp_sat_abs8(temp[i], env);
    }

    for (i = 0; i < 8; i++) {
        result = (uint64_t)(uint8_t)temp[i] << (8 * i);
    }

    return result;
}

target_ulong helper_absq_s_qh(target_ulong rt, CPUMIPSState *env)
{
    int16_t tempD, tempC, tempB, tempA;

    MIPSDSP_SPLIT64_16(rt, tempD, tempC, tempB, tempA);

    tempD = mipsdsp_sat_abs16(tempD, env);
    tempC = mipsdsp_sat_abs16(tempC, env);
    tempB = mipsdsp_sat_abs16(tempB, env);
    tempA = mipsdsp_sat_abs16(tempA, env);

    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);
}

target_ulong helper_absq_s_pw(target_ulong rt, CPUMIPSState *env)
{
    int32_t tempB, tempA;

    MIPSDSP_SPLIT64_32(rt, tempB, tempA);

    tempB = mipsdsp_sat_abs32(tempB, env);
    tempA = mipsdsp_sat_abs32(tempA, env);

    return MIPSDSP_RETURN64_32(tempB, tempA);
}
#endif

#define PRECR_QB_PH(name, a, b)\
target_ulong helper_##name##_qb_ph(target_ulong rs, target_ulong rt) \
{                                                                    \
    uint8_t tempD, tempC, tempB, tempA;                              \
                                                                     \
    tempD = (rs >> a) & MIPSDSP_Q0;                                  \
    tempC = (rs >> b) & MIPSDSP_Q0;                                  \
    tempB = (rt >> a) & MIPSDSP_Q0;                                  \
    tempA = (rt >> b) & MIPSDSP_Q0;                                  \
                                                                     \
    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);           \
}

PRECR_QB_PH(precr, 16, 0);
PRECR_QB_PH(precrq, 24, 8);

#undef PRECR_QB_OH

target_ulong helper_precr_sra_ph_w(uint32_t sa, target_ulong rs,
                                   target_ulong rt)
{
    uint16_t tempB, tempA;

    tempB = ((int32_t)rt >> sa) & MIPSDSP_LO;
    tempA = ((int32_t)rs >> sa) & MIPSDSP_LO;

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

target_ulong helper_precr_sra_r_ph_w(uint32_t sa,
                                     target_ulong rs, target_ulong rt)
{
    uint64_t tempB, tempA;

    /* If sa = 0, then (sa - 1) = -1 will case shift error, so we need else. */
    if (sa == 0) {
        tempB = (rt & MIPSDSP_LO) << 1;
        tempA = (rs & MIPSDSP_LO) << 1;
    } else {
        tempB = ((int32_t)rt >> (sa - 1)) + 1;
        tempA = ((int32_t)rs >> (sa - 1)) + 1;
    }
    rt = (((tempB >> 1) & MIPSDSP_LO) << 16) | ((tempA >> 1) & MIPSDSP_LO);

    return (target_long)(int32_t)rt;
}

target_ulong helper_precrq_ph_w(target_ulong rs, target_ulong rt)
{
    uint16_t tempB, tempA;

    tempB = (rs & MIPSDSP_HI) >> 16;
    tempA = (rt & MIPSDSP_HI) >> 16;

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

target_ulong helper_precrq_rs_ph_w(target_ulong rs, target_ulong rt,
                                   CPUMIPSState *env)
{
    uint16_t tempB, tempA;

    tempB = mipsdsp_trunc16_sat16_round(rs, env);
    tempA = mipsdsp_trunc16_sat16_round(rt, env);

    return MIPSDSP_RETURN32_16(tempB, tempA);
}

#if defined(TARGET_MIPS64)
target_ulong helper_precr_ob_qh(target_ulong rs, target_ulong rt)
{
    uint8_t rs6, rs4, rs2, rs0;
    uint8_t rt6, rt4, rt2, rt0;
    uint64_t temp;

    rs6 = (rs >> 48) & MIPSDSP_Q0;
    rs4 = (rs >> 32) & MIPSDSP_Q0;
    rs2 = (rs >> 16) & MIPSDSP_Q0;
    rs0 = rs & MIPSDSP_Q0;
    rt6 = (rt >> 48) & MIPSDSP_Q0;
    rt4 = (rt >> 32) & MIPSDSP_Q0;
    rt2 = (rt >> 16) & MIPSDSP_Q0;
    rt0 = rt & MIPSDSP_Q0;

    temp = ((uint64_t)rs6 << 56) | ((uint64_t)rs4 << 48) |
           ((uint64_t)rs2 << 40) | ((uint64_t)rs0 << 32) |
           ((uint64_t)rt6 << 24) | ((uint64_t)rt4 << 16) |
           ((uint64_t)rt2 << 8) | (uint64_t)rt0;

    return temp;
}

#define PRECR_QH_PW(name, var) \
target_ulong helper_precr_##name##_qh_pw(target_ulong rs, target_ulong rt, \
                                    uint32_t sa)                      \
{                                                                     \
    uint16_t rs3, rs2, rs1, rs0;                                      \
    uint16_t rt3, rt2, rt1, rt0;                                      \
    uint16_t tempD, tempC, tempB, tempA;                              \
                                                                      \
    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);                       \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                       \
                                                                      \
    /* When sa = 0, we use rt2, rt0, rs2, rs0;                        \
     * when sa != 0, we use rt3, rt1, rs3, rs1. */                    \
    if (sa == 0) {                                                    \
        tempD = rt2 << var;                                           \
        tempC = rt0 << var;                                           \
        tempB = rs2 << var;                                           \
        tempA = rs0 << var;                                           \
    } else {                                                          \
        tempD = (((int16_t)rt3 >> sa) + var) >> var;                  \
        tempC = (((int16_t)rt1 >> sa) + var) >> var;                  \
        tempB = (((int16_t)rs3 >> sa) + var) >> var;                  \
        tempA = (((int16_t)rs1 >> sa) + var) >> var;                  \
    }                                                                 \
                                                                      \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);           \
}

PRECR_QH_PW(sra, 0);
PRECR_QH_PW(sra_r, 1);

#undef PRECR_QH_PW

target_ulong helper_precrq_ob_qh(target_ulong rs, target_ulong rt)
{
    uint8_t rs6, rs4, rs2, rs0;
    uint8_t rt6, rt4, rt2, rt0;
    uint64_t temp;

    rs6 = (rs >> 56) & MIPSDSP_Q0;
    rs4 = (rs >> 40) & MIPSDSP_Q0;
    rs2 = (rs >> 24) & MIPSDSP_Q0;
    rs0 = (rs >> 8) & MIPSDSP_Q0;
    rt6 = (rt >> 56) & MIPSDSP_Q0;
    rt4 = (rt >> 40) & MIPSDSP_Q0;
    rt2 = (rt >> 24) & MIPSDSP_Q0;
    rt0 = (rt >> 8) & MIPSDSP_Q0;

    temp = ((uint64_t)rs6 << 56) | ((uint64_t)rs4 << 48) |
           ((uint64_t)rs2 << 40) | ((uint64_t)rs0 << 32) |
           ((uint64_t)rt6 << 24) | ((uint64_t)rt4 << 16) |
           ((uint64_t)rt2 << 8) | (uint64_t)rt0;

    return temp;
}

target_ulong helper_precrq_qh_pw(target_ulong rs, target_ulong rt)
{
    uint16_t tempD, tempC, tempB, tempA;

    tempD = (rs >> 48) & MIPSDSP_LO;
    tempC = (rs >> 16) & MIPSDSP_LO;
    tempB = (rt >> 48) & MIPSDSP_LO;
    tempA = (rt >> 16) & MIPSDSP_LO;

    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);
}

target_ulong helper_precrq_rs_qh_pw(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    uint32_t rs2, rs0;
    uint32_t rt2, rt0;
    uint16_t tempD, tempC, tempB, tempA;

    rs2 = (rs >> 32) & MIPSDSP_LLO;
    rs0 = rs & MIPSDSP_LLO;
    rt2 = (rt >> 32) & MIPSDSP_LLO;
    rt0 = rt & MIPSDSP_LLO;

    tempD = mipsdsp_trunc16_sat16_round(rs2, env);
    tempC = mipsdsp_trunc16_sat16_round(rs0, env);
    tempB = mipsdsp_trunc16_sat16_round(rt2, env);
    tempA = mipsdsp_trunc16_sat16_round(rt0, env);

    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);
}

target_ulong helper_precrq_pw_l(target_ulong rs, target_ulong rt)
{
    uint32_t tempB, tempA;

    tempB = (rs >> 32) & MIPSDSP_LLO;
    tempA = (rt >> 32) & MIPSDSP_LLO;

    return MIPSDSP_RETURN64_32(tempB, tempA);
}
#endif

target_ulong helper_precrqu_s_qb_ph(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    uint8_t  tempD, tempC, tempB, tempA;
    uint16_t rsh, rsl, rth, rtl;

    rsh = (rs & MIPSDSP_HI) >> 16;
    rsl =  rs & MIPSDSP_LO;
    rth = (rt & MIPSDSP_HI) >> 16;
    rtl =  rt & MIPSDSP_LO;

    tempD = mipsdsp_sat8_reduce_precision(rsh, env);
    tempC = mipsdsp_sat8_reduce_precision(rsl, env);
    tempB = mipsdsp_sat8_reduce_precision(rth, env);
    tempA = mipsdsp_sat8_reduce_precision(rtl, env);

    return MIPSDSP_RETURN32_8(tempD, tempC, tempB, tempA);
}

#if defined(TARGET_MIPS64)
target_ulong helper_precrqu_s_ob_qh(target_ulong rs, target_ulong rt,
                                    CPUMIPSState *env)
{
    int i;
    uint16_t rs3, rs2, rs1, rs0;
    uint16_t rt3, rt2, rt1, rt0;
    uint8_t temp[8];
    uint64_t result;

    result = 0;

    MIPSDSP_SPLIT64_16(rs, rs3, rs2, rs1, rs0);
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);

    temp[7] = mipsdsp_sat8_reduce_precision(rs3, env);
    temp[6] = mipsdsp_sat8_reduce_precision(rs2, env);
    temp[5] = mipsdsp_sat8_reduce_precision(rs1, env);
    temp[4] = mipsdsp_sat8_reduce_precision(rs0, env);
    temp[3] = mipsdsp_sat8_reduce_precision(rt3, env);
    temp[2] = mipsdsp_sat8_reduce_precision(rt2, env);
    temp[1] = mipsdsp_sat8_reduce_precision(rt1, env);
    temp[0] = mipsdsp_sat8_reduce_precision(rt0, env);

    for (i = 0; i < 8; i++) {
        result |= (uint64_t)temp[i] << (8 * i);
    }

    return result;
}

#define PRECEQ_PW(name, a, b) \
target_ulong helper_preceq_pw_##name(target_ulong rt) \
{                                                       \
    uint16_t tempB, tempA;                              \
    uint32_t tempBI, tempAI;                            \
                                                        \
    tempB = (rt >> a) & MIPSDSP_LO;                     \
    tempA = (rt >> b) & MIPSDSP_LO;                     \
                                                        \
    tempBI = (uint32_t)tempB << 16;                     \
    tempAI = (uint32_t)tempA << 16;                     \
                                                        \
    return MIPSDSP_RETURN64_32(tempBI, tempAI);         \
}

PRECEQ_PW(qhl, 48, 32);
PRECEQ_PW(qhr, 16, 0);
PRECEQ_PW(qhla, 48, 16);
PRECEQ_PW(qhra, 32, 0);

#undef PRECEQ_PW

#endif

#define PRECEQU_PH(name, a, b) \
target_ulong helper_precequ_ph_##name(target_ulong rt) \
{                                                        \
    uint16_t tempB, tempA;                               \
                                                         \
    tempB = (rt >> a) & MIPSDSP_Q0;                      \
    tempA = (rt >> b) & MIPSDSP_Q0;                      \
                                                         \
    tempB = tempB << 7;                                  \
    tempA = tempA << 7;                                  \
                                                         \
    return MIPSDSP_RETURN32_16(tempB, tempA);            \
}

PRECEQU_PH(qbl, 24, 16);
PRECEQU_PH(qbr, 8, 0);
PRECEQU_PH(qbla, 24, 8);
PRECEQU_PH(qbra, 16, 0);

#undef PRECEQU_PH

#if defined(TARGET_MIPS64)
#define PRECEQU_QH(name, a, b, c, d) \
target_ulong helper_precequ_qh_##name(target_ulong rt)       \
{                                                            \
    uint16_t tempD, tempC, tempB, tempA;                     \
                                                             \
    tempD = (rt >> a) & MIPSDSP_Q0;                          \
    tempC = (rt >> b) & MIPSDSP_Q0;                          \
    tempB = (rt >> c) & MIPSDSP_Q0;                          \
    tempA = (rt >> d) & MIPSDSP_Q0;                          \
                                                             \
    tempD = tempD << 7;                                      \
    tempC = tempC << 7;                                      \
    tempB = tempB << 7;                                      \
    tempA = tempA << 7;                                      \
                                                             \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);  \
}

PRECEQU_QH(obl, 56, 48, 40, 32);
PRECEQU_QH(obr, 24, 16, 8, 0);
PRECEQU_QH(obla, 56, 40, 24, 8);
PRECEQU_QH(obra, 48, 32, 16, 0);

#undef PRECEQU_QH

#endif

#define PRECEU_PH(name, a, b) \
target_ulong helper_preceu_ph_##name(target_ulong rt) \
{                                                     \
    uint16_t tempB, tempA;                            \
                                                      \
    tempB = (rt >> a) & MIPSDSP_Q0;                   \
    tempA = (rt >> b) & MIPSDSP_Q0;                   \
                                                      \
    return MIPSDSP_RETURN32_16(tempB, tempA);         \
}

PRECEU_PH(qbl, 24, 16);
PRECEU_PH(qbr, 8, 0);
PRECEU_PH(qbla, 24, 8);
PRECEU_PH(qbra, 16, 0);

#undef PRECEU_PH

#if defined(TARGET_MIPS64)
#define PRECEU_QH(name, a, b, c, d) \
target_ulong helper_preceu_qh_##name(target_ulong rt)        \
{                                                            \
    uint16_t tempD, tempC, tempB, tempA;                     \
                                                             \
    tempD = (rt >> a) & MIPSDSP_Q0;                          \
    tempC = (rt >> b) & MIPSDSP_Q0;                          \
    tempB = (rt >> c) & MIPSDSP_Q0;                          \
    tempA = (rt >> d) & MIPSDSP_Q0;                          \
                                                             \
    return MIPSDSP_RETURN64_16(tempD, tempC, tempB, tempA);  \
}

PRECEU_QH(obl, 56, 48, 40, 32);
PRECEU_QH(obr, 24, 16, 8, 0);
PRECEU_QH(obla, 56, 40, 24, 8);
PRECEU_QH(obra, 48, 32, 16, 0);

#undef PRECEU_QH

#endif

/** DSP GPR-Based Shift Sub-class insns **/
#define SHIFT_QB(name, func) \
target_ulong helper_##name##_qb(target_ulong sa, target_ulong rt) \
{                                                                    \
    uint8_t rt3, rt2, rt1, rt0;                                      \
                                                                     \
    sa = sa & 0x07;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                       \
                                                                     \
    rt3 = mipsdsp_##func(rt3, sa);                                   \
    rt2 = mipsdsp_##func(rt2, sa);                                   \
    rt1 = mipsdsp_##func(rt1, sa);                                   \
    rt0 = mipsdsp_##func(rt0, sa);                                   \
                                                                     \
    return MIPSDSP_RETURN32_8(rt3, rt2, rt1, rt0);                   \
}

#define SHIFT_QB_ENV(name, func) \
target_ulong helper_##name##_qb(target_ulong sa, target_ulong rt,\
                                CPUMIPSState *env) \
{                                                                    \
    uint8_t rt3, rt2, rt1, rt0;                                      \
                                                                     \
    sa = sa & 0x07;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_8(rt, rt3, rt2, rt1, rt0);                       \
                                                                     \
    rt3 = mipsdsp_##func(rt3, sa, env);                              \
    rt2 = mipsdsp_##func(rt2, sa, env);                              \
    rt1 = mipsdsp_##func(rt1, sa, env);                              \
    rt0 = mipsdsp_##func(rt0, sa, env);                              \
                                                                     \
    return MIPSDSP_RETURN32_8(rt3, rt2, rt1, rt0);                   \
}

SHIFT_QB_ENV(shll, lshift8);
SHIFT_QB(shrl, rshift_u8);

SHIFT_QB(shra, rashift8);
SHIFT_QB(shra_r, rnd8_rashift);

#undef SHIFT_QB
#undef SHIFT_QB_ENV

#if defined(TARGET_MIPS64)
#define SHIFT_OB(name, func) \
target_ulong helper_##name##_ob(target_ulong rt, target_ulong sa) \
{                                                                        \
    int i;                                                               \
    uint8_t rt_t[8];                                                     \
    uint64_t temp;                                                       \
                                                                         \
    sa = sa & 0x07;                                                      \
    temp = 0;                                                            \
                                                                         \
    for (i = 0; i < 8; i++) {                                            \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                          \
        rt_t[i] = mipsdsp_##func(rt_t[i], sa);                           \
        temp |= (uint64_t)rt_t[i] << (8 * i);                            \
    }                                                                    \
                                                                         \
    return temp;                                                         \
}

#define SHIFT_OB_ENV(name, func) \
target_ulong helper_##name##_ob(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                       \
{                                                                        \
    int i;                                                               \
    uint8_t rt_t[8];                                                     \
    uint64_t temp;                                                       \
                                                                         \
    sa = sa & 0x07;                                                      \
    temp = 0;                                                            \
                                                                         \
    for (i = 0; i < 8; i++) {                                            \
        rt_t[i] = (rt >> (8 * i)) & MIPSDSP_Q0;                          \
        rt_t[i] = mipsdsp_##func(rt_t[i], sa, env);                      \
        temp |= (uint64_t)rt_t[i] << (8 * i);                            \
    }                                                                    \
                                                                         \
    return temp;                                                         \
}

SHIFT_OB_ENV(shll, lshift8);
SHIFT_OB(shrl, rshift_u8);

SHIFT_OB(shra, rashift8);
SHIFT_OB(shra_r, rnd8_rashift);

#undef SHIFT_OB
#undef SHIFT_OB_ENV

#endif

#define SHIFT_PH(name, func) \
target_ulong helper_##name##_ph(target_ulong sa, target_ulong rt, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint16_t rth, rtl;                                            \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                             \
                                                                  \
    rth = mipsdsp_##func(rth, sa, env);                           \
    rtl = mipsdsp_##func(rtl, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN32_16(rth, rtl);                         \
}

SHIFT_PH(shll, lshift16);
SHIFT_PH(shll_s, sat16_lshift);

#undef SHIFT_PH

#if defined(TARGET_MIPS64)
#define SHIFT_QH(name, func) \
target_ulong helper_##name##_qh(target_ulong rt, target_ulong sa) \
{                                                                 \
    uint16_t rt3, rt2, rt1, rt0;                                  \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                   \
                                                                  \
    rt3 = mipsdsp_##func(rt3, sa);                                \
    rt2 = mipsdsp_##func(rt2, sa);                                \
    rt1 = mipsdsp_##func(rt1, sa);                                \
    rt0 = mipsdsp_##func(rt0, sa);                                \
                                                                  \
    return MIPSDSP_RETURN64_16(rt3, rt2, rt1, rt0);               \
}

#define SHIFT_QH_ENV(name, func) \
target_ulong helper_##name##_qh(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint16_t rt3, rt2, rt1, rt0;                                  \
                                                                  \
    sa = sa & 0x0F;                                               \
                                                                  \
    MIPSDSP_SPLIT64_16(rt, rt3, rt2, rt1, rt0);                   \
                                                                  \
    rt3 = mipsdsp_##func(rt3, sa, env);                           \
    rt2 = mipsdsp_##func(rt2, sa, env);                           \
    rt1 = mipsdsp_##func(rt1, sa, env);                           \
    rt0 = mipsdsp_##func(rt0, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN64_16(rt3, rt2, rt1, rt0);               \
}

SHIFT_QH_ENV(shll, lshift16);
SHIFT_QH_ENV(shll_s, sat16_lshift);

SHIFT_QH(shrl, rshift_u16);
SHIFT_QH(shra, rashift16);
SHIFT_QH(shra_r, rnd16_rashift);

#undef SHIFT_QH
#undef SHIFT_QH_ENV

#endif

#define SHIFT_W(name, func) \
target_ulong helper_##name##_w(target_ulong sa, target_ulong rt) \
{                                                                       \
    uint32_t temp;                                                      \
                                                                        \
    sa = sa & 0x1F;                                                     \
    temp = mipsdsp_##func(rt, sa);                                      \
                                                                        \
    return (target_long)(int32_t)temp;                                  \
}

#define SHIFT_W_ENV(name, func) \
target_ulong helper_##name##_w(target_ulong sa, target_ulong rt, \
                               CPUMIPSState *env) \
{                                                                       \
    uint32_t temp;                                                      \
                                                                        \
    sa = sa & 0x1F;                                                     \
    temp = mipsdsp_##func(rt, sa, env);                                 \
                                                                        \
    return (target_long)(int32_t)temp;                                  \
}

SHIFT_W_ENV(shll_s, sat32_lshift);
SHIFT_W(shra_r, rnd32_rashift);

#undef SHIFT_W
#undef SHIFT_W_ENV

#if defined(TARGET_MIPS64)
#define SHIFT_PW(name, func) \
target_ulong helper_##name##_pw(target_ulong rt, target_ulong sa) \
{                                                                 \
    uint32_t rt1, rt0;                                            \
                                                                  \
    sa = sa & 0x1F;                                               \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    rt1 = mipsdsp_##func(rt1, sa);                                \
    rt0 = mipsdsp_##func(rt0, sa);                                \
                                                                  \
    return MIPSDSP_RETURN64_32(rt1, rt0);                         \
}

#define SHIFT_PW_ENV(name, func) \
target_ulong helper_##name##_pw(target_ulong rt, target_ulong sa, \
                                CPUMIPSState *env)                \
{                                                                 \
    uint32_t rt1, rt0;                                            \
                                                                  \
    sa = sa & 0x1F;                                               \
    MIPSDSP_SPLIT64_32(rt, rt1, rt0);                             \
                                                                  \
    rt1 = mipsdsp_##func(rt1, sa, env);                           \
    rt0 = mipsdsp_##func(rt0, sa, env);                           \
                                                                  \
    return MIPSDSP_RETURN64_32(rt1, rt0);                         \
}

SHIFT_PW_ENV(shll, lshift32);
SHIFT_PW_ENV(shll_s, sat32_lshift);

SHIFT_PW(shra, rashift32);
SHIFT_PW(shra_r, rnd32_rashift);

#undef SHIFT_PW
#undef SHIFT_PW_ENV

#endif

#define SHIFT_PH(name, func) \
target_ulong helper_##name##_ph(target_ulong sa, target_ulong rt) \
{                                                                    \
    uint16_t rth, rtl;                                               \
                                                                     \
    sa = sa & 0x0F;                                                  \
                                                                     \
    MIPSDSP_SPLIT32_16(rt, rth, rtl);                                \
                                                                     \
    rth = mipsdsp_##func(rth, sa);                                   \
    rtl = mipsdsp_##func(rtl, sa);                                   \
                                                                     \
    return MIPSDSP_RETURN32_16(rth, rtl);                            \
}

SHIFT_PH(shrl, rshift_u16);
SHIFT_PH(shra, rashift16);
SHIFT_PH(shra_r, rnd16_rashift);

#undef SHIFT_PH

#undef MIPSDSP_LHI
#undef MIPSDSP_LLO
#undef MIPSDSP_HI
#undef MIPSDSP_LO
#undef MIPSDSP_Q3
#undef MIPSDSP_Q2
#undef MIPSDSP_Q1
#undef MIPSDSP_Q0

#undef MIPSDSP_SPLIT32_8
#undef MIPSDSP_SPLIT32_16

#undef MIPSDSP_RETURN32
#undef MIPSDSP_RETURN32_8
#undef MIPSDSP_RETURN32_16

#ifdef TARGET_MIPS64
#undef MIPSDSP_SPLIT64_16
#undef MIPSDSP_SPLIT64_32
#undef MIPSDSP_RETURN64_16
#undef MIPSDSP_RETURN64_32
#endif
