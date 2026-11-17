/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch emulation helpers for QEMU.
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "internals.h"
#include "qemu/crc32c.h"
#include <zlib.h>
#include "cpu-csr.h"

/* Exceptions helpers */
void helper_raise_exception(CPULoongArchState *env, uint32_t exception)
{
    do_raise_exception(env, exception, GETPC());
}

target_ulong helper_bitrev_w(target_ulong rj)
{
    return (int32_t)revbit32(rj);
}

target_ulong helper_bitrev_d(target_ulong rj)
{
    return revbit64(rj);
}

target_ulong helper_bitswap(target_ulong v)
{
    v = ((v >> 1) & (target_ulong)0x5555555555555555ULL) |
        ((v & (target_ulong)0x5555555555555555ULL) << 1);
    v = ((v >> 2) & (target_ulong)0x3333333333333333ULL) |
        ((v & (target_ulong)0x3333333333333333ULL) << 2);
    v = ((v >> 4) & (target_ulong)0x0F0F0F0F0F0F0F0FULL) |
        ((v & (target_ulong)0x0F0F0F0F0F0F0F0FULL) << 4);
    return v;
}

/* loongarch assert op */
void helper_asrtle_d(CPULoongArchState *env, target_ulong rj, target_ulong rk)
{
    if (rj > rk) {
        SET_CSR_IF(env->guest, BADV, rj);
        do_raise_exception(env, EXCCODE_BCE, GETPC());
    }
}

void helper_asrtgt_d(CPULoongArchState *env, target_ulong rj, target_ulong rk)
{
    if (rj <= rk) {
        SET_CSR_IF(env->guest, BADV, rj);
        do_raise_exception(env, EXCCODE_BCE, GETPC());
    }
}

target_ulong helper_crc32(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);

    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32(val ^ 0xffffffff, buf, sz) ^ 0xffffffff);
}

target_ulong helper_crc32c(target_ulong val, target_ulong m, uint64_t sz)
{
    uint8_t buf[8];
    target_ulong mask = ((sz * 8) == 64) ? -1ULL : ((1ULL << (sz * 8)) - 1);
    m &= mask;
    stq_le_p(buf, m);
    return (int32_t) (crc32c(val, buf, sz) ^ 0xffffffff);
}

target_ulong helper_cpucfg(CPULoongArchState *env, target_ulong rj)
{
    if (env->guest) {
        trigger_vm_exit(env);
        do_raise_exception(env, EXCCODE_GSPR, GETPC());
    }
    return rj >= ARRAY_SIZE(env->cpucfg) ? 0 : env->cpucfg[rj];
}

uint64_t helper_rdtime_d(CPULoongArchState *env)
{
#ifdef CONFIG_USER_ONLY
    return cpu_get_host_ticks();
#else
    uint64_t plv;
    LoongArchCPU *cpu = env_archcpu(env);

    plv = FIELD_EX64(GET_CSR_IF(env->guest, CRMD), CSR_CRMD, PLV);
    if (extract64(GET_CSR_IF(env->guest, MISC), R_CSR_MISC_DRDTL_SHIFT + plv, 1)) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }
    return cpu_loongarch_get_constant_timer_counter(cpu);
#endif
}

#ifndef CONFIG_USER_ONLY
void helper_ertn(CPULoongArchState *env)
{
    uint64_t csr_pplv, csr_pie;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        csr_pplv = FIELD_EX64(GET_CSR_IF(env->guest, TLBRPRMD), CSR_TLBRPRMD, PPLV);
        csr_pie = FIELD_EX64(GET_CSR_IF(env->guest, TLBRPRMD), CSR_TLBRPRMD, PIE);

        SET_CSR_IF(env->guest, TLBRERA, FIELD_DP64(GET_CSR_IF(env->guest, TLBRERA), CSR_TLBRERA, ISTLBR, 0));
        SET_CSR_IF(env->guest, CRMD, FIELD_DP64(GET_CSR_IF(env->guest, CRMD), CSR_CRMD, DA, 0));
        SET_CSR_IF(env->guest, CRMD, FIELD_DP64(GET_CSR_IF(env->guest, CRMD), CSR_CRMD, PG, 1));
        set_pc(env, GET_CSR_IF(env->guest, TLBRERA));
        qemu_log_mask(CPU_LOG_INT, "%s: TLBRERA " TARGET_FMT_lx "\n",
                      __func__, GET_CSR_IF(env->guest, TLBRERA));
    } else {
        csr_pplv = FIELD_EX64(GET_CSR_IF(env->guest, PRMD), CSR_PRMD, PPLV);
        csr_pie = FIELD_EX64(GET_CSR_IF(env->guest, PRMD), CSR_PRMD, PIE);

        set_pc(env, GET_CSR_IF(env->guest, ERA));
        qemu_log_mask(CPU_LOG_INT, "%s: ERA " TARGET_FMT_lx "\n",
                      __func__, GET_CSR_IF(env->guest, ERA));
    }
    SET_CSR_IF(env->guest, CRMD, FIELD_DP64(GET_CSR_IF(env->guest, CRMD), CSR_CRMD, PLV, csr_pplv));
    SET_CSR_IF(env->guest, CRMD, FIELD_DP64(GET_CSR_IF(env->guest, CRMD), CSR_CRMD, IE, csr_pie));

    env->lladdr = 1;
    if (will_return_to_guest(env)) {
        lbt_enter_guest(env);
        env->guest = true;
        env->CSR_GSTAT = FIELD_DP64(env->CSR_GSTAT, CSR_GSTAT, VM, 1);
        cpu_loongarch_set_guest_timer(env_archcpu(env), true);
        bql_lock();
        if (loongarch_guest_has_interrupt(env)) {
            cpu_interrupt(env_cpu(env), CPU_INTERRUPT_GUEST);
        } else {
            cpu_reset_interrupt(env_cpu(env), CPU_INTERRUPT_GUEST);
        }
        bql_unlock();
    }
}

void helper_idle(CPULoongArchState *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    do_raise_exception(env, EXCP_HLT, 0);
}

/* Hypervisor call helper */
void helper_hvcl(CPULoongArchState *env, uint32_t code)
{
    if (env->guest == 0) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
        return;
    }

    trigger_vm_exit(env);
    do_raise_exception(env, EXCCODE_HVC, GETPC());
}

void helper_gspr(CPULoongArchState *env) {
    trigger_vm_exit(env);
    do_raise_exception(env, EXCCODE_GSPR, GETPC());
}

#endif

/* ---------------- LoongArch binary translation (LBT) ---------------- */
#define LBT_CF  0x001u
#define LBT_PF  0x004u
#define LBT_AF  0x010u
#define LBT_ZF  0x040u
#define LBT_SF  0x080u
#define LBT_OF  0x800u
#define LBT_FLAGS_MASK (LBT_CF | LBT_PF | LBT_AF | LBT_ZF | LBT_SF | LBT_OF)

enum {
    LBT_OP_ADD, LBT_OP_ADC, LBT_OP_SUB, LBT_OP_SBC,
    LBT_OP_AND, LBT_OP_OR, LBT_OP_XOR, LBT_OP_INC, LBT_OP_DEC,
    LBT_OP_SHL, LBT_OP_SHR, LBT_OP_SAR, LBT_OP_ROL, LBT_OP_ROR,
    LBT_OP_RCL, LBT_OP_RCR,
};

static uint64_t lbt_mask(unsigned size)
{
    return size >= 8 ? ~0ULL : ((1ULL << (size * 8)) - 1);
}

static void lbt_set_szp(CPULoongArchState *env, uint64_t res, unsigned size,
                        uint32_t flags)
{
    uint64_t mask = lbt_mask(size);
    uint64_t sign = 1ULL << (size * 8 - 1);

    res &= mask;
    if (res == 0) {
        flags |= LBT_ZF;
    }
    if (res & sign) {
        flags |= LBT_SF;
    }
    if (!__builtin_parity((uint8_t)res)) {
        flags |= LBT_PF;
    }
    env->lbt.eflags = (env->lbt.eflags & ~LBT_FLAGS_MASK) | flags;
}

void helper_lbt_x86_alu(CPULoongArchState *env, uint64_t a, uint64_t b,
                        uint32_t op, uint32_t size)
{
    uint64_t mask = lbt_mask(size);
    uint64_t sign = 1ULL << (size * 8 - 1);
    uint64_t res = 0;
    uint32_t flags = 0;
    uint32_t cin = (env->lbt.eflags & LBT_CF) ? 1 : 0;
    unsigned bits = size * 8;

    a &= mask;
    b &= mask;

    switch (op) {
    case LBT_OP_ADD:
    case LBT_OP_ADC:
    case LBT_OP_INC: {
        uint32_t carry = (op == LBT_OP_ADC) ? cin : 0;
        unsigned __int128 s = (unsigned __int128)a + b + carry;
        uint32_t old_cf = env->lbt.eflags & LBT_CF;

        res = (uint64_t)s & mask;
        if ((s >> bits) & 1) {
            flags |= LBT_CF;
        }
        if (((~(a ^ b)) & (a ^ res)) & sign) {
            flags |= LBT_OF;
        }
        if (op == LBT_OP_INC) {
            flags = (flags & ~LBT_CF) | old_cf;
        }
        break;
    }
    case LBT_OP_SUB:
    case LBT_OP_SBC:
    case LBT_OP_DEC: {
        uint32_t borrow = (op == LBT_OP_SBC) ? cin : 0;
        unsigned __int128 d = (unsigned __int128)a - b - borrow;
        uint32_t old_cf = env->lbt.eflags & LBT_CF;

        res = (uint64_t)d & mask;
        if (d >> bits) {
            flags |= LBT_CF;
        }
        if ((a ^ b) & (a ^ res) & sign) {
            flags |= LBT_OF;
        }
        if (op == LBT_OP_DEC) {
            flags = (flags & ~LBT_CF) | old_cf;
        }
        break;
    }
    case LBT_OP_AND:
    case LBT_OP_OR:
    case LBT_OP_XOR:
        res = (op == LBT_OP_AND) ? (a & b) :
              (op == LBT_OP_OR) ? (a | b) : (a ^ b);
        flags = 0;
        break;
    case LBT_OP_SHL: {
        unsigned cnt = b & (bits - 1);
        if (cnt == 0) {
            return;
        }
        res = (a << cnt) & mask;
        flags |= ((a >> (bits - cnt)) & 1) ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= ((res >> (bits - 1)) ^ (flags & LBT_CF)) ? LBT_OF : 0;
        }
        break;
    }
    case LBT_OP_SHR: {
        unsigned cnt = b & (bits - 1);
        if (cnt == 0) {
            return;
        }
        res = a >> cnt;
        flags |= ((a >> (cnt - 1)) & 1) ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= ((a >> (bits - 1)) & 1) ? LBT_OF : 0;
        }
        break;
    }
    case LBT_OP_SAR: {
        unsigned cnt = b & (bits - 1);
        int64_t sa;
        if (cnt == 0) {
            return;
        }
        sa = (int64_t)(a << (64 - bits)) >> (64 - bits);
        res = (uint64_t)(sa >> cnt) & mask;
        flags |= ((a >> (cnt - 1)) & 1) ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= 0;
        }
        break;
    }
    case LBT_OP_ROL: {
        unsigned cnt = b & (bits - 1);
        if (cnt == 0) {
            return;
        }
        res = ((a << cnt) | (a >> (bits - cnt))) & mask;
        flags |= (res & 1) ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= (((res >> (bits - 1)) ^ (res & 1)) ? LBT_OF : 0);
        }
        env->lbt.eflags = (env->lbt.eflags & ~LBT_FLAGS_MASK) | flags;
        return;
    }
    case LBT_OP_ROR: {
        unsigned cnt = b & (bits - 1);
        if (cnt == 0) {
            return;
        }
        res = ((a >> cnt) | (a << (bits - cnt))) & mask;
        flags |= ((res >> (bits - 1)) & 1) ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= (((res >> (bits - 1)) ^ (res >> (bits - 2)) & 1)
                      ? LBT_OF : 0);
        }
        env->lbt.eflags = (env->lbt.eflags & ~LBT_FLAGS_MASK) | flags;
        return;
    }
    case LBT_OP_RCL: {
        unsigned cnt = b % (bits + 1);
        unsigned cf = cin;
        if (cnt == 0) {
            return;
        }
        for (unsigned i = 0; i < cnt; i++) {
            unsigned newcf = (a >> (bits - 1)) & 1;
            a = ((a << 1) | cf) & mask;
            cf = newcf;
        }
        res = a;
        flags |= cf ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= (((res >> (bits - 1)) ^ cf) ? LBT_OF : 0);
        }
        env->lbt.eflags = (env->lbt.eflags & ~LBT_FLAGS_MASK) | flags;
        return;
    }
    case LBT_OP_RCR: {
        unsigned cnt = b % (bits + 1);
        unsigned cf = cin;
        if (cnt == 0) {
            return;
        }
        for (unsigned i = 0; i < cnt; i++) {
            unsigned newcf = a & 1;
            a = (a >> 1) | ((uint64_t)cf << (bits - 1));
            cf = newcf;
        }
        res = a;
        flags |= cf ? LBT_CF : 0;
        if (cnt == 1) {
            flags |= (((res >> (bits - 2)) & 1) ^ ((res >> (bits - 1)) & 1)
                      ? LBT_OF : 0);
        }
        env->lbt.eflags = (env->lbt.eflags & ~LBT_FLAGS_MASK) | flags;
        return;
    }
    default:
        return;
    }

    if ((a ^ b ^ res) & 0x10) {
        flags |= LBT_AF;
    }
    lbt_set_szp(env, res, size, flags);
}

uint64_t helper_lbt_x86_mfflag(CPULoongArchState *env, uint32_t mask)
{
    uint32_t e = env->lbt.eflags;
    uint64_t out = 0;

    if (mask & 1)  out |= (uint64_t)(e & LBT_CF) >> 0;
    if (mask & 2)  out |= (uint64_t)(e & LBT_PF) >> 0;
    if (mask & 4)  out |= (uint64_t)(e & LBT_AF) << 0;
    if (mask & 8)  out |= (uint64_t)(e & LBT_ZF) >> 0;
    if (mask & 16) out |= (uint64_t)(e & LBT_SF) >> 0;
    if (mask & 32) out |= (uint64_t)(e & LBT_OF) >> 0;
    return out;
}

void helper_lbt_x86_mtflag(CPULoongArchState *env, uint64_t val, uint32_t mask)
{
    uint32_t e = env->lbt.eflags & ~LBT_FLAGS_MASK;

    if (mask & 1)  e |= (val & 1) ? LBT_CF : 0;
    if (mask & 2)  e |= (val & LBT_PF) ? LBT_PF : 0;
    if (mask & 4)  e |= (val & LBT_AF) ? LBT_AF : 0;
    if (mask & 8)  e |= (val & LBT_ZF) ? LBT_ZF : 0;
    if (mask & 16) e |= (val & LBT_SF) ? LBT_SF : 0;
    if (mask & 32) e |= (val & LBT_OF) ? LBT_OF : 0;
    env->lbt.eflags = e;
}

uint64_t helper_lbt_x86_setcc(CPULoongArchState *env, uint32_t cond)
{
    uint32_t e = env->lbt.eflags;
    int cf = !!(e & LBT_CF);
    int pf = !!(e & LBT_PF);
    int zf = !!(e & LBT_ZF);
    int sf = !!(e & LBT_SF);
    int of = !!(e & LBT_OF);

    switch (cond & 15) {
    case 0:  return !cf && !zf;
    case 1:  return !cf;
    case 2:  return cf;
    case 3:  return cf || zf;
    case 4:  return zf;
    case 5:  return !zf;
    case 6:  return !zf && (sf == of);
    case 7:  return sf == of;
    case 8:  return sf != of;
    case 9:  return zf || (sf != of);
    case 10: return sf;
    case 11: return !sf;
    case 12: return of;
    case 13: return !of;
    case 14: return pf;
    default: return !pf;
    }
}

void helper_lbt_movgr2scr(CPULoongArchState *env, uint32_t idx, uint64_t val)
{
    switch (idx & 3) {
    case 0: env->lbt.scr0 = val; break;
    case 1: env->lbt.scr1 = val; break;
    case 2: env->lbt.scr2 = val; break;
    default: env->lbt.scr3 = val; break;
    }
}

uint64_t helper_lbt_movscr2gr(CPULoongArchState *env, uint32_t idx)
{
    switch (idx & 3) {
    case 0: return env->lbt.scr0;
    case 1: return env->lbt.scr1;
    case 2: return env->lbt.scr2;
    default: return env->lbt.scr3;
    }
}

uint64_t helper_lbt_x86_mftop(CPULoongArchState *env)
{
    return env->lbt.ftop & 7;
}

void helper_lbt_x86_mttop(CPULoongArchState *env, uint32_t val)
{
    env->lbt.ftop = val & 7;
}

void helper_lbt_x86_inctop(CPULoongArchState *env)
{
    env->lbt.ftop = (env->lbt.ftop + 1) & 7;
}

void helper_lbt_x86_dectop(CPULoongArchState *env)
{
    env->lbt.ftop = (env->lbt.ftop - 1) & 7;
}

void helper_lbt_x86_settm(CPULoongArchState *env)
{
    env->lbt.tm = 1;
}

void helper_lbt_x86_clrtm(CPULoongArchState *env)
{
    env->lbt.tm = 0;
}


/* ---------------- LBT: ARM flags and arithmetic ---------------- */
enum {
    ARM_OP_ADD, ARM_OP_ADC, ARM_OP_SUB, ARM_OP_SBC,
    ARM_OP_AND, ARM_OP_OR, ARM_OP_XOR, ARM_OP_NOT,
    ARM_OP_SHL, ARM_OP_SHR, ARM_OP_SAR, ARM_OP_ROR, ARM_OP_RRX,
    ARM_OP_MOVW, ARM_OP_MOVD,
};

static int lbt_arm_cond_holds(uint32_t e, uint32_t cond)
{
    int n = !!(e & LBT_SF);
    int z = !!(e & LBT_ZF);
    int c = !!(e & LBT_CF);
    int v = !!(e & LBT_OF);

    switch (cond & 15) {
    case 0:  return z;
    case 1:  return !z;
    case 2:  return c;
    case 3:  return !c;
    case 4:  return n;
    case 5:  return !n;
    case 6:  return v;
    case 7:  return !v;
    case 8:  return c && !z;
    case 9:  return !c || z;
    case 10: return n == v;
    case 11: return n != v;
    case 12: return !z && n == v;
    case 13: return z || n != v;
    default: return 1;
    }
}

uint64_t helper_lbt_arm_cond(CPULoongArchState *env, uint32_t cond)
{
    return lbt_arm_cond_holds(env->lbt.eflags, cond) ? 1 : 0;
}

void helper_lbt_arm_alu(CPULoongArchState *env, uint64_t a, uint64_t b,
                        uint32_t op, uint32_t cond)
{
    uint32_t e = env->lbt.eflags;
    uint32_t r, lhs, rhs, cnt;
    uint64_t wide;
    int n = 0, z = 0, c = 0, v = 0;
    int set_c = 0, set_v = 0;

    if (!lbt_arm_cond_holds(e, cond)) {
        return;
    }

    lhs = (uint32_t)a;
    rhs = (uint32_t)b;

    switch (op) {
    case ARM_OP_ADD:
    case ARM_OP_ADC:
        wide = (uint64_t)lhs + rhs + ((op == ARM_OP_ADC) ? !!(e & LBT_CF) : 0);
        r = (uint32_t)wide;
        c = (wide >> 32) & 1;
        v = ((~(lhs ^ rhs)) & (lhs ^ r) & 0x80000000u) != 0;
        n = (r >> 31) & 1;
        z = r == 0;
        set_c = set_v = 1;
        break;
    case ARM_OP_SUB:
    case ARM_OP_SBC: {
        uint32_t borrow = (op == ARM_OP_SBC) ? ((e & LBT_CF) ? 0 : 1) : 0;
        uint64_t sub = (uint64_t)rhs + borrow;
        r = (uint32_t)(lhs - sub);
        c = lhs < sub;
        v = ((lhs ^ (uint32_t)sub) & (lhs ^ r) & 0x80000000u) != 0;
        n = (r >> 31) & 1;
        z = r == 0;
        set_c = set_v = 1;
        break;
    }
    case ARM_OP_AND:
        r = lhs & rhs; n = (r >> 31) & 1; z = r == 0; break;
    case ARM_OP_OR:
        r = lhs | rhs; n = (r >> 31) & 1; z = r == 0; break;
    case ARM_OP_XOR:
        r = lhs ^ rhs; n = (r >> 31) & 1; z = r == 0; break;
    case ARM_OP_NOT:
        r = ~lhs; n = (r >> 31) & 1; z = r == 0; break;
    case ARM_OP_SHL:
        cnt = rhs & 0xff;
        if (cnt == 0) { r = lhs; }
        else if (cnt < 32) { c = (lhs >> (32 - cnt)) & 1; r = lhs << cnt; }
        else if (cnt == 32) { c = lhs & 1; r = 0; }
        else { c = 0; r = 0; }
        n = (r >> 31) & 1; z = r == 0; set_c = 1; break;
    case ARM_OP_SHR:
        cnt = rhs & 0xff;
        if (cnt == 0) { r = lhs; }
        else if (cnt < 32) { r = lhs >> cnt; c = (lhs >> (cnt - 1)) & 1; }
        else if (cnt == 32) { r = 0; c = (lhs >> 31) & 1; }
        else { r = 0; c = 0; }
        n = (r >> 31) & 1; z = r == 0; set_c = 1; break;
    case ARM_OP_SAR:
        cnt = rhs & 0xff;
        if (cnt == 0) { r = lhs; }
        else if (cnt < 32) { r = (uint32_t)((int32_t)lhs >> cnt); c = (lhs >> (cnt - 1)) & 1; }
        else { c = (lhs >> 31) & 1; r = c ? 0xffffffffu : 0; }
        n = (r >> 31) & 1; z = r == 0; set_c = 1; break;
    case ARM_OP_ROR: {
        uint32_t raw = rhs & 0xff;
        cnt = rhs & 0x1f;
        r = cnt == 0 ? lhs : ((lhs >> cnt) | (lhs << (32 - cnt)));
        if (raw != 0) {
            c = cnt == 0 ? ((lhs >> 31) & 1) : ((lhs >> (cnt - 1)) & 1);
        }
        n = (r >> 31) & 1; z = r == 0; set_c = 1;
        break;
    }
    case ARM_OP_RRX:
        r = (lhs >> 1) | ((e & LBT_CF) ? 0x80000000u : 0);
        c = lhs & 1;
        n = (r >> 31) & 1; z = r == 0; set_c = 1; break;
    case ARM_OP_MOVW:
        r = lhs; n = (r >> 31) & 1; z = r == 0; break;
    case ARM_OP_MOVD: {
        uint64_t r64 = a;
        uint32_t ne = env->lbt.eflags & ~(LBT_SF | LBT_ZF);
        if (r64 & (1ULL << 63)) ne |= LBT_SF;
        if (r64 == 0) ne |= LBT_ZF;
        env->lbt.eflags = ne;
        return;
    }
    default:
        return;
    }

    e &= ~(LBT_SF | LBT_ZF);
    if (n) e |= LBT_SF;
    if (z) e |= LBT_ZF;
    if (set_c) { e &= ~LBT_CF; if (c) e |= LBT_CF; }
    if (set_v) { e &= ~LBT_OF; if (v) e |= LBT_OF; }
    env->lbt.eflags = e;
}

uint64_t helper_lbt_arm_move(CPULoongArchState *env, uint64_t src,
                             uint64_t dst, uint32_t cond)
{
    return lbt_arm_cond_holds(env->lbt.eflags, cond) ? src : dst;
}

uint64_t helper_lbt_arm_mfflag(CPULoongArchState *env, uint32_t mask)
{
    uint32_t e = env->lbt.eflags;
    uint32_t out = 0;

    if (mask & (1 << 4)) out |= ((e & LBT_SF) ? 1u : 0u) << 31;
    if (mask & (1 << 3)) out |= ((e & LBT_ZF) ? 1u : 0u) << 30;
    if (mask & (1 << 0)) out |= ((e & LBT_CF) ? 1u : 0u) << 29;
    if (mask & (1 << 5)) out |= ((e & LBT_OF) ? 1u : 0u) << 28;
    return (uint64_t)(int64_t)(int32_t)out;
}

void helper_lbt_arm_mtflag(CPULoongArchState *env, uint64_t val, uint32_t mask)
{
    uint32_t e = env->lbt.eflags;

    if (mask & (1 << 4)) { e &= ~LBT_SF; if ((val >> 31) & 1) e |= LBT_SF; }
    if (mask & (1 << 3)) { e &= ~LBT_ZF; if ((val >> 30) & 1) e |= LBT_ZF; }
    if (mask & (1 << 0)) { e &= ~LBT_CF; if ((val >> 29) & 1) e |= LBT_CF; }
    if (mask & (1 << 5)) { e &= ~LBT_OF; if ((val >> 28) & 1) e |= LBT_OF; }
    env->lbt.eflags = e;
}

void helper_lbt_x86_mul(CPULoongArchState *env, uint64_t a, uint64_t b,
                        uint32_t size, uint32_t is_signed)
{
    __int128 product;
    int overflow;
    uint64_t mask = lbt_mask(size);
    uint64_t sign = 1ULL << (size * 8 - 1);

    if (is_signed) {
        int64_t sa = (int64_t)(a << (64 - size * 8)) >> (64 - size * 8);
        int64_t sb = (int64_t)(b << (64 - size * 8)) >> (64 - size * 8);
        product = (__int128)sa * sb;
        overflow = product < -((__int128)1 << (size * 8 - 1)) ||
                   product > (((__int128)1 << (size * 8 - 1)) - 1);
    } else {
        unsigned __int128 p = (unsigned __int128)(a & mask) * (b & mask);
        product = (__int128)p;
        overflow = (p >> (size * 8)) != 0;
    }
    (void)sign;
    env->lbt.eflags &= ~(LBT_CF | LBT_OF | LBT_SF | LBT_ZF | LBT_AF | LBT_PF);
    if (overflow) {
        env->lbt.eflags |= LBT_CF | LBT_OF;
    }
}

uint64_t helper_lbt_x86_setloop(CPULoongArchState *env, uint64_t counter,
                                uint32_t loopne)
{
    uint32_t e = env->lbt.eflags;
    int zf = !!(e & LBT_ZF);
    return (counter != 0 && (loopne ? !zf : zf)) ? 1 : 0;
}

uint64_t helper_lbt_int_adc(CPULoongArchState *env, uint64_t a, uint64_t b,
                            uint32_t size, uint32_t sub)
{
    uint64_t mask = lbt_mask(size);
    uint32_t cf = !!(env->lbt.eflags & LBT_CF);
    uint64_t r;
    int sign;

    if (sub) {
        r = (a - b - cf) & mask;
    } else {
        r = (a + b + cf) & mask;
    }
    sign = (r >> (size * 8 - 1)) & 1;
    if (sign) {
        r |= ~mask;
    }
    return r;
}

uint64_t helper_lbt_int_rotr(CPULoongArchState *env, uint64_t a, uint64_t b,
                             uint32_t size)
{
    unsigned bits = size * 8;
    uint64_t mask = lbt_mask(size);
    unsigned c = b & (bits - 1);
    uint64_t v = a & mask;

    return ((v >> c) | (v << ((bits - c) & (bits - 1)))) & mask;
}

uint64_t helper_lbt_int_rcr(CPULoongArchState *env, uint64_t a, uint64_t b,
                            uint32_t size)
{
    unsigned bits = size * 8;
    uint64_t mask = lbt_mask(size);
    unsigned c = b % (bits + 1);
    uint64_t v = a & mask;
    unsigned cf = !!(env->lbt.eflags & LBT_CF);

    for (unsigned i = 0; i < c; i++) {
        unsigned newcf = v & 1;
        v = (v >> 1) | ((uint64_t)cf << (bits - 1));
        cf = newcf;
    }
    return v & mask;
}

uint64_t helper_lbt_fcvt_ld_d(CPULoongArchState *env, uint64_t d)
{
    unsigned exp_d = (d >> 52) & 0x7ff;
    uint64_t man_d = d & 0x000fffffffffffffULL;
    unsigned j_bit;
    uint64_t man_x87;

    if (exp_d == 0 && man_d == 0) {
        j_bit = 0;
        man_x87 = 0;
    } else if (exp_d == 0) {
        int p = 63 - __builtin_clzll(man_d);
        j_bit = 1;
        man_x87 = (man_d << (63 - p)) & 0x7fffffffffffffffULL;
    } else if (exp_d == 0x7ff && man_d == 0) {
        j_bit = 1;
        man_x87 = 0;
    } else if (exp_d == 0x7ff) {
        j_bit = 1;
        man_x87 = (man_d << 11) | (1ULL << 62);
    } else {
        j_bit = 1;
        man_x87 = man_d << 11;
    }
    return ((uint64_t)j_bit << 63) | man_x87;
}

uint64_t helper_lbt_fcvt_ud_d(CPULoongArchState *env, uint64_t d)
{
    unsigned sign = (d >> 63) & 1;
    unsigned exp_d = (d >> 52) & 0x7ff;
    uint64_t man_d = d & 0x000fffffffffffffULL;
    int64_t exp_x87;

    if (exp_d == 0x7ff) {
        exp_x87 = 0x7fff;
    } else if (exp_d == 0 && man_d == 0) {
        exp_x87 = 0;
    } else if (exp_d == 0) {
        int p = 63 - __builtin_clzll(man_d);
        exp_x87 = -1074 + p + 16383;
    } else {
        exp_x87 = (int64_t)exp_d - 1023 + 16383;
    }
    return ((uint64_t)sign << 15) | ((uint64_t)exp_x87 & 0x7fff);
}

uint64_t helper_lbt_fcvt_d_ld(CPULoongArchState *env, uint64_t lo, uint64_t hi)
{
    unsigned sign = (hi >> 15) & 1;
    int64_t exp_x87 = hi & 0x7fff;
    unsigned j_bit = (lo >> 63) & 1;
    uint64_t man_x87 = lo & 0x7fffffffffffffffULL;
    uint64_t full = ((uint64_t)j_bit << 63) | man_x87;
    unsigned exp_d;
    uint64_t man_d;

    if (exp_x87 == 0x7fff && j_bit) {
        man_d = man_x87 >> 11;
        man_d |= (1ULL << 51);
        exp_d = 0x7ff;
    } else if (exp_x87 == 0) {
        exp_d = 0;
        man_d = full >> 11;
    } else {
        exp_d = exp_x87 - 16383 + 1023;
        man_d = (full >> 11) & 0x000fffffffffffffULL;
    }
    return ((uint64_t)sign << 63) | ((uint64_t)exp_d << 52) | man_d;
}

uint64_t helper_lbt_x86_settag(CPULoongArchState *env, uint64_t tag,
                               uint32_t imm1, uint32_t imm2)
{
    unsigned op = imm1 & 7;
    unsigned bit = imm2 & 15;

    if (op == 0) {
        tag |= 1ULL << bit;
    } else if (op == 1) {
        tag &= ~(1ULL << bit);
    } else {
        tag ^= 1ULL << bit;
    }
    return tag;
}

uint64_t helper_lbt_jiscr(CPULoongArchState *env, uint32_t imm,
                          uint32_t save_pc, uint64_t pc)
{
    uint64_t target = env->lbt.scr0 + (int64_t)(int32_t)imm * 4;

    if (save_pc) {
        env->lbt.scr0 = pc;
    }
    return target;
}

static inline uint64_t lbt_mips_lmask(uint64_t addr, unsigned size)
{
    unsigned mask = size - 1;
    return (addr & mask) ^ mask;
}

uint64_t helper_lbt_mips_ldl(CPULoongArchState *env, uint64_t addr,
                             uint64_t old, uint32_t size, uint32_t mem_idx)
{
    unsigned sizem1 = size - 1;
    unsigned shift = ((addr & sizem1) ^ sizem1) * 8;
    uint64_t a = addr & ~(uint64_t)sizem1;
    uint64_t mem;

    cpu_ldub_mmuidx_ra(env, addr, mem_idx, GETPC());
    mem = size == 4 ? cpu_ldl_le_mmuidx_ra(env, a, mem_idx, GETPC())
                    : cpu_ldq_le_mmuidx_ra(env, a, mem_idx, GETPC());
    return (mem << shift) | (old & ~(~0ULL << shift));
}

uint64_t helper_lbt_mips_ldr(CPULoongArchState *env, uint64_t addr,
                             uint64_t old, uint32_t size, uint32_t mem_idx)
{
    unsigned sizem1 = size - 1;
    unsigned shift = (addr & sizem1) * 8;
    uint64_t a = addr & ~(uint64_t)sizem1;
    uint64_t mem;

    cpu_ldub_mmuidx_ra(env, addr, mem_idx, GETPC());
    mem = size == 4 ? cpu_ldl_le_mmuidx_ra(env, a, mem_idx, GETPC())
                    : cpu_ldq_le_mmuidx_ra(env, a, mem_idx, GETPC());
    mem >>= shift;
    return mem | (old & (~1ULL << (size * 8 - 1 - shift)));
}

void helper_lbt_mips_stl(CPULoongArchState *env, uint64_t val, uint64_t addr,
                         uint32_t size, uint32_t mem_idx)
{
    unsigned sizem1 = size - 1;
    uint64_t lmask = lbt_mips_lmask(addr, size);
    int dir = -1;
    uint64_t v = val;

    cpu_stb_mmuidx_ra(env, addr, (uint8_t)(v >> (size * 8 - 8)), mem_idx, GETPC());
    for (unsigned i = 1; i < size; i++) {
        if (lmask <= sizem1 - i) {
            cpu_stb_mmuidx_ra(env, addr + dir * (int)i,
                              (uint8_t)(v >> (size * 8 - 8 - 8 * i)),
                              mem_idx, GETPC());
        }
    }
}

void helper_lbt_mips_str(CPULoongArchState *env, uint64_t val, uint64_t addr,
                         uint32_t size, uint32_t mem_idx)
{
    unsigned sizem1 = size - 1;
    uint64_t lmask = lbt_mips_lmask(addr, size);
    int dir = -1;
    uint64_t v = val;

    cpu_stb_mmuidx_ra(env, addr, (uint8_t)v, mem_idx, GETPC());
    for (unsigned i = 1; i < size; i++) {
        if (lmask >= i) {
            cpu_stb_mmuidx_ra(env, addr - dir * (int)i,
                              (uint8_t)(v >> (8 * i)),
                              mem_idx, GETPC());
        }
    }
}
