/* Translate IR to target machine code */

#include "riscv.c"

/* Compute stack space needed for function's parameters */
void size_func(func_t *fn)
{
    int s = 0, i;

    /* parameters are turned into local variables */
    for (i = 0; i < fn->num_params; i++) {
        s += size_var(&fn->param_defs[i]);
        fn->param_defs[i].offset = s; /* stack offset */
    }

    /* align to 16 bytes */
    if ((s & 15) > 0)
        s = (s - (s & 15)) + 16;
    if (s > 2047)
        error("Local stack size exceeded");

    fn->params_size = s;
}

/* Return stack size required after block local variables */
int size_block(block_t *blk)
{
    int size = 0, i, offset;

    /* our offset starts from parent's offset */
    if (!blk->parent)
        offset = blk->func ? blk->func->params_size : 0;
    else
        offset = size_block(blk->parent);

    /* declared locals */
    for (i = 0; i < blk->next_local; i++) {
        int vs = size_var(&blk->locals[i]);
        /* look up value off stack */
        blk->locals[i].offset = size + offset + vs;
        size += vs;
    }

    /* align to 16 bytes */
    if ((size & 15) > 0)
        size = (size - (size & 15)) + 16;
    if (size > 2047)
        error("Local stack size exceeded");

    /* save in block for stack allocation */
    blk->locals_size = size;
    return size + offset;
}

/* Compute stack necessary sizes for all functions */
void size_funcs(int data_start)
{
    block_t *blk;
    int i;

    /* size functions */
    for (i = 0; i < funcs_idx; i++)
        size_func(&FUNCS[i]);

    /* size blocks excl. global block */
    for (i = 1; i < blocks_idx; i++)
        size_block(&BLOCKS[i]);

    /* allocate data for globals, in block 0 */
    blk = &BLOCKS[0];
    for (i = 0; i < blk->next_local; i++) {
        blk->locals[i].offset = elf_data_idx; /* set offset in data section */
        elf_add_symbol(blk->locals[i].var_name, strlen(blk->locals[i].var_name),
                       data_start + elf_data_idx);
        /* TODO: add .bss section */
        if (!strcmp(blk->locals[i].type_name, "int") &&
            blk->locals[i].init_val != 0)
            elf_write_data_int(blk->locals[i].init_val);
        else
            elf_data_idx += size_var(&blk->locals[i]);
    }
}

/* Return expected binary length of an IR instruction in bytes */
int get_code_length(ir_instr_t *ii)
{
    opcode_t op = ii->op;

    switch (op) {
    case OP_func_extry: {
        func_t *fn = find_func(ii->str_param1);
        return 16 + (fn->num_params << 2);
    }
    case OP_call:
    case OP_indirect:
        return ii->param_no ? 8 : 4;
    case OP_load_constant:
        return (ii->int_param1 > -2048 && ii->int_param1 < 2047) ? 4 : 8;
    case OP_block_start:
    case OP_block_end: {
        block_t *blk = &BLOCKS[ii->int_param1];
        return (blk->next_local > 0) ? 4 : 0;
    }
    case OP_syscall:
        return 20;
    case OP_eq:
    case OP_neq:
    case OP_lt:
    case OP_leq:
    case OP_gt:
    case OP_geq:
    case OP_func_exit:
        return 16;
    case OP_exit:
        return 12;
    case OP_load_data_address:
    case OP_jz:
    case OP_jnz:
    case OP_push:
    case OP_pop:
    case OP_address_of:
    case OP_start:
        return 8;
    case OP_jump:
    case OP_return:
    case OP_add:
    case OP_sub:
    case OP_mul:
    case OP_div:
    case OP_mod:
    case OP_read:
    case OP_write:
    case OP_log_or:
    case OP_log_and:
    case OP_log_not:
    case OP_bit_or:
    case OP_bit_and:
    case OP_bit_xor:
    case OP_bit_not:
    case OP_negate:
    case OP_lshift:
    case OP_rshift:
        return 4;
    case OP_label:
        return 0;
    default:
        error("Unsupported IR opcode");
    }
    return 0;
}

void emit(int code)
{
    elf_write_code_int(code);
}

void expire_regs(int i)
{
    int t;
    for (t = 0; t < REG_CNT; t++)
        if (REG[t].end < i)
            REG[t].var = NULL;
}

int find_reg(var_t *var)
{
    int i;
    for (i = 0; i < REG_CNT; i++)
        if (REG[i].var == var)
            return i;
    return -1;
}

int get_avl_reg(sym_tbl_t *sym_tbl, var_t *var)
{
    int t;
    for (t = 0; t < REG_CNT; t++)
        if (REG[t].var == NULL) {
            REG[t].var = var;
            REG[t].end = find_sym(sym_tbl, var)->end;
            return t;
        }
    return -1;
}

/* return the available register index, spill out if needs */
int get_src_reg(sym_tbl_t *sym_tbl, var_t *var, int *reserved)
{
    int reg_idx = find_reg(var);
    if (reg_idx > -1)
        return reg_idx;

    ph2_ir_t *ph2_ir;
    reg_idx = get_avl_reg(sym_tbl, var);
    if (reg_idx > -1) {
        ph2_ir = add_ph2_ir(OP_load);
        ph2_ir->dest = reg_idx;
        ph2_ir->src0 = find_sym(sym_tbl, var)->offset;
        return reg_idx;
    }

    int i, ofs;
    int t = 0;
    for (i = 0; i < REG_CNT; i++) {
        if (reserved != NULL)
            if (*reserved == i)
                continue;
        if (REG[i].end > t) {
            t = REG[i].end;
            reg_idx = i;
        }
    }

    /* skip this: it should spill itself if it has the longest lifetime */
    if (0 && find_sym(sym_tbl, var)->end > REG[reg_idx].end) {
        ;
    } else {
        ofs = find_sym(sym_tbl, REG[reg_idx].var)->offset;
        if (ofs == -1) {
            ofs = sym_tbl->stack_size;
            if (REG[reg_idx].var->is_global == 1) {
                find_sym(sym_tbl, REG[reg_idx].var)->offset =
                    global_syms->stack_size;
                global_syms->stack_size += 4;
            } else {
                find_sym(sym_tbl, REG[reg_idx].var)->offset =
                    sym_tbl->stack_size;
                sym_tbl->stack_size += 4;
            }
        }
        if (REG[reg_idx].var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_store);
        else
            ph2_ir = add_ph2_ir(OP_store);
        ph2_ir->src0 = reg_idx;
        ph2_ir->src1 = ofs;

        REG[reg_idx].var = var;
        REG[reg_idx].end = find_sym(sym_tbl, var)->end;

        if (var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_load);
        else
            ph2_ir = add_ph2_ir(OP_load);
        ph2_ir->dest = reg_idx;
        ph2_ir->src0 = find_sym(sym_tbl, var)->offset;
        return reg_idx;
    }
}

int get_dest_reg(sym_tbl_t *sym_tbl,
                 var_t *var,
                 int pc,
                 int *src0,
                 int *src1,
                 int hold_src)
{
    int reg_idx = find_reg(var);
    if (reg_idx > -1)
        return reg_idx;

    reg_idx = get_avl_reg(sym_tbl, var);
    if (reg_idx > -1)
        return reg_idx;

    if (hold_src == 0) {
        if (src0 != NULL)
            if (REG[*src0].end == pc) {
                REG[*src0].var = var;
                REG[*src0].end = find_sym(sym_tbl, var)->end;
                return *src0;
            }
        if (src1 != NULL)
            if (REG[*src1].end == pc) {
                REG[*src1].var = var;
                REG[*src1].end = find_sym(sym_tbl, var)->end;
                return *src1;
            }
    }

    ph2_ir_t *ph2_ir;
    int i, ofs;
    int t = 0;
    for (i = 0; i < REG_CNT; i++) {
        if (hold_src == 1) {
            if (src0 != NULL)
                if (src0[0] == i)
                    continue;
            if (src1 != NULL)
                if (src1[0] == i)
                    continue;
        }
        if (REG[i].end > t) {
            t = REG[i].end;
            reg_idx = i;
        }
    }

    if (0 && find_sym(sym_tbl, var)->end > REG[reg_idx].end) {
        ;
    } else {
        ofs = find_sym(sym_tbl, REG[reg_idx].var)->offset;
        if (ofs == -1) {
            ofs = sym_tbl->stack_size;
            if (REG[reg_idx].var->is_global == 1) {
                find_sym(sym_tbl, REG[reg_idx].var)->offset =
                    global_syms->stack_size;
                global_syms->stack_size += 4;
            } else {
                find_sym(sym_tbl, REG[reg_idx].var)->offset =
                    sym_tbl->stack_size;
                sym_tbl->stack_size += 4;
            }
        }
        if (REG[reg_idx].var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_store);
        else
            ph2_ir = add_ph2_ir(OP_store);
        ph2_ir->src0 = reg_idx;
        ph2_ir->src1 = ofs;

        REG[reg_idx].var = var;
        REG[reg_idx].end = find_sym(sym_tbl, var)->end;
        return reg_idx;
    }
}

void dump_ph2_ir()
{
    if (dump_ir == 0)
        return;

    ph2_ir_t *ph2_ir;

    int i, j, k;
    for (i = 0; i < ph2_ir_idx; i++) {
        ph2_ir = &PH2_IR[i];

        switch (ph2_ir->op) {
        case OP_define:
            printf("%s:", ph2_ir->func_name);
            break;
        case OP_block_start:
        case OP_block_end:
        case OP_allocat:
            continue;
        case OP_load_constant:
            printf("\tli %%a%c, $%d", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_address_of:
            printf("\t%%a%c = %%sp + %d", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_load_data_address:
            printf("\t%%a%c = %d", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_assign:
            printf("\t%%a%c = %%a%c", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_branch:
            printf("\tbeqz %%a%c, %s", ph2_ir->src0 + 48, ph2_ir->false_label);
            break;
        case OP_label:
            printf("%s:", ph2_ir->func_name);
            break;
        case OP_jump:
            printf("\tj %s", ph2_ir->func_name);
            break;
        case OP_load:
            printf("\tload %%a%c, %%sp(%d)", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_store:
            printf("\tstore %%a%c, %%sp(%d)", ph2_ir->src0 + 48, ph2_ir->src1);
            break;
        case OP_read:
            printf("\t%%a%c = (%%a%c)", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_write:
            printf("\t(%%a%c) = %%a%c", ph2_ir->src1 + 48, ph2_ir->src0 + 48);
            break;
        case OP_call:
            printf("\tcall @%s", ph2_ir->func_name);
            break;
        case OP_return:
            if (ph2_ir->src0 == -1)
                printf("\tret");
            else
                printf("\tret %%a%c", ph2_ir->src0 + 48);
            break;
        case OP_negate:
            printf("\tneg %%a%c, %%a%c", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_add:
            printf("\t%%a%c = add %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_sub:
            printf("\t%%a%c = sub %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_mul:
            printf("\t%%a%c = mul %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_div:
            printf("\t%%a%c = div %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_eq:
            printf("\t%%a%c = eq %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_neq:
            printf("\t%%a%c = neq %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_gt:
            printf("\t%%a%c = gt %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_lt:
            printf("\t%%a%c = gt %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_geq:
            printf("\t%%a%c = geq %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_leq:
            printf("\t%%a%c = leq %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_bit_and:
            printf("\t%%a%c = and %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_bit_or:
            printf("\t%%a%c = or %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_bit_not:
            printf("\t%%a%c = not %%a%c", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_bit_xor:
            printf("\t%%a%c = xor %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_log_and:
            printf("\t%%a%c = and %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_log_or:
            printf("\t%%a%c = or %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_log_not:
            printf("\t%%a%c = not %%a%c", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_rshift:
            printf("\t%%a%c = rshift %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        case OP_lshift:
            printf("\t%%a%c = lshift %%a%c, %%a%c", ph2_ir->dest + 48,
                   ph2_ir->src0 + 48, ph2_ir->src1 + 48);
            break;
        default:
            break;
        }
        printf("\n");
    }
}

/* BAD: force to spill all registers to keep the consistence of context in
 *      registers before/after function calling */
void spill_used_regs(sym_tbl_t *sym_tbl, int *pc)
{
    int i, ofs;
    ph2_ir_t *ph2_ir;
    for (i = 0; i < REG_CNT; i++) {
        if (REG[i].var == NULL)
            continue;
        if (pc && REG[i].end == *pc)
            continue;

        if (REG[i].var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_store);
        else
            ph2_ir = add_ph2_ir(OP_store);
        ph2_ir->src0 = i;
        ofs = find_sym(sym_tbl, REG[i].var)->offset;
        if (ofs == -1) {
            ofs = sym_tbl->stack_size;
            if (REG[i].var->is_global == 1) {
                find_sym(sym_tbl, REG[i].var)->offset = global_syms->stack_size;
                global_syms->stack_size += 4;
            } else {
                find_sym(sym_tbl, REG[i].var)->offset = sym_tbl->stack_size;
                sym_tbl->stack_size += 4;
            }
        }
        ph2_ir->src1 = ofs;
        REG[i].var = NULL;
    }
}

void code_generate()
{
    dump_ph1_ir();
    printf("===\n");

    block_t *blk = NULL;

    ph1_ir_t *ph1_ir;
    ph2_ir_t *ph2_ir;
    func_t *fn;
    sym_tbl_t *sym_tbl;
    int ofs;
    int reg_idx;
    int reg_idx_src0;
    int reg_idx_src1;

    strcpy(LABEL_LUT[label_lut_idx].name, "__syscall");
    LABEL_LUT[label_lut_idx].offset = 24 + 20;
    label_lut_idx++;

    global_syms = &SYM_TBL[MAX_SYMTBL - 1];

    /* BAD: extend the end of life to the loop end for every vars in loop */
    int is_in_loop = 0, loop_end_idx;
    char t[16];

    int i, j, k, l;
    for (i = 0; i < ph1_ir_idx; i++) {
        ph1_ir = &PH1_IR[i];

        switch (ph1_ir->op) {
        case OP_define:
            fn = find_func(ph1_ir->func_name);
            sym_tbl = add_sym_tbl(fn);
            for (j = 0; j < fn->num_params; j++)
                add_sym(sym_tbl, &fn->param_defs[j]);
            break;
        case OP_allocat:
            if (ph1_ir->src0->is_global == 1) {
                add_sym(global_syms, ph1_ir->src0);
                set_sym_end(global_syms, ph1_ir->src0, 1 << 16);
            } else
                add_sym(sym_tbl, ph1_ir->src0);
            break;
        case OP_load_constant:
            add_sym(sym_tbl, ph1_ir->dest);
            break;
        case OP_load_data_address:
            add_sym(sym_tbl, ph1_ir->dest);
            break;
        case OP_assign:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_label:
            if (is_in_loop == 1)
                break;
            if (ph1_ir->src0->init_val != 0) {
                is_in_loop = 1;
                loop_end_idx = ph1_ir->src0->init_val;
                strcpy(t, ph1_ir->src0->var_name);
            }
            break;
        case OP_jump:
            if (!strcmp(ph1_ir->dest->var_name, t)) {
                is_in_loop = 0;
            }
            break;
        case OP_branch:
            set_sym_end(sym_tbl, ph1_ir->dest,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_push:
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_func_ret:
            add_sym(sym_tbl, ph1_ir->dest);
            break;
        case OP_return:
            if (ph1_ir->src0)
                set_sym_end(sym_tbl, ph1_ir->src0,
                            is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_address_of:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_read:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_write:
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            set_sym_end(sym_tbl, ph1_ir->dest,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_negate:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_eq:
        case OP_neq:
        case OP_gt:
        case OP_lt:
        case OP_geq:
        case OP_leq:
        case OP_bit_and:
        case OP_bit_or:
        case OP_bit_xor:
        case OP_log_and:
        case OP_log_or:
        case OP_rshift:
        case OP_lshift:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            set_sym_end(sym_tbl, ph1_ir->src1,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        case OP_bit_not:
        case OP_log_not:
            add_sym(sym_tbl, ph1_ir->dest);
            set_sym_end(sym_tbl, ph1_ir->src0,
                        is_in_loop == 1 ? loop_end_idx : i);
            break;
        default:
            break;
        }
    }

    for (i = 0; i < REG_CNT; i++)
        REG[i].var = NULL;

    int stack_idx = 0;
    for (i = 0; i < ph1_ir_idx; i++) {
        ph1_ir = &PH1_IR[i];
        expire_regs(i);

        /* BAD: should restore the context of register before calling */
        if (i > 0 && PH1_IR[i - 1].op == OP_call)
            for (j = 0; j < REG_CNT; j++)
                REG[j].var = NULL;

        switch (ph1_ir->op) {
        case OP_define:
            fn = find_func(ph1_ir->func_name);
            sym_tbl = find_sym_tbl(fn);

            ph2_ir = add_ph2_ir(OP_define);
            strcpy(ph2_ir->func_name, ph1_ir->func_name);

            /* set arguments availiable, more than 8 will cause error */
            for (j = 0; j < fn->num_params; j++) {
                REG[j].var = sym_tbl->syms[j].var;
                REG[j].end = sym_tbl->syms[j].end;
            }
            /* if it's variadic, store all values in register first */
            if (fn->va_args == 1)
                spill_used_regs(sym_tbl, NULL);

            break;
        case OP_allocat:
            if (ph1_ir->src0->is_global == 1) {
                find_sym(sym_tbl, ph1_ir->src0)->offset =
                    global_syms->stack_size;
                if (ph1_ir->src0->array_size == 0) {
                    if (strcmp(ph1_ir->src0->type_name, "int") &&
                        strcmp(ph1_ir->src0->type_name, "char")) {
                        int remainder =
                            find_type(ph1_ir->src0->type_name)->size & 3;
                        global_syms->stack_size +=
                            find_type(ph1_ir->src0->type_name)->size;
                        global_syms->stack_size += (4 - remainder);
                    } else {
                        /* word aligned */
                        global_syms->stack_size += 4;
                    }
                } else {
                    if (ph1_ir->src0->is_ptr)
                        global_syms->stack_size +=
                            PTR_SIZE * ph1_ir->src0->array_size;
                    else
                        global_syms->stack_size +=
                            find_type(ph1_ir->src0->type_name)->size *
                            ph1_ir->src0->array_size;
                }
            } else {
                find_sym(sym_tbl, ph1_ir->src0)->offset = sym_tbl->stack_size;
                if (ph1_ir->src0->array_size == 0) {
                    if (strcmp(ph1_ir->src0->type_name, "int") &&
                        strcmp(ph1_ir->src0->type_name, "char")) {
                        int remainder =
                            find_type(ph1_ir->src0->type_name)->size & 3;
                        sym_tbl->stack_size +=
                            find_type(ph1_ir->src0->type_name)->size;
                        sym_tbl->stack_size += (4 - remainder);
                    } else {
                        /* word aligned */
                        sym_tbl->stack_size += 4;
                    }
                } else {
                    sym_tbl->stack_size += PTR_SIZE;

                    reg_idx =
                        get_dest_reg(sym_tbl, ph1_ir->src0, i, NULL, NULL, 0);
                    ph2_ir = add_ph2_ir(OP_address_of);
                    ph2_ir->src0 = sym_tbl->stack_size;
                    ph2_ir->dest = reg_idx;

                    if (ph1_ir->src0->is_ptr)
                        sym_tbl->stack_size +=
                            PTR_SIZE * ph1_ir->src0->array_size;
                    else
                        sym_tbl->stack_size +=
                            find_type(ph1_ir->src0->type_name)->size *
                            ph1_ir->src0->array_size;
                }
            }
            break;
        case OP_load_constant:
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, NULL, NULL, 0);

            ph2_ir = add_ph2_ir(OP_load_constant);
            ph2_ir->src0 = ph1_ir->dest->init_val;
            ph2_ir->dest = reg_idx;
            break;
        case OP_load_data_address:
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, NULL, NULL, 0);
            ph2_ir = add_ph2_ir(OP_load_data_address);
            ph2_ir->src0 = ph1_ir->dest->init_val;
            ph2_ir->dest = reg_idx;
            break;
        case OP_assign:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx =
                get_dest_reg(sym_tbl, ph1_ir->dest, i, &reg_idx_src0, NULL, 0);

            ph2_ir = add_ph2_ir(OP_assign);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->dest = reg_idx;
            break;
        case OP_label:
            if (PH2_IR[ph2_ir_idx - 1].op != OP_branch &&
                PH2_IR[ph2_ir_idx - 1].op != OP_jump)
                spill_used_regs(sym_tbl, NULL);
            ph2_ir = add_ph2_ir(OP_label);
            strcpy(ph2_ir->func_name, ph1_ir->src0->var_name);
            break;
        case OP_jump:
            spill_used_regs(sym_tbl, NULL);
            ph2_ir = add_ph2_ir(OP_jump);
            strcpy(ph2_ir->func_name, ph1_ir->dest->var_name);
            break;
        case OP_branch:
            spill_used_regs(sym_tbl, &i);
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->dest, NULL);
            ph2_ir = add_ph2_ir(OP_branch);
            ph2_ir->src0 = reg_idx_src0;
            strcpy(ph2_ir->true_label, ph1_ir->src0->var_name);
            strcpy(ph2_ir->false_label, ph1_ir->src1->var_name);
            break;
        case OP_push:
            if (stack_idx == 0)
                spill_used_regs(sym_tbl, NULL);

            ofs = find_sym(sym_tbl, ph1_ir->src0)->offset;
            if (ph1_ir->src0->is_global)
                ph2_ir = add_ph2_ir(OP_global_load);
            else
                ph2_ir = add_ph2_ir(OP_load);
            ph2_ir->src0 = ofs;
            ph2_ir->dest = stack_idx;

            REG[stack_idx].var = ph1_ir->src0;
            REG[stack_idx].end = find_sym(sym_tbl, ph1_ir->src0)->end;
            stack_idx++;
            break;
        case OP_call:
            ph2_ir = add_ph2_ir(OP_call);
            strcpy(ph2_ir->func_name, ph1_ir->func_name);
            stack_idx = 0;
            break;
        case OP_func_ret:
            j = 0;
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, &j, NULL, 0);
            ph2_ir = add_ph2_ir(OP_assign);
            ph2_ir->src0 = 0;
            ph2_ir->dest = reg_idx;
            break;
        case OP_return:
            for (j = 0; j < REG_CNT; j++) {
                if (REG[j].var && REG[j].var->is_global == 1) {
                    ofs = find_sym(global_syms, REG[j].var)->offset;
                    ph2_ir = add_ph2_ir(OP_global_store);
                    ph2_ir->src0 = j;
                    ph2_ir->src1 = ofs;
                }
            }
            if (ph1_ir->src0)
                reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            else
                reg_idx_src0 = -1;

            ph2_ir = add_ph2_ir(OP_return);
            ph2_ir->src0 = reg_idx_src0;
            break;
        case OP_address_of:
            ofs = find_sym(sym_tbl, ph1_ir->src0)->offset;
            if (ofs == -1) {
                for (j = 0; j < REG_CNT; j++)
                    if (REG[j].var == ph1_ir->src0)
                        break;
                ph2_ir = add_ph2_ir(OP_store);
                if (ph1_ir->src0->is_global) {
                    ofs = global_syms->stack_size;
                    global_syms->stack_size += 4;
                } else {
                    ofs = sym_tbl->stack_size;
                    sym_tbl->stack_size += 4;
                }
                find_sym(sym_tbl, ph1_ir->src0)->offset = ofs;
                ph2_ir->src0 = j;
                ph2_ir->src1 = ofs;
            }
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, NULL, NULL, 0);

            if (ph1_ir->src0->is_global)
                ph2_ir = add_ph2_ir(OP_global_addr_of);
            else
                ph2_ir = add_ph2_ir(OP_address_of);
            ph2_ir->src0 = ofs;
            ph2_ir->dest = reg_idx;
            break;
        case OP_read:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx =
                get_dest_reg(sym_tbl, ph1_ir->dest, i, &reg_idx_src0, NULL, 0);
            ph2_ir = add_ph2_ir(OP_read);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = ph1_ir->size;
            ph2_ir->dest = reg_idx;
            break;
        case OP_write:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx_src1 = get_src_reg(sym_tbl, ph1_ir->dest, &reg_idx_src0);
            ph2_ir = add_ph2_ir(OP_write);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = reg_idx_src1;
            ph2_ir->dest = ph1_ir->size;
            break;
        case OP_negate:
        case OP_bit_not:
        case OP_log_not:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx =
                get_dest_reg(sym_tbl, ph1_ir->dest, i, &reg_idx_src0, NULL, 0);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->dest = reg_idx;
            break;
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_eq:
        case OP_neq:
        case OP_gt:
        case OP_lt:
        case OP_geq:
        case OP_leq:
        case OP_bit_and:
        case OP_bit_or:
        case OP_bit_xor:
        case OP_log_or:
        case OP_rshift:
        case OP_lshift:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx_src1 = get_src_reg(sym_tbl, ph1_ir->src1, &reg_idx_src0);
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, &reg_idx_src0,
                                   &reg_idx_src1, 0);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = reg_idx_src1;
            ph2_ir->dest = reg_idx;
            break;
        /* workaround: see details at the codegen of OP_log_and */
        case OP_log_and:
            reg_idx_src0 = get_src_reg(sym_tbl, ph1_ir->src0, NULL);
            reg_idx_src1 = get_src_reg(sym_tbl, ph1_ir->src1, &reg_idx_src0);
            reg_idx = get_dest_reg(sym_tbl, ph1_ir->dest, i, &reg_idx_src0,
                                   &reg_idx_src1, 1);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = reg_idx_src1;
            ph2_ir->dest = reg_idx;
            break;
        default:
            add_ph2_ir(ph1_ir->op);
            break;
        }
    }

    dump_ph2_ir();

    /* calculate the offset of label */
    elf_code_idx = 92; /* 72 + 20 */
    int block_lv = 0;
    for (i = 0; i < ph2_ir_idx; i++) {
        ph2_ir = &PH2_IR[i];

        switch (ph2_ir->op) {
        case OP_define:
            fn = find_func(ph2_ir->func_name);
            strcpy(LABEL_LUT[label_lut_idx].name, ph2_ir->func_name);
            LABEL_LUT[label_lut_idx].offset = elf_code_idx;
            label_lut_idx++;
            elf_code_idx += 12;
            break;
        case OP_block_start:
            block_lv++;
            break;
        case OP_block_end:
            if (--block_lv != 0)
                break;
            if (!strcmp(fn->return_def.type_name, "void"))
                elf_code_idx += 16;
            break;
        case OP_label:
            strcpy(LABEL_LUT[label_lut_idx].name, ph2_ir->func_name);
            LABEL_LUT[label_lut_idx].offset = elf_code_idx;
            label_lut_idx++;
            break;
        case OP_assign:
        case OP_store:
        case OP_load:
        case OP_global_load:
        case OP_global_store:
        case OP_global_addr_of:
        case OP_branch:
        case OP_jump:
        case OP_call:
        case OP_read:
        case OP_write:
        case OP_negate:
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_gt:
        case OP_lt:
        case OP_bit_and:
        case OP_bit_or:
        case OP_bit_xor:
        case OP_bit_not:
        case OP_rshift:
        case OP_lshift:
        case OP_address_of:
            elf_code_idx += 4;
            break;
        case OP_load_constant:
        case OP_load_data_address:
        case OP_neq:
        case OP_geq:
        case OP_leq:
        case OP_log_or:
        case OP_log_not:
            elf_code_idx += 8;
            break;
        case OP_eq:
            elf_code_idx += 12;
            break;
        case OP_log_and:
            elf_code_idx += 16;
            break;
        case OP_return:
            elf_code_idx += 20;
            break;
        default:
            break;
        }
    }

    int elf_data_start = elf_code_start + elf_code_idx;
    block_lv = 0;
    elf_code_idx = 0;

    /* manually insert the entry, exit and syscall */
    elf_add_symbol("__start", strlen("__start"), 0);
    emit(__addi(__sp, __sp, -global_syms->stack_size - 4));
    emit(__sw(__gp, __sp, global_syms->stack_size));
    emit(__addi(__gp, __sp, 0));
    emit(__lw(__a0, __sp, 0));   /* argc */
    emit(__addi(__a1, __sp, 4)); /* argv */
    for (i = 0; i < label_lut_idx; i++)
        if (!strcmp(LABEL_LUT[i].name, "main")) {
            emit(__jal(__ra, LABEL_LUT[i].offset - elf_code_idx));
            break;
        }

    elf_add_symbol("__exit", strlen("__exit"), elf_code_idx);
    emit(__lw(__gp, __sp, global_syms->stack_size));
    emit(__addi(__sp, __sp, global_syms->stack_size + 4));
    emit(__addi(__a0, __a0, 0));
    emit(__addi(__a7, __zero, 93));
    emit(__ecall());

    elf_add_symbol("__syscall", strlen("__syscall"), elf_code_idx);
    emit(__addi(__sp, __sp, -8));
    emit(__sw(__ra, __sp, 0));
    emit(__sw(__s0, __sp, 4));
    emit(__addi(__a7, __a0, 0));
    emit(__addi(__a0, __a1, 0));
    emit(__addi(__a1, __a2, 0));
    emit(__addi(__a2, __a3, 0));
    emit(__ecall());
    emit(__lw(__s0, __sp, 4));
    emit(__lw(__ra, __sp, 0));
    emit(__addi(__sp, __sp, 8));
    emit(__jalr(__zero, __ra, 0));

    for (i = 0; i < ph2_ir_idx; i++) {
        ph2_ir = &PH2_IR[i];

        switch (ph2_ir->op) {
        case OP_define:
            fn = find_func(ph2_ir->func_name);
            sym_tbl = find_sym_tbl(fn);
            emit(__addi(__sp, __sp, -sym_tbl->stack_size - 8));
            emit(__sw(__ra, __sp, sym_tbl->stack_size));
            emit(__sw(__s0, __sp, sym_tbl->stack_size + 4));
            break;
        case OP_block_start:
            block_lv++;
            break;
        case OP_block_end:
            if (--block_lv != 0)
                break;
            if (!strcmp(fn->return_def.type_name, "void")) {
                emit(__lw(__s0, __sp, sym_tbl->stack_size + 4));
                emit(__lw(__ra, __sp, sym_tbl->stack_size));
                emit(__addi(__sp, __sp, sym_tbl->stack_size + 8));
                emit(__jalr(__zero, __ra, 0));
            }
            break;
        case OP_load_constant:
            if (ph2_ir->src0 > -2048 && ph2_ir->src0 < 2047) {
                emit(__addi(__zero, __zero, 0));
                emit(__addi(ph2_ir->dest + 10, __zero, ph2_ir->src0));
            } else {
                emit(__lui(ph2_ir->dest + 10, rv_hi(ph2_ir->src0)));
                emit(__addi(ph2_ir->dest + 10, ph2_ir->dest + 10,
                            rv_lo(ph2_ir->src0)));
            }
            break;
        case OP_load_data_address:
            emit(
                __lui(ph2_ir->dest + 10, rv_hi(ph2_ir->src0 + elf_data_start)));
            emit(__addi(ph2_ir->dest + 10, ph2_ir->dest + 10,
                        rv_lo(ph2_ir->src0 + elf_data_start)));
            break;
        case OP_address_of:
            emit(__addi(ph2_ir->dest + 10, __sp, ph2_ir->src0));
            break;
        case OP_assign:
            emit(__addi(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
            break;
        case OP_branch:
            for (j = 0; j < label_lut_idx; j++)
                if (!strcmp(LABEL_LUT[j].name, ph2_ir->false_label)) {
                    ofs = LABEL_LUT[j].offset;
                    break;
                }
            emit(__beq(ph2_ir->src0 + 10, __zero, ofs - elf_code_idx));
            break;
        case OP_jump:
            for (j = 0; j < label_lut_idx; j++)
                if (!strcmp(LABEL_LUT[j].name, ph2_ir->func_name)) {
                    ofs = LABEL_LUT[j].offset;
                    break;
                }
            emit(__jal(__zero, ofs - elf_code_idx));
            break;
        case OP_load:
            emit(__lw(ph2_ir->dest + 10, __sp, ph2_ir->src0));
            break;
        case OP_store:
            emit(__sw(ph2_ir->src0 + 10, __sp, ph2_ir->src1));
            break;
        case OP_global_load:
            emit(__lw(ph2_ir->dest + 10, __gp, ph2_ir->src0));
            break;
        case OP_global_store:
            emit(__sw(ph2_ir->src0 + 10, __gp, ph2_ir->src1));
            break;
        case OP_global_addr_of:
            emit(__addi(ph2_ir->dest + 10, __gp, ph2_ir->src0));
            break;
        case OP_read:
            if (ph2_ir->src1 == 1)
                emit(__lbu(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
            else if (ph2_ir->src1 == 4)
                emit(__lw(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
            else
                abort();
            break;
        case OP_write:
            if (ph2_ir->dest == 1)
                emit(__sb(ph2_ir->src0 + 10, ph2_ir->src1 + 10, 0));
            else if (ph2_ir->dest == 4)
                emit(__sw(ph2_ir->src0 + 10, ph2_ir->src1 + 10, 0));
            else
                abort();
            break;
        case OP_call:
            for (j = 0; j < label_lut_idx; j++)
                if (!strcmp(LABEL_LUT[j].name, ph2_ir->func_name)) {
                    ofs = LABEL_LUT[j].offset;
                    break;
                }
            emit(__jal(__ra, ofs - elf_code_idx));
            break;
        case OP_return:
            if (ph2_ir->src0 == -1)
                emit(__addi(__zero, __zero, 0));
            else
                emit(__addi(__a0, ph2_ir->src0 + 10, 0));
            emit(__lw(__s0, __sp, sym_tbl->stack_size + 4));
            emit(__lw(__ra, __sp, sym_tbl->stack_size));
            emit(__addi(__sp, __sp, sym_tbl->stack_size + 8));
            emit(__jalr(__zero, __ra, 0));
            break;
        case OP_negate:
            emit(__sub(ph2_ir->dest + 10, __zero, ph2_ir->src0 + 10));
            break;
        case OP_add:
            emit(
                __add(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_sub:
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_mul:
            emit(
                __mul(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_div:
            emit(
                __div(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_eq:
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
            break;
        case OP_neq:
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            break;
        case OP_gt:
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src1 + 10, ph2_ir->src0 + 10));
            break;
        case OP_lt:
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_geq:
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
            break;
        case OP_leq:
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src1 + 10, ph2_ir->src0 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
            break;
        case OP_bit_and:
            emit(
                __and(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_bit_or:
            emit(__or(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_bit_xor:
            emit(
                __xor(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_bit_not:
            emit(__xori(ph2_ir->dest + 10, ph2_ir->src0 + 10, -1));
            break;
        /* workaround for the logical-and (&&) operation */
        case OP_log_and:
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->src0 + 10));
            emit(__sub(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            emit(
                __and(ph2_ir->dest + 10, ph2_ir->dest + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            break;
        case OP_log_or:
            emit(__or(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            break;
        case OP_log_not:
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->src0 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
            break;
        case OP_rshift:
            emit(
                __sra(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        case OP_lshift:
            emit(
                __sll(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            break;
        default:
            break;
        }
    }
}
