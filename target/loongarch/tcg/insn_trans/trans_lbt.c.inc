/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LoongArch binary translation (LBT) TCG translation. */

#define LBT_OP_ADD 0
#define LBT_OP_ADC 1
#define LBT_OP_SUB 2
#define LBT_OP_SBC 3
#define LBT_OP_AND 4
#define LBT_OP_OR  5
#define LBT_OP_XOR 6
#define LBT_OP_INC 7
#define LBT_OP_DEC 8
#define LBT_OP_SHL 9
#define LBT_OP_SHR 10
#define LBT_OP_SAR 11
#define LBT_OP_ROL 12
#define LBT_OP_ROR 13
#define LBT_OP_RCL 14
#define LBT_OP_RCR 15

static void gen_lbt_alu(TCGv a, TCGv b, int op, int size)
{
    gen_helper_lbt_x86_alu(tcg_env, a, b, tcg_constant_i32(op),
                           tcg_constant_i32(size));
}

#define LBT_RR(NAME, OP, SIZE)                                        \
static bool trans_##NAME(DisasContext *ctx, arg_rr_jk *a)             \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                gpr_src(ctx, a->rk, EXT_NONE), (OP), (SIZE));         \
    return true;                                                      \
}

#define LBT_IMM3(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui3 *a)         \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM4(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui4 *a)         \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM5(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui5 *a)         \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM6(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui6 *a)         \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

/* x86 integer / logical / shift / rotate (flag-only instructions). */
LBT_RR(lbt_x86add_wu, LBT_OP_ADD, 4)
LBT_RR(lbt_x86add_du, LBT_OP_ADD, 8)
LBT_RR(lbt_x86sub_wu, LBT_OP_SUB, 4)
LBT_RR(lbt_x86sub_du, LBT_OP_SUB, 8)
LBT_RR(lbt_x86add_b, LBT_OP_ADD, 1)
LBT_RR(lbt_x86add_h, LBT_OP_ADD, 2)
LBT_RR(lbt_x86add_w, LBT_OP_ADD, 4)
LBT_RR(lbt_x86add_d, LBT_OP_ADD, 8)
LBT_RR(lbt_x86sub_b, LBT_OP_SUB, 1)
LBT_RR(lbt_x86sub_h, LBT_OP_SUB, 2)
LBT_RR(lbt_x86sub_w, LBT_OP_SUB, 4)
LBT_RR(lbt_x86sub_d, LBT_OP_SUB, 8)
LBT_RR(lbt_x86adc_b, LBT_OP_ADC, 1)
LBT_RR(lbt_x86adc_h, LBT_OP_ADC, 2)
LBT_RR(lbt_x86adc_w, LBT_OP_ADC, 4)
LBT_RR(lbt_x86adc_d, LBT_OP_ADC, 8)
LBT_RR(lbt_x86sbc_b, LBT_OP_SBC, 1)
LBT_RR(lbt_x86sbc_h, LBT_OP_SBC, 2)
LBT_RR(lbt_x86sbc_w, LBT_OP_SBC, 4)
LBT_RR(lbt_x86sbc_d, LBT_OP_SBC, 8)
LBT_RR(lbt_x86and_b, LBT_OP_AND, 1)
LBT_RR(lbt_x86and_h, LBT_OP_AND, 2)
LBT_RR(lbt_x86and_w, LBT_OP_AND, 4)
LBT_RR(lbt_x86and_d, LBT_OP_AND, 8)
LBT_RR(lbt_x86or_b, LBT_OP_OR, 1)
LBT_RR(lbt_x86or_h, LBT_OP_OR, 2)
LBT_RR(lbt_x86or_w, LBT_OP_OR, 4)
LBT_RR(lbt_x86or_d, LBT_OP_OR, 8)
LBT_RR(lbt_x86xor_b, LBT_OP_XOR, 1)
LBT_RR(lbt_x86xor_h, LBT_OP_XOR, 2)
LBT_RR(lbt_x86xor_w, LBT_OP_XOR, 4)
LBT_RR(lbt_x86xor_d, LBT_OP_XOR, 8)
LBT_RR(lbt_x86sll_b, LBT_OP_SHL, 1)
LBT_RR(lbt_x86sll_h, LBT_OP_SHL, 2)
LBT_RR(lbt_x86sll_w, LBT_OP_SHL, 4)
LBT_RR(lbt_x86sll_d, LBT_OP_SHL, 8)
LBT_RR(lbt_x86srl_b, LBT_OP_SHR, 1)
LBT_RR(lbt_x86srl_h, LBT_OP_SHR, 2)
LBT_RR(lbt_x86srl_w, LBT_OP_SHR, 4)
LBT_RR(lbt_x86srl_d, LBT_OP_SHR, 8)
LBT_RR(lbt_x86sra_b, LBT_OP_SAR, 1)
LBT_RR(lbt_x86sra_h, LBT_OP_SAR, 2)
LBT_RR(lbt_x86sra_w, LBT_OP_SAR, 4)
LBT_RR(lbt_x86sra_d, LBT_OP_SAR, 8)
LBT_RR(lbt_x86rotl_b, LBT_OP_ROL, 1)
LBT_RR(lbt_x86rotl_h, LBT_OP_ROL, 2)
LBT_RR(lbt_x86rotl_w, LBT_OP_ROL, 4)
LBT_RR(lbt_x86rotl_d, LBT_OP_ROL, 8)
LBT_RR(lbt_x86rotr_b, LBT_OP_ROR, 1)
LBT_RR(lbt_x86rotr_h, LBT_OP_ROR, 2)
LBT_RR(lbt_x86rotr_w, LBT_OP_ROR, 4)
LBT_RR(lbt_x86rotr_d, LBT_OP_ROR, 8)
LBT_RR(lbt_x86rcl_b, LBT_OP_RCL, 1)
LBT_RR(lbt_x86rcl_h, LBT_OP_RCL, 2)
LBT_RR(lbt_x86rcl_w, LBT_OP_RCL, 4)
LBT_RR(lbt_x86rcl_d, LBT_OP_RCL, 8)
LBT_RR(lbt_x86rcr_b, LBT_OP_RCR, 1)
LBT_RR(lbt_x86rcr_h, LBT_OP_RCR, 2)
LBT_RR(lbt_x86rcr_w, LBT_OP_RCR, 4)
LBT_RR(lbt_x86rcr_d, LBT_OP_RCR, 8)

/* immediate-count shift/rotate forms */
LBT_IMM3(lbt_x86slli_b, LBT_OP_SHL, 1)
LBT_IMM3(lbt_x86srli_b, LBT_OP_SHR, 1)
LBT_IMM3(lbt_x86srai_b, LBT_OP_SAR, 1)
LBT_IMM3(lbt_x86rotli_b, LBT_OP_ROL, 1)
LBT_IMM3(lbt_x86rotri_b, LBT_OP_ROR, 1)
LBT_IMM3(lbt_x86rcli_b, LBT_OP_RCL, 1)
LBT_IMM3(lbt_x86rcri_b, LBT_OP_RCR, 1)
LBT_IMM4(lbt_x86slli_h, LBT_OP_SHL, 2)
LBT_IMM4(lbt_x86srli_h, LBT_OP_SHR, 2)
LBT_IMM4(lbt_x86srai_h, LBT_OP_SAR, 2)
LBT_IMM4(lbt_x86rotli_h, LBT_OP_ROL, 2)
LBT_IMM4(lbt_x86rotri_h, LBT_OP_ROR, 2)
LBT_IMM4(lbt_x86rcli_h, LBT_OP_RCL, 2)
LBT_IMM4(lbt_x86rcri_h, LBT_OP_RCR, 2)
LBT_IMM5(lbt_x86slli_w, LBT_OP_SHL, 4)
LBT_IMM5(lbt_x86srli_w, LBT_OP_SHR, 4)
LBT_IMM5(lbt_x86srai_w, LBT_OP_SAR, 4)
LBT_IMM5(lbt_x86rotli_w, LBT_OP_ROL, 4)
LBT_IMM5(lbt_x86rotri_w, LBT_OP_ROR, 4)
LBT_IMM5(lbt_x86rcli_w, LBT_OP_RCL, 4)
LBT_IMM5(lbt_x86rcri_w, LBT_OP_RCR, 4)
LBT_IMM6(lbt_x86slli_d, LBT_OP_SHL, 8)
LBT_IMM6(lbt_x86srli_d, LBT_OP_SHR, 8)
LBT_IMM6(lbt_x86srai_d, LBT_OP_SAR, 8)
LBT_IMM6(lbt_x86rotli_d, LBT_OP_ROL, 8)
LBT_IMM6(lbt_x86rotri_d, LBT_OP_ROR, 8)
LBT_IMM6(lbt_x86rcli_d, LBT_OP_RCL, 8)
LBT_IMM6(lbt_x86rcri_d, LBT_OP_RCR, 8)

/* x86inc / x86dec: flag-only, CF preserved */
#define LBT_INCDEC(NAME, OP, SIZE)                                    \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j *a)             \
{                                                                     \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(1), (OP), (SIZE));                    \
    return true;                                                      \
}
LBT_INCDEC(lbt_x86inc_b, LBT_OP_INC, 1)
LBT_INCDEC(lbt_x86inc_h, LBT_OP_INC, 2)
LBT_INCDEC(lbt_x86inc_w, LBT_OP_INC, 4)
LBT_INCDEC(lbt_x86inc_d, LBT_OP_INC, 8)
LBT_INCDEC(lbt_x86dec_b, LBT_OP_DEC, 1)
LBT_INCDEC(lbt_x86dec_h, LBT_OP_DEC, 2)
LBT_INCDEC(lbt_x86dec_w, LBT_OP_DEC, 4)
LBT_INCDEC(lbt_x86dec_d, LBT_OP_DEC, 8)

static bool trans_lbt_movgr2scr(DisasContext *ctx, arg_lbt_gr2scr *a)
{
    gen_helper_lbt_movgr2scr(tcg_env, tcg_constant_i32(a->scr),
                             gpr_src(ctx, a->rj, EXT_NONE));
    return true;
}

static bool trans_lbt_movscr2gr(DisasContext *ctx, arg_lbt_scr2gr *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_movscr2gr(dest, tcg_env, tcg_constant_i32(a->scr));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86mfflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_x86_mfflag(dest, tcg_env, tcg_constant_i32(a->imm));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86mtflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    gen_helper_lbt_x86_mtflag(tcg_env, gpr_src(ctx, a->rd, EXT_NONE),
                              tcg_constant_i32(a->imm));
    return true;
}

static bool trans_lbt_x86setj(DisasContext *ctx, arg_lbt_rd_cond *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_x86_setcc(dest, tcg_env, tcg_constant_i32(a->cond));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86inctop(DisasContext *ctx, arg_empty *a)
{
    gen_helper_lbt_x86_inctop(tcg_env);
    return true;
}

static bool trans_lbt_x86dectop(DisasContext *ctx, arg_empty *a)
{
    gen_helper_lbt_x86_dectop(tcg_env);
    return true;
}

static bool trans_lbt_x86settm(DisasContext *ctx, arg_empty *a)
{
    gen_helper_lbt_x86_settm(tcg_env);
    return true;
}

static bool trans_lbt_x86clrtm(DisasContext *ctx, arg_empty *a)
{
    gen_helper_lbt_x86_clrtm(tcg_env);
    return true;
}
