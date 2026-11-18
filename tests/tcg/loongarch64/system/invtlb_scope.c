/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INVTLB bank and GID scoping.
 *
 * The host and guest banks are separate arrays; which one an INVTLB form
 * clears is chosen by the opcode, never by the GID an entry carries.  The
 * expected semantics below follow the op names in
 * arch/loongarch/include/asm/tlb.h of Linux:
 *
 *   0x0/0x1  every entry of the current bank
 *   0x2/0x3  G=1 / G=0 of the current bank
 *   0x4      G=0 and ASID of the current bank
 *   0x5/0x6  ... plus a VA match
 *   0x9-0xe  guest bank, filtered by GID = rj[23:16]
 *   0x10/0x13  guest bank, any GID / matching GID (GVA -> GPA)
 *   0x11/0x14  host bank, any GID / matching GID (GPA -> HPA)
 *   0x12/0x15  both banks, any GID / matching GID
 *   0x16       host bank, matching GID and VA
 *
 * GTLBC/GSTAT are only reachable from the host view, so 0x9-0x16 raise IPE
 * when executed in guest mode; the test runs entirely in host mode and only
 * moves data through the guest bank with gtlbwr/gtlbsrch.
 */

#include "minilib.h"

extern void _exit(int status);

typedef unsigned long u64;
typedef unsigned int u32;

#define PS_STLB 14              /* 16 KiB STLB lines */

#define CSR_TLBIDX   0x10
#define CSR_TLBEHI   0x11
#define CSR_TLBELO0  0x12
#define CSR_TLBELO1  0x13
#define CSR_GTLBC    0x15
#define CSR_STLBPS   0x1e
#define CSR_GSTAT    0x50

#define GSTAT_GID_SHIFT   16
#define GTLBC_USETGID     (1UL << 12)
#define GTLBC_TGID_SHIFT  16

#define TLB_ELO_PPN(vpn)  (((u64)(vpn)) << 12)
#define TLB_ELO_V         (1UL << 0)
#define TLB_ELO_D         (1UL << 1)
#define TLB_ELO_PLV3      (3UL << 2)
#define TLB_ELO_G         (1UL << 6)
#define TLB_ELO_MAT       (1UL << 4)

/*
 * An STLB line is selected by VA[22:15], so every probe below needs its own
 * 32 KiB slot: probes sharing an index overwrite each other and turn the
 * assertions into no-ops.
 */
#define GA 0x30000000UL         /* guest bank probes */
#define GB 0x30008000UL
#define GC 0x30010000UL
#define GX 0x30800000UL         /* same STLB column as GA, other VPPN */
#define PA 0x30018000UL         /* host bank probes */
#define PB 0x30020000UL
#define GPPN 0x12345UL
#define HPPN 0x76540UL

static int checks;
static int fails;

static void check(int cond, const char *what)
{
    checks++;
    if (!cond) {
        fails++;
        ml_printf("FAIL %s\n", what);
    }
}

/* csrwr writes the previous value back into rd, so stage it in $r12. */
#define CSR_WR(n, v) \
    __asm__ volatile("move $r12,%0\n csrwr $r12," #n :: "r"(v) : "r12", "memory")
#define CSR_RD(n, v) \
    __asm__ volatile("csrrd %0," #n : "=r"(v))

static void set_gid(unsigned int gid)
{
    u64 gstat;

    CSR_RD(0x50, gstat);
    gstat &= ~(0xffUL << GSTAT_GID_SHIFT);
    gstat |= (u64)(gid & 0xff) << GSTAT_GID_SHIFT;
    CSR_WR(0x50, gstat);
}

static void set_tgid(unsigned int gid, int use)
{
    CSR_WR(0x15, use ? (GTLBC_USETGID |
                        ((u64)(gid & 0xff) << GTLBC_TGID_SHIFT)) : 0);
}

/*
 * GCSR access.  The gcsr mnemonics are not accepted by every assembler, so
 * the encodings are emitted as words; each has its own helper because the
 * CSR number is part of the instruction word, not an operand.
 */
#define GCSR_WR(name, enc)                                                  \
    static inline void gcsrwr_##name(u64 v)                            \
    {                                                                       \
        __asm__ volatile("move $r12,%0\n .word " #enc :: "r"(v)             \
                         : "r12", "memory");                                \
    }
#define GCSR_RD(name, enc)                                                  \
    static inline u64 gcsrrd_##name(void)                              \
    {                                                                       \
        u64 v;                                                         \
        __asm__ volatile(".word " #enc "\n move %0,$r12" : "=r"(v) :: "r12");\
        return v;                                                           \
    }

GCSR_WR(idx,   0x0500402c)
GCSR_WR(ehi,   0x0500442c)
GCSR_WR(elo0,  0x0500482c)
GCSR_WR(elo1,  0x05004c2c)
GCSR_WR(stlbps, 0x0500782c)
GCSR_RD(idx,   0x0500400c)
GCSR_RD(ehi,   0x0500440c)
GCSR_RD(stlbps, 0x0500780c)

static void gtlbwr_insn(void)
{
    __asm__ volatile(".word 0x06483001" ::: "memory");
}

static void gtlbsrch_insn(void)
{
    __asm__ volatile(".word 0x06482801" ::: "memory");
}

static inline u32 stlb_index(u64 va)
{
    return (va >> (PS_STLB + 1)) & 0xff;
}

/* Fill a guest TLB line through the guest view (host mode). */
static void gfill(u64 va, u64 elo)
{
    gcsrwr_idx(stlb_index(va) | ((u64)PS_STLB << 24));
    gcsrwr_ehi(va);
    gcsrwr_elo0(elo);
    gcsrwr_elo1(elo);
    gtlbwr_insn();
}

/*
 * Same, but into an explicit STLB way: gtlbwr honours TLBIDX.index, so this
 * is how a single column can be populated in more than one way and the
 * "all ways of that column" behaviour of tlbclr/tlbflush becomes visible.
 */
static void gfill_way(u64 va, unsigned int way, u64 elo)
{
    gcsrwr_idx((way * 256 + stlb_index(va)) | ((u64)PS_STLB << 24));
    gcsrwr_ehi(va);
    gcsrwr_elo0(elo);
    gcsrwr_elo1(elo);
    gtlbwr_insn();
}

static int gsearch(u64 va)
{
    gcsrwr_ehi(va);
    gtlbsrch_insn();
    return ((gcsrrd_idx() >> 31) & 1) == 0;
}

/* Fill a host TLB line through the host view. */
static void hfill(u64 va, u64 elo)
{
    CSR_WR(0x10, stlb_index(va) | ((u64)PS_STLB << 24));
    CSR_WR(0x11, va);
    CSR_WR(0x12, elo);
    CSR_WR(0x13, elo);
    __asm__ volatile("tlbwr" ::: "memory");
}

static int hsearch(u64 va)
{
    u64 idx;

    CSR_WR(0x11, va);
    __asm__ volatile("tlbsrch" ::: "memory");
    CSR_RD(0x10, idx);
    return ((idx >> 31) & 1) == 0;
}

/*
 * Guest-bank maintenance, issued from host mode.  gtlbclr/gtlbflush/gtlbrd
 * are only reachable through GCSR.TLBIDX/GCSR.ASID, and must act on the
 * guest bank even though the host GID is what get_tgid() would report.
 */
static void gtlbclr_insn(void)
{
    __asm__ volatile(".word 0x06482001" ::: "memory");
}

static void gtlbflush_insn(void)
{
    __asm__ volatile(".word 0x06482401" ::: "memory");
}

static void gtlbrd_insn(void)
{
    __asm__ volatile(".word 0x06482c01" ::: "memory");
}

/* Point GCSR.TLBIDX at an STLB line without disturbing GCSR.STLBPS. */
static void gset_idx(u64 va)
{
    gcsrwr_idx(stlb_index(va) | ((u64)PS_STLB << 24));
}

/* tlbrd on the guest bank returns the entry's VPPN in GCSR.TLBEHI. */
static int gread_hits(u64 va)
{
    gset_idx(va);
    gtlbrd_insn();
    return ((gcsrrd_idx() >> 31) & 1) == 0;
}

static void invtlb(unsigned int op, u64 info, u64 addr)
{
    switch (op) {
#define CASE_OP(n)                                                          \
    case n: __asm__ volatile("invtlb " #n ",%0,%1" :: "r"(info), "r"(addr)  \
                             : "memory"); break;
    CASE_OP(0x0) CASE_OP(0x1) CASE_OP(0x2) CASE_OP(0x3) CASE_OP(0x4)
    CASE_OP(0x5) CASE_OP(0x6)
    CASE_OP(0x9) CASE_OP(0xa) CASE_OP(0xb) CASE_OP(0xc) CASE_OP(0xd)
    CASE_OP(0xe)
    CASE_OP(0x10) CASE_OP(0x11) CASE_OP(0x12) CASE_OP(0x13) CASE_OP(0x14)
    CASE_OP(0x15) CASE_OP(0x16)
#undef CASE_OP
    default:
        __builtin_unreachable();
    }
}

#define GINFO(gid)      ((u64)(gid) << 16)
#define ELO(ppn, flags) (TLB_ELO_PPN(ppn) | (flags))

#define HOST_ELO  ELO(HPPN, TLB_ELO_V | TLB_ELO_D | TLB_ELO_PLV3 | TLB_ELO_MAT)
#define GUEST_ELO ELO(GPPN, TLB_ELO_V | TLB_ELO_D | TLB_ELO_PLV3 | TLB_ELO_MAT)

void main(void)
{
    ml_printf("invtlb: start\n");

    CSR_WR(0x1e, PS_STLB);
    gcsrwr_stlbps(PS_STLB);
    set_gid(0);
    set_tgid(0, 0);

    /* Bank separation: a guest fill is invisible to the host bank. */
    set_gid(1);
    gfill(GA, GUEST_ELO);
    check(gsearch(GA), "gtlbwr fill is searchable through the guest bank");
    check(!hsearch(GA), "guest fill must not appear in the host bank");
    hfill(PA, HOST_ELO);
    check(hsearch(PA), "tlbwr fill is searchable through the host bank");
    check(!gsearch(PA), "host fill must not appear in the guest bank");

    /* 0x0/0x1 clear the current bank only (host mode). */
    invtlb(0x0, 0, 0);
    check(!hsearch(PA), "0x0 cleared the host bank");
    check(gsearch(GA), "0x0 must not clear the guest bank");
    hfill(PA, HOST_ELO);
    invtlb(0x1, 0, 0);
    check(!hsearch(PA), "0x1 cleared the host bank");
    check(gsearch(GA), "0x1 must not clear the guest bank");

    /*
     * 0x0 ignores GID, 0x1 only clears the GID that owns the bank.  The
     * no-LVZ manual makes them identical because there is a single GID
     * then; with LVZ a VM-local "current" flush must not evict another
     * domain's mappings (Linux: INVTLB_ALL vs INVTLB_CURRENT_ALL).
     */
    set_gid(0);
    hfill(PA, HOST_ELO);
    set_tgid(1, 1);
    hfill(PB, HOST_ELO);
    set_tgid(0, 0);
    invtlb(0x0, 0, 0);
    check(!hsearch(PA), "0x0 cleared the GID-0 host entry");
    set_tgid(1, 1);
    check(!hsearch(PB), "0x0 cleared a GID-1 host entry");
    set_tgid(0, 0);

    hfill(PA, HOST_ELO);
    set_tgid(1, 1);
    hfill(PB, HOST_ELO);
    set_tgid(0, 0);
    invtlb(0x1, 0, 0);
    check(!hsearch(PA), "0x1 cleared the current GID entry");
    set_tgid(1, 1);
    check(hsearch(PB), "0x1 kept another GID's entry");
    set_tgid(0, 0);

    /* Restore the guest probe consumed above. */
    set_gid(1);
    gfill(GA, GUEST_ELO);

    /* 0x2/0x3 split on G. */
    hfill(PA, ELO(HPPN, TLB_ELO_V | TLB_ELO_D | TLB_ELO_PLV3 | TLB_ELO_G));
    hfill(PB, HOST_ELO);
    invtlb(0x2, 0, 0);
    check(!hsearch(PA), "0x2 cleared the G=1 entry");
    check(hsearch(PB), "0x2 kept the G=0 entry");
    invtlb(0x3, 0, 0);
    check(!hsearch(PB), "0x3 cleared the G=0 entry");
    check(gsearch(GA), "0x3 must not clear the guest bank");

    /* 0x4 filters on ASID; the fills above used the current ASID (0). */
    hfill(PA, HOST_ELO);
    invtlb(0x4, 0x1, 0);
    check(hsearch(PA), "0x4 kept an entry of another ASID");
    invtlb(0x4, 0x0, 0);
    check(!hsearch(PA), "0x4 cleared the matching ASID");

    /* 0x5/0x6 match on VA. */
    hfill(PA, HOST_ELO);
    hfill(PB, HOST_ELO);
    invtlb(0x5, 0x0, PA);
    check(!hsearch(PA), "0x5 cleared the matching VA");
    check(hsearch(PB), "0x5 kept another VA");
    invtlb(0x6, 0x0, PB);
    check(!hsearch(PB), "0x6 cleared the matching VA");

    /* 0x10/0x11/0x12 pick the bank by opcode, not by GID. */
    gfill(GA, GUEST_ELO);
    hfill(PA, HOST_ELO);
    invtlb(0x10, 0, 0);
    check(!gsearch(GA), "0x10 cleared the guest bank");
    check(hsearch(PA), "0x10 must not clear the host bank");
    invtlb(0x11, 0, 0);
    check(!hsearch(PA), "0x11 cleared the host bank");
    gfill(GA, GUEST_ELO);
    hfill(PA, HOST_ELO);
    invtlb(0x12, 0, 0);
    check(!gsearch(GA), "0x12 cleared the guest bank");
    check(!hsearch(PA), "0x12 cleared the host bank");

    /* 0x13/0x14/0x15 filter on rj[23:16]. */
    set_gid(1);
    gfill(GA, GUEST_ELO);
    set_gid(2);
    gfill(GB, GUEST_ELO);
    invtlb(0x13, GINFO(1), 0);
    check(!gsearch(GA), "0x13 cleared the matching GID");
    check(gsearch(GB), "0x13 kept another GID");
    invtlb(0x13, GINFO(2), 0);
    check(!gsearch(GB), "0x13 cleared the second GID");

    /* Host entries are tagged with GTLBC.TGID (0 when unset). */
    hfill(PA, HOST_ELO);
    invtlb(0x14, GINFO(1), 0);
    check(hsearch(PA), "0x14 kept an entry of another GID");
    invtlb(0x14, GINFO(0), 0);
    check(!hsearch(PA), "0x14 cleared the matching GID");

    /* A TGID-tagged host entry is filtered by its own GID. */
    set_tgid(1, 1);
    hfill(PB, HOST_ELO);
    invtlb(0x14, GINFO(2), 0);
    check(hsearch(PB), "0x14 kept a TGID-tagged entry of another GID");
    invtlb(0x14, GINFO(1), 0);
    check(!hsearch(PB), "0x14 cleared a TGID-tagged entry");
    set_tgid(0, 0);

    /*
     * 0x15 spans both banks but still filters on rj[23:16]: the guest entry
     * carries GSTAT.GID, the host entry GTLBC.TGID, so a GID-1 sweep must
     * reach the guest entry and leave the default (GID 0) host entry alone.
     */
    set_gid(1);
    gfill(GA, GUEST_ELO);
    set_gid(0);
    hfill(PA, HOST_ELO);
    invtlb(0x15, GINFO(1), 0);
    check(!gsearch(GA), "0x15 cleared the guest entry of that GID");
    check(hsearch(PA), "0x15 kept a host entry of another GID");
    set_tgid(1, 1);
    hfill(PB, HOST_ELO);
    invtlb(0x15, GINFO(1), 0);
    check(!hsearch(PB), "0x15 cleared a host entry of that GID");
    set_tgid(0, 0);

    /* 0x9-0xe work on the guest bank and honour rj[23:16]. */
    set_gid(1);
    gfill(GA, GUEST_ELO);
    set_gid(2);
    gfill(GB, GUEST_ELO);
    invtlb(0x9, GINFO(1), 0);
    check(!gsearch(GA), "0x9 cleared the matching GID");
    check(gsearch(GB), "0x9 kept another GID");
    invtlb(0x9, GINFO(2), 0);
    check(!gsearch(GB), "0x9 cleared the second GID");

    set_gid(1);
    gfill(GA, ELO(GPPN, TLB_ELO_V | TLB_ELO_D | TLB_ELO_PLV3 | TLB_ELO_G));
    gfill(GB, GUEST_ELO);
    invtlb(0xa, GINFO(1), 0);
    check(!gsearch(GA), "0xa cleared the G=1 entry");
    check(gsearch(GB), "0xa kept the G=0 entry");
    invtlb(0xb, GINFO(1), 0);
    check(!gsearch(GB), "0xb cleared the G=0 entry");

    gfill(GA, GUEST_ELO);
    invtlb(0xc, GINFO(1) | 0x1, 0);
    check(gsearch(GA), "0xc kept an entry of another ASID");
    invtlb(0xc, GINFO(1), 0);
    check(!gsearch(GA), "0xc cleared the matching ASID");

    gfill(GA, GUEST_ELO);
    gfill(GC, GUEST_ELO);
    invtlb(0xd, GINFO(1), GA);
    check(!gsearch(GA), "0xd cleared the matching VA");
    check(gsearch(GC), "0xd kept another VA");
    invtlb(0xe, GINFO(1), GC);
    check(!gsearch(GC), "0xe cleared the matching VA");

    /* 0x16 needs both the GID and the VA to match. */
    hfill(PA, HOST_ELO);
    hfill(PB, HOST_ELO);
    invtlb(0x16, GINFO(1), PA);
    check(hsearch(PA), "0x16 kept an entry of another GID");
    invtlb(0x16, GINFO(0), PA);
    check(!hsearch(PA), "0x16 cleared the matching address");
    check(hsearch(PB), "0x16 kept another address");

    /*
     * gtlb* maintenance acts on the guest bank from host mode.  HEAD ran
     * these through get_tgid(), which is 0 in host mode, so every guest
     * entry tagged with a non-zero GID was invisible to them.
     *
     * GT LB ops address one STLB *column*: index < 256 selects VA[22:15],
     * and the operation then sweeps all eight ways of that column.
     */
    set_gid(1);
    gfill_way(GA, 0, GUEST_ELO);
    gset_idx(GA);
    gtlbrd_insn();
    check(((gcsrrd_idx() >> 31) & 1) == 0, "gtlbrd reads a guest entry");
    check((gcsrrd_ehi() >> 13) == (GA >> 13),
          "gtlbrd returned the entry's VPPN");

    /* gtlbclr sweeps every way of its column. */
    gfill_way(GA, 0, GUEST_ELO);
    gfill_way(GX, 1, GUEST_ELO);
    gset_idx(GA);
    gtlbclr_insn();
    check(!gsearch(GA), "gtlbclr cleared way 0");
    check(!gsearch(GX), "gtlbclr cleared way 1 of the same column");

    /* gtlbflush does the same, ignoring G. */
    gfill_way(GA, 0, GUEST_ELO);
    gfill_way(GX, 1, GUEST_ELO);
    gset_idx(GA);
    gtlbflush_insn();
    check(!gsearch(GA), "gtlbflush cleared way 0");
    check(!gsearch(GX), "gtlbflush cleared way 1 of the same column");

    /* gtlbclr only drops G=0 entries; gtlbflush drops those too. */
    gfill_way(GA, 0, ELO(GPPN, TLB_ELO_V | TLB_ELO_D | TLB_ELO_PLV3 |
                         TLB_ELO_G));
    gfill_way(GX, 1, GUEST_ELO);
    gset_idx(GA);
    gtlbclr_insn();
    check(gsearch(GA), "gtlbclr kept the G=1 guest entry");
    check(!gsearch(GX), "gtlbclr cleared the G=0 guest entry");
    gset_idx(GA);
    gtlbflush_insn();
    check(!gsearch(GA), "gtlbflush cleared the G=1 guest entry");

    /* The host bank must stay untouched throughout. */
    set_gid(0);
    hfill(PA, HOST_ELO);
    hfill(PB, HOST_ELO);
    set_gid(1);
    gfill_way(GA, 0, GUEST_ELO);
    gset_idx(GA);
    gtlbflush_insn();
    gtlbclr_insn();
    check(hsearch(PA), "gtlbflush/gtlbclr must not clear the host bank");
    check(hsearch(PB), "gtlbflush/gtlbclr left the host bank intact");
    set_gid(0);

    ml_printf("invtlb: checks=%d fails=%d\n", checks, fails);
    ml_printf(fails ? "invtlb: FAIL\n" : "invtlb: PASS\n");
    _exit(fails ? 1 : 0);
}
