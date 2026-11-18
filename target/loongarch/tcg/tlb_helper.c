/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU LoongArch TLB helpers
 *
 * Copyright (c) 2021 Loongson Technology Corporation Limited
 *
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"

#include "cpu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/page-protection.h"
#include "exec/cpu_ldst.h"
#include "exec/log.h"
#include "cpu-csr.h"

static inline bool tlb_entry_matches_gid(const LoongArchTLB *tlb, uint8_t gid)
{
    uint8_t entry_gid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, GID);
    return entry_gid == gid;
}

/*
 * Bank an operation targets.  It is chosen by the operation, never by the
 * mode in which the helper happens to run: gtlbwr/gtlbfill execute in host
 * mode yet must address the guest bank.
 */
static inline LoongArchTLB *tlb_bank_base(CPULoongArchState *env, bool guest)
{
    return guest ? env->gtlb : env->tlb;
}

/* Entries of a bank all carry that bank's GID; anything else is a stale tag. */
static inline bool tlb_entry_in_bank(CPULoongArchState *env,
                                     const LoongArchTLB *tlb, bool guest)
{
    return tlb_entry_matches_gid(tlb, get_bank_gid(env, guest));
}

static void get_dir_base_width(CPULoongArchState *env, uint64_t *dir_base,
                               uint64_t *dir_width, target_ulong level)
{
    uint64_t pwcl, pwch;
    pwcl = GET_CSR_IF(env->guest, PWCL);
    pwch = GET_CSR_IF(env->guest, PWCH);
    switch (level) {
    case 1:
        *dir_base = FIELD_EX64(pwcl, CSR_PWCL, DIR1_BASE);
        *dir_width = FIELD_EX64(pwcl, CSR_PWCL, DIR1_WIDTH);
        break;
    case 2:
        *dir_base = FIELD_EX64(pwcl, CSR_PWCL, DIR2_BASE);
        *dir_width = FIELD_EX64(pwcl, CSR_PWCL, DIR2_WIDTH);
        break;
    case 3:
        *dir_base = FIELD_EX64(pwch, CSR_PWCH, DIR3_BASE);
        *dir_width = FIELD_EX64(pwch, CSR_PWCH, DIR3_WIDTH);
        break;
    case 4:
        *dir_base = FIELD_EX64(pwch, CSR_PWCH, DIR4_BASE);
        *dir_width = FIELD_EX64(pwch, CSR_PWCH, DIR4_WIDTH);
        break;
    default:
        /* level may be zero for ldpte */
        *dir_base = FIELD_EX64(pwcl, CSR_PWCL, PTBASE);
        *dir_width = FIELD_EX64(pwcl, CSR_PWCL, PTWIDTH);
        break;
    }
}

static void raise_mmu_exception(CPULoongArchState *env, target_ulong address,
                                MMUAccessType access_type, int tlb_error)
{
    CPUState *cs = env_cpu(env);

    if (env->guest && tlb_error > TLBRET_HOST_MATCH) {
        trigger_vm_exit(env);
    }

    int real_guest = !env->vm_exit && env->guest;

    switch (tlb_error) {
    default:
    case TLBRET_BADADDR:
    case TLBRET_HOST_BADADDR:
        cs->exception_index = access_type == MMU_INST_FETCH
                              ? EXCCODE_ADEF : EXCCODE_ADEM;
        break;
    case TLBRET_NOMATCH:
    case TLBRET_HOST_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        SET_CSR_IF(real_guest, TLBRERA,
                   FIELD_DP64(GET_CSR_IF(real_guest, TLBRERA), CSR_TLBRERA, ISTLBR, 1));
        break;
    case TLBRET_INVALID:
    case TLBRET_HOST_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_LOAD) {
            cs->exception_index = EXCCODE_PIL;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PIS;
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PIF;
        }
        break;
    case TLBRET_DIRTY:
    case TLBRET_HOST_DIRTY:
        /* TLB match but 'D' bit is cleared */
        cs->exception_index = EXCCODE_PME;
        break;
    case TLBRET_XI:
    case TLBRET_HOST_XI:
        /* Execute-Inhibit Exception */
        cs->exception_index = EXCCODE_PNX;
        break;
    case TLBRET_RI:
    case TLBRET_HOST_RI:
        /* Read-Inhibit Exception */
        cs->exception_index = EXCCODE_PNR;
        break;
    case TLBRET_PE:
    case TLBRET_HOST_PE:
        /* Privileged Exception */
        cs->exception_index = EXCCODE_PPI;
        break;
    }

    if (tlb_error == TLBRET_NOMATCH
        || tlb_error == TLBRET_HOST_NOMATCH) {
        SET_CSR_IF(real_guest, TLBRBADV, address);
        if (is_la64(env)) {
            SET_CSR_IF(real_guest, TLBREHI, FIELD_DP64(GET_CSR_IF(real_guest, TLBREHI), CSR_TLBREHI_64,
                                          VPPN, extract64(address, 13, 35)));
        } else {
            SET_CSR_IF(real_guest, TLBREHI, FIELD_DP64(GET_CSR_IF(real_guest, TLBREHI), CSR_TLBREHI_32,
                                          VPPN, extract64(address, 13, 19)));
        }
    } else {
        if (!FIELD_EX64(env->CSR_DBG, CSR_DBG, DST)) {
            SET_CSR_IF(real_guest, BADV, address);
        }
        SET_CSR_IF(real_guest, TLBEHI, address & (TARGET_PAGE_MASK << 1));
    }
}

static void invalidate_tlb_entry(CPULoongArchState *env, int index, bool guest)
{
    target_ulong addr, mask, pagesize;
    CPUState *cs = env_cpu(env);
    uint16_t idxmap = 0;
    uint8_t tlb_ps;
    LoongArchTLB *tlb = guest ? &env->gtlb[index] : &env->tlb[index];

    uint8_t tlb_v0 = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, V);
    uint8_t tlb_v1 = FIELD_EX64(tlb->tlb_entry1, TLBENTRY, V);
    uint64_t tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
    uint8_t tlb_gid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, GID);

    if (guest) {
        for (int i = MMU_GUEST_IDX; i <= MMU_GUEST_DA_IDX; i++) {
            idxmap |= 1u << i;
        }
    } else if (tlb_gid) {
        tlb_flush(cs);
        return;
    } else {
        idxmap = 1u << cpu_mmu_index(cs, false);
    }

    if (index >= LOONGARCH_STLB) {
        tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    } else {
        tlb_ps = FIELD_EX64(GET_CSR_IF(guest, STLBPS), CSR_STLBPS, PS);
    }
    pagesize = MAKE_64BIT_MASK(tlb_ps, 1);
    mask = MAKE_64BIT_MASK(0, tlb_ps + 1);

    if (tlb_v0) {
        addr = (tlb_vppn << R_TLB_MISC_VPPN_SHIFT) & ~mask;    /* even */
        tlb_flush_range_by_mmuidx(cs, addr, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }

    if (tlb_v1) {
        addr = ((tlb_vppn << R_TLB_MISC_VPPN_SHIFT) & ~mask) | pagesize;    /* odd */
        tlb_flush_range_by_mmuidx(cs, addr, pagesize,
                                  idxmap, TARGET_LONG_BITS);
    }
}

static void invalidate_tlb(CPULoongArchState *env, int index, bool guest)
{
    LoongArchTLB *tlb;
    uint16_t csr_asid, tlb_asid, tlb_g;

    csr_asid = FIELD_EX64(GET_CSR_IF(guest, ASID), CSR_ASID, ASID);
    tlb = guest ? &env->gtlb[index] : &env->tlb[index];

    if (!guest && FIELD_EX64(tlb->tlb_misc, TLB_MISC, GID)) {
        invalidate_tlb_entry(env, index, guest);
        return;
    }

    tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
    tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
    if (tlb_g == 0 && tlb_asid != csr_asid) {
        return;
    }
    invalidate_tlb_entry(env, index, guest);
}

static void fill_tlb_entry(CPULoongArchState *env, int index, bool guest)
{
    LoongArchTLB *tlb = guest ? &env->gtlb[index] : &env->tlb[index];
    uint64_t lo0, lo1, csr_vppn;
    uint16_t csr_asid;
    uint8_t csr_ps;

    if (FIELD_EX64(GET_CSR_IF(guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        csr_ps = FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI, PS);
        if (is_la64(env)) {
            csr_vppn = FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI_64, VPPN);
        } else {
            csr_vppn = FIELD_EX64(GET_CSR_IF(guest, TLBREHI), CSR_TLBREHI_32, VPPN);
        }
        lo0 = GET_CSR_IF(guest, TLBRELO0);
        lo1 = GET_CSR_IF(guest, TLBRELO1);
    } else {
        csr_ps = FIELD_EX64(GET_CSR_IF(guest, TLBIDX), CSR_TLBIDX, PS);
        if (is_la64(env)) {
            csr_vppn = FIELD_EX64(GET_CSR_IF(guest, TLBEHI), CSR_TLBEHI_64, VPPN);
        } else {
            csr_vppn = FIELD_EX64(GET_CSR_IF(guest, TLBEHI), CSR_TLBEHI_32, VPPN);
        }
        lo0 = GET_CSR_IF(guest, TLBELO0);
        lo1 = GET_CSR_IF(guest, TLBELO1);
    }

    if (csr_ps == 0) {
        qemu_log_mask(CPU_LOG_MMU, "page size is 0\n");
    }

    /* Only MTLB has the ps fields */
    if (index >= LOONGARCH_STLB) {
        tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, PS, csr_ps);
    }

    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, VPPN, csr_vppn);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 1);
    csr_asid = FIELD_EX64(guest ? env->GCSR_ASID : env->CSR_ASID, CSR_ASID, ASID);
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, ASID, csr_asid);
    /*
     * The GID is a property of the bank being filled: a guest entry belongs to
     * the guest context (gtlbwr/gtlbfill run in host mode, so get_tgid() would
     * have tagged it with the host GID).
     */
    tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, GID,
                               get_bank_gid(env, guest));

    tlb->tlb_entry0 = lo0;
    tlb->tlb_entry1 = lo1;

}

/* Return an random value between low and high */
static uint32_t get_random_tlb(uint32_t low, uint32_t high)
{
    uint32_t val;

    qemu_guest_getrandom_nofail(&val, sizeof(val));
    return val % (high - low + 1) + low;
}

void helper_tlbsrch(CPULoongArchState *env)
{
    int index, match;
    uint64_t search_ehi;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBRERA),
            CSR_TLBRERA, ISTLBR)) {
        search_ehi = GET_CSR_IF(env->guest, TLBREHI);
    } else {
        /* Use effective CSR for virtualization support */
        search_ehi = GET_CSR_IF(env->guest, TLBEHI);
    }

    /* Search only in TLB entries that belong to current guest context */
    match = loongarch_tlb_search(env, search_ehi, &index, env->guest,
                                 get_bank_gid(env, env->guest));

    if (match) {
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX, index));
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 0));
        return;
    }

    SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 1));
}

void helper_gtlbsrch(CPULoongArchState *env)
{
    int index, match;
    uint64_t search_ehi;

    if (env->guest) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
    }

    if (FIELD_EX64(env->GCSR_TLBRERA,
            CSR_TLBRERA, ISTLBR)) {
        search_ehi = env->GCSR_TLBREHI;
    } else {
        /* Use effective CSR for virtualization support */
        search_ehi = env->GCSR_TLBEHI;
    }

    /* Search only in TLB entries that belong to current guest context */
    match = loongarch_tlb_search(env, search_ehi, &index, true,
                                 get_bank_gid(env, true));

    if (match) {
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX, index);
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 0);
        return;
    }

    env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 1);
}

void helper_tlbrd(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int index;
    uint8_t tlb_ps, tlb_e;

    index = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX);
    tlb = env->guest ? &env->gtlb[index] : &env->tlb[index];

    /* Check if TLB entry belongs to current guest context */
    if (env->guest && !tlb_entry_in_bank(env, tlb, true)) {
        /* Invalid TLB entry for current guest */
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 1));
        SET_CSR_IF(env->guest, ASID, FIELD_DP64(GET_CSR_IF(env->guest, ASID), CSR_ASID, ASID, 0));
        SET_CSR_IF(env->guest, TLBEHI, 0);
        SET_CSR_IF(env->guest, TLBELO0, 0);
        SET_CSR_IF(env->guest, TLBELO1, 0);
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, PS, 0));
        return;
    }

    if (index >= LOONGARCH_STLB) {
        tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    } else {
        tlb_ps = FIELD_EX64(GET_CSR_IF(env->guest, STLBPS), CSR_STLBPS, PS);
    }
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

    if (!tlb_e) {
        /* Invalid TLB entry*/
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 1));
        SET_CSR_IF(env->guest, ASID, FIELD_DP64(GET_CSR_IF(env->guest, ASID), CSR_ASID, ASID, 0));
        SET_CSR_IF(env->guest, TLBEHI, 0);
        SET_CSR_IF(env->guest, TLBELO0, 0);
        SET_CSR_IF(env->guest, TLBELO1, 0);
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, PS, 0));
    } else {
        /* Valid TLB entry */
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE, 0));
        SET_CSR_IF(env->guest, TLBIDX, FIELD_DP64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, PS, (tlb_ps & 0x3f)));
        SET_CSR_IF(env->guest, TLBEHI, FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN) << R_TLB_MISC_VPPN_SHIFT);
        SET_CSR_IF(env->guest, TLBELO0, tlb->tlb_entry0);
        SET_CSR_IF(env->guest, TLBELO1, tlb->tlb_entry1);
    }
}

void helper_gtlbrd(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int index;
    uint8_t tlb_ps, tlb_e;

    if (env->guest) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
    }

    index = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX);
    tlb = &env->gtlb[index];

    /* Check if TLB entry belongs to current guest context */
    if (!tlb_entry_in_bank(env, tlb, true)) {
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 1);
        env->GCSR_ASID = FIELD_DP64(env->GCSR_ASID, CSR_ASID, ASID, 0);
        env->GCSR_TLBEHI = 0;
        env->GCSR_TLBELO0 = 0;
        env->GCSR_TLBELO1 = 0;
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, PS, 0);
        return;
    }

    if (index >= LOONGARCH_STLB) {
        tlb_ps = FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS);
    } else {
        tlb_ps = FIELD_EX64(env->GCSR_STLBPS, CSR_STLBPS, PS);
    }
    tlb_e = FIELD_EX64(tlb->tlb_misc, TLB_MISC, E);

    if (!tlb_e) {
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 1);
        env->GCSR_ASID = FIELD_DP64(env->GCSR_ASID, CSR_ASID, ASID, 0);
        env->GCSR_TLBEHI = 0;
        env->GCSR_TLBELO0 = 0;
        env->GCSR_TLBELO1 = 0;
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, PS, 0);
    } else {
        /* Valid TLB entry */
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, NE, 0);
        env->GCSR_TLBIDX = FIELD_DP64(env->GCSR_TLBIDX, CSR_TLBIDX, PS, (tlb_ps & 0x3f));
        env->GCSR_TLBEHI = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN) << R_TLB_MISC_VPPN_SHIFT;
        env->GCSR_TLBELO0 = tlb->tlb_entry0;
        env->GCSR_TLBELO1 = tlb->tlb_entry1;
    }
}

void helper_tlbwr(CPULoongArchState *env)
{
    int index = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX);
    LoongArchTLB *tlb = env->guest ? &env->gtlb[index] : &env->tlb[index];

    invalidate_tlb(env, index, env->guest);

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, NE)) {
        tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                              TLB_MISC, E, 0);
        return;
    }

    fill_tlb_entry(env, index, env->guest);
}

void helper_gtlbwr(CPULoongArchState *env)
{
    int index = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX);
    LoongArchTLB *tlb = &env->gtlb[index];

    if (env->guest) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
    }

    invalidate_tlb(env, index, true);

    if (FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, NE)) {
        tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                              TLB_MISC, E, 0);
        return;
    }

    fill_tlb_entry(env, index, true);
}

void helper_tlbfill(CPULoongArchState *env)
{
    uint64_t address, entryhi;
    int index, set, stlb_idx;
    uint16_t pagesize, stlb_ps;

    if (FIELD_EX64(GET_CSR_IF(env->guest, TLBRERA), CSR_TLBRERA, ISTLBR)) {
        entryhi = GET_CSR_IF(env->guest, TLBREHI);
        pagesize = FIELD_EX64(GET_CSR_IF(env->guest, TLBREHI), CSR_TLBREHI, PS);
    } else {
        /* Use effective CSR for virtualization support */
        entryhi = GET_CSR_IF(env->guest, TLBEHI);
        pagesize = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, PS);
    }

    stlb_ps = FIELD_EX64(GET_CSR_IF(env->guest, STLBPS), CSR_STLBPS, PS);

    if (pagesize == stlb_ps) {
        /* Only write into STLB bits [47:13] */
        address = entryhi & ~MAKE_64BIT_MASK(0, R_CSR_TLBEHI_64_VPPN_SHIFT);

        /* Choose one set ramdomly */
        set = get_random_tlb(0, 7);

        /* Index in one set */
        stlb_idx = (address >> (stlb_ps + 1)) & 0xff; /* [0,255] */

        index = set * 256 + stlb_idx;
    } else {
        /* Only write into MTLB */
        index = get_random_tlb(LOONGARCH_STLB, LOONGARCH_TLB_MAX - 1);
    }

    invalidate_tlb(env, index, env->guest);
    fill_tlb_entry(env, index, env->guest);
}

void helper_gtlbfill(CPULoongArchState *env)
{
    uint64_t address, entryhi;
    int index, set, stlb_idx;
    uint16_t pagesize, stlb_ps;

    if (env->guest) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
    }

    if (FIELD_EX64(env->GCSR_TLBRERA, CSR_TLBRERA, ISTLBR)) {
        entryhi = env->GCSR_TLBREHI;
        pagesize = FIELD_EX64(env->GCSR_TLBREHI, CSR_TLBREHI, PS);
    } else {
        /* Use effective CSR for virtualization support */
        entryhi = env->GCSR_TLBEHI;
        pagesize = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, PS);
    }

    stlb_ps = FIELD_EX64(env->GCSR_STLBPS, CSR_STLBPS, PS);

    if (pagesize == stlb_ps) {
        /* Only write into STLB bits [47:13] */
        address = entryhi & ~MAKE_64BIT_MASK(0, R_CSR_TLBEHI_64_VPPN_SHIFT);

        /* Choose one set ramdomly */
        set = get_random_tlb(0, 7);

        /* Index in one set */
        stlb_idx = (address >> (stlb_ps + 1)) & 0xff; /* [0,255] */

        index = set * 256 + stlb_idx;
    } else {
        /* Only write into MTLB */
        index = get_random_tlb(LOONGARCH_STLB, LOONGARCH_TLB_MAX - 1);
    }

    invalidate_tlb(env, index, true);
    fill_tlb_entry(env, index, true);
}

void helper_tlbclr(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int i, index;
    uint16_t csr_asid, tlb_asid, tlb_g;

    /* Use effective CSR for virtualization support */
    csr_asid = FIELD_EX64(GET_CSR_IF(env->guest, ASID), CSR_ASID, ASID);
    index = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            tlb = env->guest ? &env->gtlb[i * 256 + (index % 256)] : &env->tlb[i * 256 + (index % 256)];

            /* Only clear entries belonging to current guest */
            if (env->guest && !tlb_entry_in_bank(env, tlb, true)) {
                continue;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = env->guest ? &env->gtlb[i] : &env->tlb[i];

            /* Only clear entries belonging to current guest */
            if (env->guest && !tlb_entry_in_bank(env, tlb, true)) {
                continue;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

/* Guest TLB clear helper */
void helper_gtlbclr(CPULoongArchState *env)
{
    LoongArchTLB *tlb;
    int i, index;
    uint16_t csr_asid, tlb_asid, tlb_g;

    if (env->guest) {
        do_raise_exception(env, EXCCODE_INE, GETPC());
    }

    /* Use effective CSR for virtualization support */
    csr_asid = FIELD_EX64(env->GCSR_ASID, CSR_ASID, ASID);
    index = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX);

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            tlb = &env->gtlb[i * 256 + (index % 256)];

            /* Only clear entries belonging to current guest */
            if (!tlb_entry_in_bank(env, tlb, true)) {
                continue;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = &env->gtlb[i];

            /* Only clear entries belonging to current guest */
            if (!tlb_entry_in_bank(env, tlb, true)) {
                continue;
            }

            tlb_asid = FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
            tlb_g = FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
            if (!tlb_g && tlb_asid == csr_asid) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_tlbflush(CPULoongArchState *env)
{
    int i, index;

    index = FIELD_EX64(GET_CSR_IF(env->guest, TLBIDX), CSR_TLBIDX, INDEX);
    LoongArchTLB *tlb;

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            int s_idx = i * 256 + (index % 256);
            tlb = env->guest ? &env->gtlb[s_idx] : &env->tlb[s_idx];

            if (env->guest == 0 || tlb_entry_in_bank(env, tlb, true)) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                                      TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = env->guest ? &env->gtlb[i] : &env->tlb[i];
            /* Only flush entries belonging to current guest */
            if (env->guest == 0 || tlb_entry_in_bank(env, tlb, true)) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                                  TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

void helper_gtlbflush(CPULoongArchState *env)
{
    int i, index;

    if (env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
        return;
    }

    index = FIELD_EX64(env->GCSR_TLBIDX, CSR_TLBIDX, INDEX);
    LoongArchTLB *tlb;

    if (index < LOONGARCH_STLB) {
        /* STLB. One line per operation */
        for (i = 0; i < 8; i++) {
            int s_idx = i * 256 + (index % 256);
            tlb = &env->gtlb[s_idx];

            /* Only flush entries belonging to current guest */
            if (tlb_entry_in_bank(env, tlb, true)) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                                      TLB_MISC, E, 0);
            }
        }
    } else if (index < LOONGARCH_TLB_MAX) {
        /* All MTLB entries */
        for (i = LOONGARCH_STLB; i < LOONGARCH_TLB_MAX; i++) {
            tlb = &env->gtlb[i];
            /* Only flush entries belonging to current guest */
            if (tlb_entry_in_bank(env, tlb, true)) {
                tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc,
                                                  TLB_MISC, E, 0);
            }
        }
    }

    tlb_flush(env_cpu(env));
}

/*
 * INVTLB scoping rules (LoongArch manual 4.2.4.7 for 0x0-0x6, LVZ for the
 * rest):
 *
 *   0x0-0x6  act on the bank currently in use and are filtered by the GID of
 *            that bank, so a host sweep can never clear guest entries.
 *   0x9      clear the entries of the guest bank tagged with GID = rj.info[23:16]
 *   0xa-0xe  same, further restricted to G = 1 / G = 0 / ASID / VA
 *   0x10     clear the whole guest bank, any GID (GVA -> GPA mappings)
 *   0x11     clear the whole host bank, any GID (GPA -> HPA mappings)
 *   0x12     clear both banks, any GID
 *   0x13-0x16 same three scopes as 0x10-0x12 but filtered by GID = rj.info[23:16]
 *
 * The bank is chosen by the opcode (and, for 0x0-0x6, by the mode in use),
 * never by the GID an entry happens to carry: bank separation is what makes
 * the two arrays independent.
 */
typedef bool (*invtlb_pred)(const LoongArchTLB *tlb, void *opaque);

/* 0x9-0x16 read GTLBC/GSTAT through the host view, so they are host-only. */
static inline bool invtlb_op_is_lvz(uint32_t op)
{
    return op >= 0x9 && op <= 0x16;
}

static void invtlb_sweep(CPULoongArchState *env, bool guest, bool gid_all,
                         uint8_t gid, invtlb_pred pred, void *opaque)
{
    LoongArchTLB *bank = tlb_bank_base(env, guest);

    for (int i = 0; i < LOONGARCH_TLB_MAX; i++) {
        LoongArchTLB *tlb = &bank[i];

        if (!gid_all && !tlb_entry_matches_gid(tlb, gid)) {
            continue;
        }
        if (pred && !pred(tlb, opaque)) {
            continue;
        }
        tlb->tlb_misc = FIELD_DP64(tlb->tlb_misc, TLB_MISC, E, 0);
    }
}

/* rj[23:16] holds the target GID of the LVZ forms, rj[9:0] the ASID. */
static inline uint8_t invtlb_op_gid(target_ulong info)
{
    return (info >> 16) & 0xff;
}

static inline uint16_t invtlb_op_asid(target_ulong info)
{
    return info & R_CSR_ASID_ASID_MASK;
}

static inline uint8_t invtlb_entry_g(const LoongArchTLB *tlb)
{
    return FIELD_EX64(tlb->tlb_entry0, TLBENTRY, G);
}

static inline uint16_t invtlb_entry_asid(const LoongArchTLB *tlb)
{
    return FIELD_EX64(tlb->tlb_misc, TLB_MISC, ASID);
}

/* Per-entry predicates for the constrained forms. */
typedef struct InvtlbMatch {
    CPULoongArchState *env;
    bool guest;
    uint16_t asid;
    target_ulong addr;
    uint8_t g;
} InvtlbMatch;

static inline bool invtlb_entry_matches_va(CPULoongArchState *env,
                                           const LoongArchTLB *tlb, int index,
                                           bool guest, target_ulong addr)
{
    uint8_t tlb_ps = (index >= LOONGARCH_STLB)
        ? FIELD_EX64(tlb->tlb_misc, TLB_MISC, PS)
        : FIELD_EX64(GET_CSR_IF(guest, STLBPS), CSR_STLBPS, PS);
    uint64_t tlb_vppn = FIELD_EX64(tlb->tlb_misc, TLB_MISC, VPPN);
    uint64_t vpn;
    int compare_shift;

    /* Same convention as loongarch_tlb_search(). */
    if (tlb_ps >= 48) {
        return false;
    }
    compare_shift = (int)tlb_ps + 1 - R_TLB_MISC_VPPN_SHIFT;
    if (compare_shift < 0) {
        return false;
    }
    vpn = (addr & TARGET_VIRT_MASK) >> (tlb_ps + 1);
    return vpn == (tlb_vppn >> compare_shift);
}

static bool invtlb_pred_page_asid(const LoongArchTLB *tlb, void *opaque)
{
    InvtlbMatch *m = opaque;
    int index = tlb - tlb_bank_base(m->env, m->guest);

    return !invtlb_entry_g(tlb)
        && invtlb_entry_asid(tlb) == m->asid
        && invtlb_entry_matches_va(m->env, tlb, index, m->guest, m->addr);
}

static bool invtlb_pred_page_asid_or_g(const LoongArchTLB *tlb, void *opaque)
{
    InvtlbMatch *m = opaque;
    int index = tlb - tlb_bank_base(m->env, m->guest);

    return (invtlb_entry_g(tlb) || invtlb_entry_asid(tlb) == m->asid)
        && invtlb_entry_matches_va(m->env, tlb, index, m->guest, m->addr);
}

static bool invtlb_pred_page(const LoongArchTLB *tlb, void *opaque)
{
    InvtlbMatch *m = opaque;
    int index = tlb - tlb_bank_base(m->env, m->guest);

    return invtlb_entry_matches_va(m->env, tlb, index, m->guest, m->addr);
}

static bool invtlb_pred_asid(const LoongArchTLB *tlb, void *opaque)
{
    InvtlbMatch *m = opaque;

    return !invtlb_entry_g(tlb) && invtlb_entry_asid(tlb) == m->asid;
}

static bool invtlb_pred_global(const LoongArchTLB *tlb, void *opaque)
{
    InvtlbMatch *m = opaque;

    return invtlb_entry_g(tlb) == m->g;
}

void helper_invtlb(CPULoongArchState *env, target_ulong info,
                   target_ulong addr, uint32_t op)
{
    InvtlbMatch m = { .env = env, .addr = addr };
    invtlb_pred pred = NULL;
    bool guest, gid_all;
    uint8_t gid = 0;

    if (invtlb_op_is_lvz(op) && env->guest) {
        do_raise_exception(env, EXCCODE_IPE, GETPC());
    }

    /* Bank currently in use, for the base forms. */
    guest = env->guest;

    switch (op) {
    case 0x0: /* every entry of the current bank, any GID */
        gid_all = true;
        break;
    case 0x1:
        /*
         * "Invalidate current": the same bank, restricted to the GID that
         * owns it.  The no-LVZ manual calls 0x0 and 0x1 identical because
         * there is only one GID then; with LVZ they must differ, or a guest
         * could evict another guest's mappings -- Linux names 0x1
         * INVTLB_CURRENT_ALL and KVM uses it to flush only its own VM.
         */
        gid = get_bank_gid(env, guest);
        gid_all = false;
        break;
    case 0x2:
    case 0x3: /* G = 1 / G = 0 */
        gid = get_bank_gid(env, guest);
        m.g = (op == 0x2);
        pred = invtlb_pred_global;
        gid_all = false;
        break;
    case 0x4: /* G = 0 and ASID match */
        gid = get_bank_gid(env, guest);
        m.asid = invtlb_op_asid(info);
        pred = invtlb_pred_asid;
        gid_all = false;
        break;
    case 0x5: /* G = 0, ASID and VA match */
        gid = get_bank_gid(env, guest);
        m.guest = guest;
        m.asid = invtlb_op_asid(info);
        pred = invtlb_pred_page_asid;
        gid_all = false;
        break;
    case 0x6: /* G = 1 or ASID match, and VA match */
        gid = get_bank_gid(env, guest);
        m.guest = guest;
        m.asid = invtlb_op_asid(info);
        pred = invtlb_pred_page_asid_or_g;
        gid_all = false;
        break;
    case 0x9: /* guest bank, GID from rj */
    case 0xa: /* guest bank, GID from rj, G = 1 */
    case 0xb: /* guest bank, GID from rj, G = 0 */
    case 0xc: /* guest bank, GID from rj, G = 0 and ASID match */
    case 0xd: /* guest, G = 0, ASID and VA match */
    case 0xe: /* guest, G = 1 or ASID match, and VA match */
        guest = true;
        gid = invtlb_op_gid(info);
        gid_all = false;
        if (op == 0xa || op == 0xb) {
            m.g = (op == 0xa);
            pred = invtlb_pred_global;
        } else if (op == 0xc) {
            m.asid = invtlb_op_asid(info);
            pred = invtlb_pred_asid;
        } else if (op == 0xd || op == 0xe) {
            m.guest = guest;
            m.asid = invtlb_op_asid(info);
            pred = (op == 0xd) ? invtlb_pred_page_asid
                               : invtlb_pred_page_asid_or_g;
        }
        break;
    case 0x10: /* guest bank, every GID: GVA -> GPA */
    case 0x13: /* guest bank, GID from rj: GVA -> GPA */
        guest = true;
        gid = invtlb_op_gid(info);
        gid_all = (op == 0x10);
        break;
    case 0x11: /* host bank, every GID: GPA -> HPA */
    case 0x14: /* host bank, GID from rj: GPA -> HPA */
        guest = false;
        gid = invtlb_op_gid(info);
        gid_all = (op == 0x11);
        break;
    case 0x12: /* both banks, every GID */
    case 0x15: /* both banks, GID from rj */
        gid = invtlb_op_gid(info);
        gid_all = (op == 0x12);
        invtlb_sweep(env, false, gid_all, gid, NULL, NULL);
        invtlb_sweep(env, true, gid_all, gid, NULL, NULL);
        tlb_flush(env_cpu(env));
        return;
    case 0x16: /* host bank, GID from rj, address match */
        guest = false;
        gid = invtlb_op_gid(info);
        m.guest = guest;
        m.asid = invtlb_op_asid(info);
        pred = invtlb_pred_page;
        gid_all = false;
        break;
    default:
        /* Values absent from the manual raise RI; trans_invtlb rejects the
         * ones illegal without LVZ before we get here. */
        do_raise_exception(env, EXCCODE_INE, GETPC());
        return;
    }

    invtlb_sweep(env, guest, gid_all, gid, pred, pred ? &m : NULL);
    tlb_flush(env_cpu(env));
}

bool loongarch_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                            MMUAccessType access_type, int mmu_idx,
                            bool probe, uintptr_t retaddr)
{
    CPULoongArchState *env = cpu_env(cs);
    vaddr gpa;
    hwaddr physical;
    int prot, host_prot;
    int ret;

    ret = get_physical_address(env, &physical, &prot, address,
                               access_type, mmu_idx);

    if (ret == TLBRET_MATCH) {
        if (env->guest) {
            gpa = physical;
            ret = loongarch_map_host_address(env, &physical, &host_prot,
                                           gpa, access_type);
            if (ret != TLBRET_HOST_MATCH) {
                if (probe) {
                    return false;
                }

                raise_mmu_exception(env, gpa, access_type, ret);
                cpu_loop_exit_restore(cs, retaddr);
                return false;
            }
            prot &= host_prot;
        }

        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " HWADDR_FMT_plx
                      " prot %d guest %d\n", __func__, address, physical, prot, env->guest);
        return true;
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " guest %d ret %d\n", __func__, address,
                      env->guest, ret);
    }
    if (probe) {
        return false;
    }
    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}

static inline hwaddr loongarch_get_host_address(CPULoongArchState *env, target_ulong gpa)
{
    hwaddr physical = 0;
    int prot, ret;

    ret = loongarch_map_host_address(env, &physical, &prot, gpa, MMU_DATA_LOAD);

    if (ret != TLBRET_HOST_MATCH) {
        raise_mmu_exception(env, gpa, MMU_DATA_LOAD, ret);
        cpu_loop_exit_restore(env_cpu(env), GETPC());
    }

    return physical;
}

target_ulong helper_lddir(CPULoongArchState *env, target_ulong base,
                          target_ulong level, uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    target_ulong badvaddr, index, phys, ret;
    int shift;
    uint64_t dir_base, dir_width;

    if (unlikely((level == 0) || (level > 4))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Attepted LDDIR with level %"PRId64"\n", level);
        return base;
    }

    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        if (unlikely(level == 4)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "Attempted use of level 4 huge page\n");
        }

        if (FIELD_EX64(base, TLBENTRY, LEVEL)) {
            return base;
        } else {
            return FIELD_DP64(base, TLBENTRY, LEVEL, level);
        }
    }

    badvaddr = GET_CSR_IF(env->guest, TLBRBADV);
    /* Extract only the PPN field (bits 12-47) from the directory entry.
     * Flag bits (V, D, PLV, MAT, etc.) at positions 0-11 must not
     * participate in the next-level address calculation. */
    base = (target_ulong)FIELD_EX64(base, TLBENTRY_64, PPN) << TARGET_PAGE_BITS;

    /* 0:64bit, 1:128bit, 2:192bit, 3:256bit */
    shift = FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTEWIDTH);
    shift = (shift + 1) * 3;

    get_dir_base_width(env, &dir_base, &dir_width, level);
    index = (badvaddr >> dir_base) & ((1 << dir_width) - 1);
    phys = base | index << shift;
    if (env->guest) {
        ret = ldq_phys(cs->as, loongarch_get_host_address(env, phys)) & TARGET_PHYS_MASK;
    } else {
        ret = ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
    }
    return ret;
}

void helper_ldpte(CPULoongArchState *env, target_ulong base, target_ulong odd,
                  uint32_t mem_idx)
{
    CPUState *cs = env_cpu(env);
    target_ulong phys, tmp0, ptindex, ptoffset0, ptoffset1, ps, badv;
    int shift;
    uint64_t ptbase = FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTBASE);
    uint64_t ptwidth = FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTWIDTH);
    uint64_t dir_base, dir_width;

    /*
     * The parameter "base" has only two types,
     * one is the page table base address,
     * whose bit 6 should be 0,
     * and the other is the huge page entry,
     * whose bit 6 should be 1.
     */
    base = base & TARGET_PHYS_MASK;
    if (FIELD_EX64(base, TLBENTRY, HUGE)) {
        /*
         * Gets the huge page level and Gets huge page size.
         * Clears the huge page level information in the entry.
         * Clears huge page bit.
         * Move HGLOBAL bit to GLOBAL bit.
         */
        get_dir_base_width(env, &dir_base, &dir_width,
                           FIELD_EX64(base, TLBENTRY, LEVEL));

        base = FIELD_DP64(base, TLBENTRY, LEVEL, 0);
        base = FIELD_DP64(base, TLBENTRY, HUGE, 0);
        if (FIELD_EX64(base, TLBENTRY, HGLOBAL)) {
            base = FIELD_DP64(base, TLBENTRY, HGLOBAL, 0);
            base = FIELD_DP64(base, TLBENTRY, G, 1);
        }

        ps = dir_base + dir_width - 1;
        /*
         * Huge pages are evenly split into parity pages
         * when loaded into the tlb,
         * so the tlb page size needs to be divided by 2.
         */
        tmp0 = base;
        if (odd) {
            tmp0 += MAKE_64BIT_MASK(ps, 1);
        }
    } else {
        /* 0:64bit, 1:128bit, 2:192bit, 3:256bit */
        shift = FIELD_EX64(GET_CSR_IF(env->guest, PWCL), CSR_PWCL, PTEWIDTH);
        shift = (shift + 1) * 3;
        badv = GET_CSR_IF(env->guest, TLBRBADV);

        ptindex = (badv >> ptbase) & ((1 << ptwidth) - 1);
        ptindex = ptindex & ~0x1;   /* clear bit 0 */
        ptoffset0 = ptindex << shift;
        ptoffset1 = (ptindex + 1) << shift;

        /* Strip flag bits (0-11) from base before address calculation. */
        base = (target_ulong)FIELD_EX64(base, TLBENTRY_64, PPN) << TARGET_PAGE_BITS;
        phys = base | (odd ? ptoffset1 : ptoffset0);
        if (env->guest) {
            tmp0 = ldq_phys(cs->as, loongarch_get_host_address(env, phys)) & TARGET_PHYS_MASK;
        } else {
            tmp0 = ldq_phys(cs->as, phys) & TARGET_PHYS_MASK;
        }
        ps = ptbase;
    }

    if (odd) {
        SET_CSR_IF(env->guest, TLBRELO1, tmp0);
    } else {
        SET_CSR_IF(env->guest, TLBRELO0, tmp0);
    }
    SET_CSR_IF(env->guest, TLBREHI, FIELD_DP64(GET_CSR_IF(env->guest, TLBREHI), CSR_TLBREHI, PS, ps));
}
