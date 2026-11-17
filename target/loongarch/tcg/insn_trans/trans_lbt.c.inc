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
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                gpr_src(ctx, a->rk, EXT_NONE), (OP), (SIZE));         \
    return true;                                                      \
}

#define LBT_IMM3(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui3 *a)         \
{                                                                     \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM4(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui4 *a)         \
{                                                                     \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM5(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui5 *a)         \
{                                                                     \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_lbt_alu(gpr_src(ctx, a->rj, EXT_NONE),                        \
                tcg_constant_tl(a->imm), (OP), (SIZE));               \
    return true;                                                      \
}

#define LBT_IMM6(NAME, OP, SIZE)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_j_ui6 *a)         \
{                                                                     \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
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
    if (!avail_LBT_X86(ctx)) { return false; }                        \
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
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_movgr2scr(tcg_env, tcg_constant_i32(a->scr),
                             gpr_src(ctx, a->rj, EXT_NONE));
    return true;
}

static bool trans_lbt_movscr2gr(DisasContext *ctx, arg_lbt_scr2gr *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_movscr2gr(dest, tcg_env, tcg_constant_i32(a->scr));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86mfflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_x86_mfflag(dest, tcg_env, tcg_constant_i32(a->imm));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86mtflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_mtflag(tcg_env, gpr_src(ctx, a->rd, EXT_NONE),
                              tcg_constant_i32(a->imm));
    return true;
}

static bool trans_lbt_x86setj(DisasContext *ctx, arg_lbt_rd_cond *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    gen_helper_lbt_x86_setcc(dest, tcg_env, tcg_constant_i32(a->cond));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86inctop(DisasContext *ctx, arg_empty *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_inctop(tcg_env);
    return true;
}

static bool trans_lbt_x86dectop(DisasContext *ctx, arg_empty *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_dectop(tcg_env);
    return true;
}

static bool trans_lbt_x86settm(DisasContext *ctx, arg_empty *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_settm(tcg_env);
    return true;
}

static bool trans_lbt_x86clrtm(DisasContext *ctx, arg_empty *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_clrtm(tcg_env);
    return true;
}

/* ---------------- ARM LBT ---------------- */
#define ARM_OP_ADD 0
#define ARM_OP_ADC 1
#define ARM_OP_SUB 2
#define ARM_OP_SBC 3
#define ARM_OP_AND 4
#define ARM_OP_OR  5
#define ARM_OP_XOR 6
#define ARM_OP_NOT 7
#define ARM_OP_SHL 8
#define ARM_OP_SHR 9
#define ARM_OP_SAR 10
#define ARM_OP_ROR 11
#define ARM_OP_RRX 12
#define ARM_OP_MOVW 13
#define ARM_OP_MOVD 14

#define LBT_ARM_RR(NAME, OP)                                          \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_arm_rr *a)        \
{                                                                     \
    if (!avail_LBT_ARM(ctx)) { return false; }                        \
    gen_helper_lbt_arm_alu(tcg_env, gpr_src(ctx, a->rj, EXT_NONE),    \
                           gpr_src(ctx, a->rk, EXT_NONE),             \
                           tcg_constant_i32(OP),                      \
                           tcg_constant_i32(a->cond));                \
    return true;                                                      \
}

#define LBT_ARM_IMM(NAME, OP)                                         \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_arm_imm *a)       \
{                                                                     \
    if (!avail_LBT_ARM(ctx)) { return false; }                        \
    gen_helper_lbt_arm_alu(tcg_env, gpr_src(ctx, a->rj, EXT_NONE),    \
                           tcg_constant_tl(a->imm),                   \
                           tcg_constant_i32(OP),                      \
                           tcg_constant_i32(a->cond));                \
    return true;                                                      \
}

#define LBT_ARM_J(NAME, OP)                                           \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_arm_j *a)         \
{                                                                     \
    if (!avail_LBT_ARM(ctx)) { return false; }                        \
    gen_helper_lbt_arm_alu(tcg_env, gpr_src(ctx, a->rj, EXT_NONE),    \
                           tcg_constant_tl(0),                        \
                           tcg_constant_i32(OP),                      \
                           tcg_constant_i32(a->cond));                \
    return true;                                                      \
}

LBT_ARM_RR(lbt_armadd_w, ARM_OP_ADD)
LBT_ARM_RR(lbt_armsub_w, ARM_OP_SUB)
LBT_ARM_RR(lbt_armadc_w, ARM_OP_ADC)
LBT_ARM_RR(lbt_armsbc_w, ARM_OP_SBC)
LBT_ARM_RR(lbt_armand_w, ARM_OP_AND)
LBT_ARM_RR(lbt_armor_w, ARM_OP_OR)
LBT_ARM_RR(lbt_armxor_w, ARM_OP_XOR)
LBT_ARM_RR(lbt_armsll_w, ARM_OP_SHL)
LBT_ARM_RR(lbt_armsrl_w, ARM_OP_SHR)
LBT_ARM_RR(lbt_armsra_w, ARM_OP_SAR)
LBT_ARM_RR(lbt_armrotr_w, ARM_OP_ROR)
LBT_ARM_IMM(lbt_armslli_w, ARM_OP_SHL)
LBT_ARM_IMM(lbt_armsrli_w, ARM_OP_SHR)
LBT_ARM_IMM(lbt_armsrai_w, ARM_OP_SAR)
LBT_ARM_IMM(lbt_armrotri_w, ARM_OP_ROR)
LBT_ARM_J(lbt_armnot_w, ARM_OP_NOT)
LBT_ARM_J(lbt_armrrx_w, ARM_OP_RRX)
LBT_ARM_J(lbt_armmov_w, ARM_OP_MOVW)
LBT_ARM_J(lbt_armmov_d, ARM_OP_MOVD)

static bool trans_lbt_armmove(DisasContext *ctx, arg_lbt_arm_move *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_ARM(ctx)) { return false; }
    gen_helper_lbt_arm_move(dest, tcg_env,
                            gpr_src(ctx, a->rj, EXT_NONE), dest,
                            tcg_constant_i32(a->cond));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_armsetj(DisasContext *ctx, arg_lbt_rd_cond *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_ARM(ctx)) { return false; }
    gen_helper_lbt_arm_cond(dest, tcg_env, tcg_constant_i32(a->cond));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_armmfflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_ARM(ctx)) { return false; }
    gen_helper_lbt_arm_mfflag(dest, tcg_env, tcg_constant_i32(a->imm));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_armmtflag(DisasContext *ctx, arg_lbt_rd_imm *a)
{
    if (!avail_LBT_ARM(ctx)) { return false; }
    gen_helper_lbt_arm_mtflag(tcg_env, gpr_src(ctx, a->rd, EXT_NONE),
                              tcg_constant_i32(a->imm));
    return true;
}

/* ---------------- x86 mul / loop ---------------- */
#define LBT_MUL(NAME, SIZE, SIGNED)                                   \
static bool trans_##NAME(DisasContext *ctx, arg_rr_jk *a)             \
{                                                                     \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_helper_lbt_x86_mul(tcg_env, gpr_src(ctx, a->rj, EXT_NONE),    \
                           gpr_src(ctx, a->rk, EXT_NONE),             \
                           tcg_constant_i32(SIZE),                    \
                           tcg_constant_i32(SIGNED));                 \
    return true;                                                      \
}
LBT_MUL(lbt_x86mul_b, 1, 1)
LBT_MUL(lbt_x86mul_h, 2, 1)
LBT_MUL(lbt_x86mul_w, 4, 1)
LBT_MUL(lbt_x86mul_d, 8, 1)
LBT_MUL(lbt_x86mul_bu, 1, 0)
LBT_MUL(lbt_x86mul_hu, 2, 0)
LBT_MUL(lbt_x86mul_wu, 4, 0)
LBT_MUL(lbt_x86mul_du, 8, 0)

static bool trans_lbt_setx86loope(DisasContext *ctx, arg_lbt_rd_rj *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_setloop(dest, tcg_env,
                               gpr_src(ctx, a->rj, EXT_NONE),
                               tcg_constant_i32(0));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_setx86loopne(DisasContext *ctx, arg_lbt_rd_rj *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_setloop(dest, tcg_env,
                               gpr_src(ctx, a->rj, EXT_NONE),
                               tcg_constant_i32(1));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

/* ---------------- generic LBT integer ---------------- */
static bool trans_lbt_addu12i_w(DisasContext *ctx, arg_lbt_rd_rj_imm *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    if (!avail_LBT_ANY(ctx)) { return false; }
    tcg_gen_addi_tl(dest, src, (int32_t)((int32_t)(a->imm << 27) >> 27) << 12);
    tcg_gen_ext32s_tl(dest, dest);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_addu12i_d(DisasContext *ctx, arg_lbt_rd_rj_imm *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    TCGv src = gpr_src(ctx, a->rj, EXT_NONE);
    if (!avail_LBT_ANY(ctx)) { return false; }
    tcg_gen_addi_tl(dest, src, (int32_t)((int32_t)(a->imm << 27) >> 27) << 12);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

#define LBT_ADC(NAME, SIZE, SUB)                                      \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_rd_rj_rk *a)      \
{                                                                     \
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);                        \
    if (!avail_LBT_ANY(ctx)) { return false; }                        \
    gen_helper_lbt_int_adc(dest, tcg_env,                             \
                           gpr_src(ctx, a->rj, EXT_NONE),             \
                           gpr_src(ctx, a->rk, EXT_NONE),             \
                           tcg_constant_i32(SIZE),                    \
                           tcg_constant_i32(SUB));                    \
    gen_set_gpr(a->rd, dest, EXT_NONE);                               \
    return true;                                                      \
}
LBT_ADC(lbt_adc_b, 1, 0)
LBT_ADC(lbt_adc_h, 2, 0)
LBT_ADC(lbt_adc_w, 4, 0)
LBT_ADC(lbt_adc_d, 8, 0)
LBT_ADC(lbt_sbc_b, 1, 1)
LBT_ADC(lbt_sbc_h, 2, 1)
LBT_ADC(lbt_sbc_w, 4, 1)
LBT_ADC(lbt_sbc_d, 8, 1)

#define LBT_RCR(NAME, SIZE)                                           \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_rd_rj_rk *a)      \
{                                                                     \
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);                        \
    if (!avail_LBT_ANY(ctx)) { return false; }                        \
    gen_helper_lbt_int_rcr(dest, tcg_env,                             \
                           gpr_src(ctx, a->rj, EXT_NONE),             \
                           gpr_src(ctx, a->rk, EXT_NONE),             \
                           tcg_constant_i32(SIZE));                   \
    gen_set_gpr(a->rd, dest, EXT_NONE);                               \
    return true;                                                      \
}
LBT_RCR(lbt_rcr_b, 1)
LBT_RCR(lbt_rcr_h, 2)
LBT_RCR(lbt_rcr_w, 4)
LBT_RCR(lbt_rcr_d, 8)

/* ---------------- x86 FPU state / conversions ---------------- */
static bool trans_lbt_x86mftop(DisasContext *ctx, arg_lbt_rd *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_mftop(dest, tcg_env);
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

static bool trans_lbt_x86mttop(DisasContext *ctx, arg_lbt_ui3 *a)
{
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_mttop(tcg_env, tcg_constant_i32(a->imm));
    return true;
}

static bool trans_lbt_x86settag(DisasContext *ctx, arg_lbt_settag *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_x86_settag(dest, tcg_env,
                              gpr_src(ctx, a->rd, EXT_NONE),
                              tcg_constant_i32(a->imm1),
                              tcg_constant_i32(a->imm2));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

#define LBT_FCVT_FD_FJ(NAME, HELPER)                                  \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_fd_fj *a)         \
{                                                                     \
    TCGv dest = get_fpr(ctx, a->fd);                                  \
    if (!avail_LBT_X86(ctx)) { return false; }                        \
    gen_helper_##HELPER(dest, tcg_env, get_fpr(ctx, a->fj));          \
    set_fpr(a->fd, dest);                                             \
    return true;                                                      \
}
LBT_FCVT_FD_FJ(lbt_fcvt_ld_d, lbt_fcvt_ld_d)
LBT_FCVT_FD_FJ(lbt_fcvt_ud_d, lbt_fcvt_ud_d)

static bool trans_lbt_fcvt_d_ld(DisasContext *ctx, arg_lbt_fd_fj_fk *a)
{
    TCGv dest = get_fpr(ctx, a->fd);
    if (!avail_LBT_X86(ctx)) { return false; }
    gen_helper_lbt_fcvt_d_ld(dest, tcg_env,
                             get_fpr(ctx, a->fk), get_fpr(ctx, a->fj));
    set_fpr(a->fd, dest);
    return true;
}

/* ---------------- MIPS unaligned memory ---------------- */
#define LBT_MIPS_LD(NAME, HELPER, SIZE)                               \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_rd_rj_simm12 *a)  \
{                                                                     \
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);                        \
    TCGv addr = tcg_temp_new();                                       \
    if (!avail_LBT_MIPS(ctx)) { return false; }                       \
    tcg_gen_addi_tl(addr, gpr_src(ctx, a->rj, EXT_NONE), a->imm);     \
    gen_helper_##HELPER(dest, tcg_env, addr,                          \
                        gpr_src(ctx, a->rd, EXT_NONE),                \
                        tcg_constant_i32(SIZE),                       \
                        tcg_constant_i32(ctx->mem_idx));              \
    gen_set_gpr(a->rd, dest, EXT_NONE);                               \
    return true;                                                      \
}
LBT_MIPS_LD(lbt_ldl_w, lbt_mips_ldl, 4)
LBT_MIPS_LD(lbt_ldr_w, lbt_mips_ldr, 4)
LBT_MIPS_LD(lbt_ldl_d, lbt_mips_ldl, 8)
LBT_MIPS_LD(lbt_ldr_d, lbt_mips_ldr, 8)

#define LBT_MIPS_ST(NAME, HELPER, SIZE)                               \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_rd_rj_simm12 *a)  \
{                                                                     \
    TCGv addr = tcg_temp_new();                                       \
    if (!avail_LBT_MIPS(ctx)) { return false; }                       \
    tcg_gen_addi_tl(addr, gpr_src(ctx, a->rj, EXT_NONE), a->imm);     \
    gen_helper_##HELPER(tcg_env, gpr_src(ctx, a->rd, EXT_NONE), addr, \
                        tcg_constant_i32(SIZE),                       \
                        tcg_constant_i32(ctx->mem_idx));              \
    return true;                                                      \
}
LBT_MIPS_ST(lbt_stl_w, lbt_mips_stl, 4)
LBT_MIPS_ST(lbt_str_w, lbt_mips_str, 4)
LBT_MIPS_ST(lbt_stl_d, lbt_mips_stl, 8)
LBT_MIPS_ST(lbt_str_d, lbt_mips_str, 8)

static bool trans_lbt_jiscr0(DisasContext *ctx, arg_lbt_imm *a)
{
    if (!avail_LBT_ANY(ctx)) { return false; }
    gen_helper_lbt_jiscr(cpu_pc, tcg_env, tcg_constant_i32(a->imm),
                         tcg_constant_i32(0),
                         tcg_constant_i64(ctx->base.pc_next));
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_lbt_jiscr1(DisasContext *ctx, arg_lbt_imm *a)
{
    if (!avail_LBT_ANY(ctx)) { return false; }
    gen_helper_lbt_jiscr(cpu_pc, tcg_env, tcg_constant_i32(a->imm),
                         tcg_constant_i32(1),
                         tcg_constant_i64(ctx->base.pc_next));
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

#define LBT_ROTR(NAME, SIZE)                                          \
static bool trans_##NAME(DisasContext *ctx, arg_lbt_rd_rj_rk *a)      \
{                                                                     \
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);                        \
    if (!avail_LBT_ANY(ctx)) { return false; }                        \
    gen_helper_lbt_int_rotr(dest, tcg_env,                            \
                            gpr_src(ctx, a->rj, EXT_NONE),            \
                            gpr_src(ctx, a->rk, EXT_NONE),            \
                            tcg_constant_i32(SIZE));                  \
    gen_set_gpr(a->rd, dest, EXT_NONE);                               \
    return true;                                                      \
}
LBT_ROTR(lbt_rotr_b, 1)
LBT_ROTR(lbt_rotr_h, 2)

static bool trans_lbt_rotri_b(DisasContext *ctx, arg_lbt_rd_rj_imm4 *a)
{
    TCGv dest = gpr_dst(ctx, a->rd, EXT_NONE);
    if (!avail_LBT_ANY(ctx)) { return false; }
    gen_helper_lbt_int_rotr(dest, tcg_env,
                            gpr_src(ctx, a->rj, EXT_NONE),
                            tcg_constant_tl(a->imm),
                            tcg_constant_i32(1));
    gen_set_gpr(a->rd, dest, EXT_NONE);
    return true;
}

/* rotri.h shares 0x004c4000 with the standard rotri.d encoding.  When the
 * ARM LBT feature is enabled the LBT interpretation wins. */
bool lbt_predecode(DisasContext *ctx, uint32_t insn)
{
    TCGv dest;

    if (!avail_LBT_ARM(ctx)) {
        return false;
    }
    if ((insn & 0xfffffc00u) != 0x004c4000u) {
        return false;
    }
    dest = gpr_dst(ctx, insn & 0x1f, EXT_NONE);
    gen_helper_lbt_int_rotr(dest, tcg_env,
                            gpr_src(ctx, (insn >> 5) & 0x1f, EXT_NONE),
                            tcg_constant_tl((insn >> 10) & 0xf),
                            tcg_constant_i32(2));
    gen_set_gpr(insn & 0x1f, dest, EXT_NONE);
    return true;
}
