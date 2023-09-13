/* Translate IR to target machine code */

#include "riscv.c"

void emit(int code)
{
    elf_write_code_int(code);
}

void expire_regs(int i)
{
    int t;
    for (t = 0; t < REG_CNT; t++) {
        if (REG[t].var == NULL)
            continue;
        if (REG[t].var->eol < i) {
            REG[t].var = NULL;
            REG[t].polluted = 0;
        }
    }
}

int find_in_regs(var_t *var)
{
    int i;
    for (i = 0; i < REG_CNT; i++)
        if (REG[i].var == var)
            return i;
    return -1;
}

int try_avl_reg()
{
    int i;
    for (i = 0; i < REG_CNT; i++)
        if (REG[i].var == NULL)
            return i;
    return -1;
}

/* return the available index of register, spill out the value in
 * it if needed */
int get_src_reg(func_t *fn, var_t *var, int reserved)
{
    ph2_ir_t *ph2_ir;
    int reg_idx, i, ofs, t = 0;

    reg_idx = find_in_regs(var);
    if (reg_idx > -1)
        return reg_idx;

    reg_idx = try_avl_reg();
    if (reg_idx > -1) {
        REG[reg_idx].var = var;
        REG[reg_idx].polluted = 0;

        if (var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_load);
        else
            ph2_ir = add_ph2_ir(OP_load);
        ph2_ir->dest = reg_idx;
        ph2_ir->src0 = var->offset;
        return reg_idx;
    }

    for (i = 0; i < REG_CNT; i++) {
        if (reserved == i)
            continue;
        if (REG[i].var->eol > t) {
            t = REG[i].var->eol;
            reg_idx = i;
        }
    }

    /* skip this: it should spill itself if it has the longest lifetime */
    if (0 && var->eol > REG[reg_idx].var->eol) {
        ;
    } else {
        ofs = REG[reg_idx].var->offset;
        if (ofs == 0) {
            if (REG[reg_idx].var->is_global == 1) {
                ofs = FUNCS[0].stack_size;
                REG[reg_idx].var->offset = ofs;
                FUNCS[0].stack_size += 4;
            } else {
                ofs = fn->stack_size;
                REG[reg_idx].var->offset = ofs;
                fn->stack_size += 4;
            }
        }
        if (REG[reg_idx].polluted) {
            if (REG[reg_idx].var->is_global == 1)
                ph2_ir = add_ph2_ir(OP_global_store);
            else
                ph2_ir = add_ph2_ir(OP_store);
            ph2_ir->src0 = reg_idx;
            ph2_ir->src1 = ofs;
        }

        REG[reg_idx].var = var;

        if (var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_load);
        else
            ph2_ir = add_ph2_ir(OP_load);
        ph2_ir->dest = reg_idx;
        ph2_ir->src0 = var->offset;
        REG[reg_idx].polluted = 0;
        return reg_idx;
    }
}

/* `hold_src1` is used for `OP_log_and` */
int get_dest_reg(func_t *fn,
                 var_t *var,
                 int pc,
                 int src0,
                 int src1,
                 int hold_src1)
{
    ph2_ir_t *ph2_ir;
    int reg_idx, i, ofs, t = 0;

    reg_idx = find_in_regs(var);
    if (reg_idx > -1) {
        REG[reg_idx].polluted = 1;
        return reg_idx;
    }

    reg_idx = try_avl_reg();
    if (reg_idx > -1) {
        REG[reg_idx].var = var;
        REG[reg_idx].polluted = 1;
        return reg_idx;
    }

    if (src0 > -1)
        if (REG[src0].var->eol == pc) {
            REG[src0].var = var;
            REG[src0].polluted = 1;
            return src0;
        }
    if (!hold_src1 && src1 > -1)
        if (REG[src1].var->eol == pc) {
            REG[src1].var = var;
            REG[src1].polluted = 1;
            return src1;
        }

    for (i = 0; i < REG_CNT; i++) {
        if (hold_src1 && src1 == i)
            continue;
        if (REG[i].var->eol > t) {
            t = REG[i].var->eol;
            reg_idx = i;
        }
    }

    if (0 && var->eol > REG[reg_idx].var->eol) {
        ;
    } else {
        ofs = REG[reg_idx].var->offset;
        if (ofs == 0) {
            if (REG[reg_idx].var->is_global == 1) {
                ofs = FUNCS[0].stack_size;
                REG[reg_idx].var->offset = ofs;
                FUNCS[0].stack_size += 4;
            } else {
                ofs = fn->stack_size;
                REG[reg_idx].var->offset = ofs;
                fn->stack_size += 4;
            }
        }
        if (REG[reg_idx].polluted) {
            if (REG[reg_idx].var->is_global == 1)
                ph2_ir = add_ph2_ir(OP_global_store);
            else
                ph2_ir = add_ph2_ir(OP_store);
            ph2_ir->src0 = reg_idx;
            ph2_ir->src1 = ofs;
        }

        REG[reg_idx].var = var;
        REG[reg_idx].polluted = 1;
        return reg_idx;
    }
}

/* If it's the end of a block, spill the global vars only */
void spill_used_regs(func_t *fn, int pc, int global_only)
{
    int i, ofs;
    ph2_ir_t *ph2_ir;
    for (i = 0; i < REG_CNT; i++) {
        if (REG[i].var == NULL)
            continue;

        /* if the var is going to expire after this instruction,
         * don't store it */
        if (REG[i].var->eol == pc)
            continue;

        if (REG[i].polluted == 0) {
            REG[i].var = NULL;
            continue;
        }

        if (REG[i].var->is_global == 1)
            ph2_ir = add_ph2_ir(OP_global_store);
        else if (!global_only)
            ph2_ir = add_ph2_ir(OP_store);
        else
            continue;

        ph2_ir->src0 = i;
        ofs = REG[i].var->offset;
        if (ofs == 0) {
            if (REG[i].var->is_global == 1) {
                ofs = FUNCS[0].stack_size;
                REG[i].var->offset = ofs;
                FUNCS[0].stack_size += 4;
            } else {
                ofs = fn->stack_size;
                REG[i].var->offset = ofs;
                fn->stack_size += 4;
            }
        }
        ph2_ir->src1 = ofs;

        REG[i].var = NULL;
        REG[i].polluted = 0;
    }
}

void dump_ph2_ir();

void code_generate()
{
    ph1_ir_t *ph1_ir;
    ph2_ir_t *ph2_ir;
    func_t *fn;
    int i, j, ofs;
    int reg_idx, reg_idx_src0, reg_idx_src1;
    int elf_data_start;

    int argument_idx = 0;
    int block_lv = 0;


    int loop_end_idx = 0;

    /* extend live range which is "write after read (WAR)" in loop to make sure
     * the storage*/
    int loop_lv[10];
    int loop_lv_idx = 0;

    dump_ph1_ir();

    for (i = 0; i < global_ir_idx; i++) {
        ph1_ir = &GLOBAL_IR[i];
        if (ph1_ir->op == OP_allocat) {
            set_var_liveout(ph1_ir->src0, 1 << 28);

        } else if (ph1_ir->op == OP_assign) {
            set_var_liveout(ph1_ir->src0, i);
        } else if (ph1_ir->op != OP_load_constant)
            error("Unsupported global operation");
    }

    for (i = 0; i < ph1_ir_idx; i++) {
        ph1_ir = &PH1_IR[i];

        switch (ph1_ir->op) {
        case OP_allocat:
            if (ph1_ir->src0->is_global == 1) {
                set_var_liveout(ph1_ir->src0, 1 << 28);
                error("Unknown global allocation in body statement");
            }
            break;
        case OP_assign:
            set_var_liveout(ph1_ir->src0, i);
            if (loop_end_idx != 0)
                ph1_ir->src0->in_loop = 1;
            if (ph1_ir->dest->in_loop != 0)
                set_var_liveout(ph1_ir->dest, loop_end_idx);
            break;
        case OP_label:
            if (loop_end_idx == i) {
                loop_end_idx = loop_lv[--loop_lv_idx];
                break;
            }
            if (ph1_ir->src0->init_val != 0) {
                loop_lv[loop_lv_idx++] = loop_end_idx;
                loop_end_idx = ph1_ir->src0->init_val;
            }
            break;
        case OP_branch:
            set_var_liveout(ph1_ir->dest, i);
            break;
        case OP_push:
            set_var_liveout(ph1_ir->src0, i);
            break;
        case OP_return:
            if (ph1_ir->src0)
                set_var_liveout(ph1_ir->src0, i);
            break;
        case OP_address_of:
            set_var_liveout(ph1_ir->src0, i);
            if (loop_end_idx != 0)
                ph1_ir->src0->in_loop = 1;
            if (ph1_ir->dest->in_loop != 0)
                set_var_liveout(ph1_ir->dest, loop_end_idx);
            break;
        case OP_read:
            set_var_liveout(ph1_ir->src0, i);
            if (loop_end_idx != 0)
                ph1_ir->src0->in_loop = 1;
            if (ph1_ir->dest->in_loop != 0)
                set_var_liveout(ph1_ir->dest, loop_end_idx);
            break;
        case OP_write:
            if (ph1_ir->src0->is_func == 0)
                set_var_liveout(ph1_ir->src0, i);
            set_var_liveout(ph1_ir->dest, i);
            break;
        case OP_indirect:
            set_var_liveout(ph1_ir->src0, i);
            break;
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_mod:
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
            set_var_liveout(ph1_ir->src0, i);
            set_var_liveout(ph1_ir->src1, i);
            if (loop_end_idx != 0) {
                ph1_ir->src0->in_loop = 1;
                ph1_ir->src1->in_loop = 1;
            }
            if (ph1_ir->dest->in_loop != 0)
                set_var_liveout(ph1_ir->dest, loop_end_idx);
            break;
        case OP_bit_not:
        case OP_log_not:
        case OP_negate:
            set_var_liveout(ph1_ir->src0, i);
            if (loop_end_idx != 0)
                ph1_ir->src0->in_loop = 1;
            if (ph1_ir->dest->in_loop != 0)
                set_var_liveout(ph1_ir->dest, loop_end_idx);
            break;
        default:
            break;
        }
    }

    /* initiate register file */
    for (i = 0; i < REG_CNT; i++) {
        REG[i].var = NULL;
        REG[i].polluted = 0;
    }

    for (i = 0; i < global_ir_idx; i++) {
        ph1_ir = &GLOBAL_IR[i];

        if (ph1_ir->op == OP_allocat) {
            func_t *t = &FUNCS[0];
            ph1_ir->src0->offset = t->stack_size;
            if (ph1_ir->src0->array_size == 0) {
                if (ph1_ir->src0->is_ptr) {
                    t->stack_size += 4;
                } else if (strcmp(ph1_ir->src0->type_name, "int") &&
                           strcmp(ph1_ir->src0->type_name, "char")) {
                    type_t *type = find_type(ph1_ir->src0->type_name);
                    t->stack_size += type->size;
                } else
                    t->stack_size += 4;
            } else {
                int sz = t->stack_size;
                t->stack_size += PTR_SIZE;

                reg_idx = get_dest_reg(t, ph1_ir->src0, i, -1, -1, 0);

                ph2_ir = add_ph2_ir(OP_global_addr_of);
                ph2_ir->src0 = t->stack_size;
                ph2_ir->dest = reg_idx;

                if (ph1_ir->src0->is_ptr)
                    t->stack_size += PTR_SIZE * ph1_ir->src0->array_size;
                else {
                    type_t *type = find_type(ph1_ir->src0->type_name);
                    t->stack_size += type->size * ph1_ir->src0->array_size;
                }

                ph2_ir = add_ph2_ir(OP_global_store);
                ph2_ir->src0 = reg_idx;
                ph2_ir->src1 = sz;
            }
        } else if (ph1_ir->op == OP_load_constant) {
            func_t *fn = &FUNCS[0];
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, -1, -1, 0);
            ph2_ir = add_ph2_ir(OP_load_constant);
            ph2_ir->src0 = ph1_ir->dest->init_val;
            ph2_ir->dest = reg_idx;
        } else if (ph1_ir->op == OP_assign) {
            func_t *fn = &FUNCS[0];
            int ofs;

            reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, reg_idx_src0, -1, 0);
            ph2_ir = add_ph2_ir(OP_assign);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->dest = reg_idx;

            ph2_ir = add_ph2_ir(OP_global_store);
            ofs = ph1_ir->dest->offset;
            ph2_ir->src0 = reg_idx;
            ph2_ir->src1 = ofs;
        } else
            error("Unsupported global operation");
    }

    /* jump to entry point after global statements */
    ph2_ir = add_ph2_ir(OP_jump);
    strcpy(ph2_ir->func_name, "main");

    for (i = 0; i < ph1_ir_idx; i++) {
        ph1_ir = &PH1_IR[i];
        expire_regs(i);

        if (i > 0 &&
            (PH1_IR[i - 1].op == OP_call || PH1_IR[i - 1].op == OP_indirect))
            for (j = 0; j < REG_CNT; j++)
                REG[j].var = NULL;

        switch (ph1_ir->op) {
        case OP_block_end:
            if (PH1_IR[i - 1].op != OP_return)
                spill_used_regs(fn, -1, 1);
            ph2_ir = add_ph2_ir(OP_block_end);
            break;
        case OP_define:
            fn = find_func(ph1_ir->func_name);
            ph2_ir = add_ph2_ir(OP_define);
            strcpy(ph2_ir->func_name, ph1_ir->func_name);

            /* set arguments availiable, more than 8 will cause error */
            for (j = 0; j < fn->num_params; j++) {
                REG[j].var = &fn->param_defs[j];
                REG[j].polluted = 1;
            }
            for (; j < REG_CNT; j++) {
                REG[j].var = NULL;
                REG[j].polluted = 0;
            }

            spill_used_regs(fn, -1, 0);

            break;
        case OP_allocat: {
            func_t *t;

            if (ph1_ir->src0->is_global == 1)
                t = &FUNCS[0];
            else
                t = fn;

            ph1_ir->src0->offset = t->stack_size;
            if (ph1_ir->src0->array_size == 0) {
                if (ph1_ir->src0->is_ptr) {
                    t->stack_size += 4;
                } else if (strcmp(ph1_ir->src0->type_name, "int") &&
                           strcmp(ph1_ir->src0->type_name, "char")) {
                    type_t *type = find_type(ph1_ir->src0->type_name);
                    t->stack_size += type->size;
                } else
                    t->stack_size += 4;
            } else {
                int sz = t->stack_size;
                t->stack_size += PTR_SIZE;

                reg_idx = get_dest_reg(t, ph1_ir->src0, i, -1, -1, 0);

                if (ph1_ir->src0->is_global == 1)
                    ph2_ir = add_ph2_ir(OP_global_addr_of);
                else
                    ph2_ir = add_ph2_ir(OP_address_of);
                ph2_ir->src0 = t->stack_size;
                ph2_ir->dest = reg_idx;

                if (ph1_ir->src0->is_ptr)
                    t->stack_size += PTR_SIZE * ph1_ir->src0->array_size;
                else {
                    type_t *type = find_type(ph1_ir->src0->type_name);
                    t->stack_size += type->size * ph1_ir->src0->array_size;
                }
                if (ph1_ir->src0->is_global == 1)
                    ph2_ir = add_ph2_ir(OP_global_store);
                else
                    ph2_ir = add_ph2_ir(OP_store);
                ph2_ir->src0 = reg_idx;
                ph2_ir->src1 = sz;
            }
        } break;
        case OP_load_constant:
        case OP_load_data_address:
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, -1, -1, 0);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = ph1_ir->dest->init_val;
            ph2_ir->dest = reg_idx;
            break;
        case OP_label:
            if (PH1_IR[i - 1].op == OP_branch)
                if (strcmp(PH1_IR[i - 1].src0->var_name,
                           ph1_ir->src0->var_name))
                    for (j = 0; j < REG_CNT; j++)
                        REG->var = NULL;

            /* spill at the beginning of the while statement */
            if (PH1_IR[i - 1].op != OP_branch && PH1_IR[i - 1].op != OP_jump)
                spill_used_regs(fn, -1, 0);

            ph2_ir = add_ph2_ir(OP_label);
            strcpy(ph2_ir->func_name, ph1_ir->src0->var_name);
            break;
        case OP_jump:
            spill_used_regs(fn, -1, 0);
            ph2_ir = add_ph2_ir(OP_jump);
            strcpy(ph2_ir->func_name, ph1_ir->dest->var_name);
            break;
        case OP_branch:
            spill_used_regs(fn, i, 0);
            reg_idx_src0 = get_src_reg(fn, ph1_ir->dest, -1);
            ph2_ir = add_ph2_ir(OP_branch);
            ph2_ir->src0 = reg_idx_src0;
            strcpy(ph2_ir->true_label, ph1_ir->src0->var_name);
            strcpy(ph2_ir->false_label, ph1_ir->src1->var_name);
            break;
        case OP_push:
            if (argument_idx == 0)
                spill_used_regs(fn, -1, 0);

            ofs = ph1_ir->src0->offset;
            if (ph1_ir->src0->is_global)
                ph2_ir = add_ph2_ir(OP_global_load);
            else
                ph2_ir = add_ph2_ir(OP_load);
            ph2_ir->src0 = ofs;
            ph2_ir->dest = argument_idx;
            argument_idx++;
            break;
        case OP_call:
            if (PH1_IR[i - 1].op != OP_push)
                spill_used_regs(fn, -1, 0);
            ph2_ir = add_ph2_ir(OP_call);
            strcpy(ph2_ir->func_name, ph1_ir->func_name);
            argument_idx = 0;
            break;
        case OP_func_ret:
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, 0, -1, 0);
            ph2_ir = add_ph2_ir(OP_assign);
            ph2_ir->src0 = 0;
            ph2_ir->dest = reg_idx;
            break;
        case OP_return:
            spill_used_regs(fn, -1, 1);

            if (ph1_ir->src0)
                reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            else
                reg_idx_src0 = -1;

            ph2_ir = add_ph2_ir(OP_return);
            ph2_ir->src0 = reg_idx_src0;
            break;
        case OP_address_of:
            ofs = ph1_ir->src0->offset;
            if (ofs == 0) {
                for (j = 0; j < REG_CNT; j++)
                    if (REG[j].var == ph1_ir->src0)
                        break;
                ph2_ir = add_ph2_ir(OP_store);
                if (ph1_ir->src0->is_global) {
                    ofs = FUNCS[0].stack_size;
                    FUNCS[0].stack_size += 4;
                } else {
                    ofs = fn->stack_size;
                    fn->stack_size += 4;
                }
                ph1_ir->src0->offset = ofs;
                ph2_ir->src0 = j;
                ph2_ir->src1 = ofs;
            }

            /* write the content back into stack, prevent to get the obsolete
             * content when dereferencing */
            for (j = 0; j < REG_CNT; j++)
                if (REG[j].var == ph1_ir->src0)
                    if (REG[j].polluted) {
                        if (REG[j].var->is_global)
                            ph2_ir = add_ph2_ir(OP_global_store);
                        else
                            ph2_ir = add_ph2_ir(OP_store);
                        ph2_ir->src0 = j;
                        ph2_ir->src1 = ofs;
                    }

            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, -1, -1, 0);

            if (ph1_ir->src0->is_global)
                ph2_ir = add_ph2_ir(OP_global_addr_of);
            else
                ph2_ir = add_ph2_ir(OP_address_of);
            ph2_ir->src0 = ofs;
            ph2_ir->dest = reg_idx;
            break;
        case OP_read:
            reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, reg_idx_src0, -1, 0);
            ph2_ir = add_ph2_ir(OP_read);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = ph1_ir->size;
            ph2_ir->dest = reg_idx;
            break;
        case OP_write:
            if (ph1_ir->src0->is_func == 0) {
                /* without this, the content stored in register which represent
                 * to a var won't be updated after writing into its reference */
                spill_used_regs(fn, -1, 0);

                reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
                reg_idx_src1 = get_src_reg(fn, ph1_ir->dest, reg_idx_src0);
                ph2_ir = add_ph2_ir(OP_write);
                ph2_ir->src0 = reg_idx_src0;
                ph2_ir->src1 = reg_idx_src1;
                ph2_ir->dest = ph1_ir->size;
            } else {
                reg_idx_src0 = get_src_reg(fn, ph1_ir->dest, -1);
                ph2_ir = add_ph2_ir(OP_func_addr);
                ph2_ir->src0 = reg_idx_src0;
                strcpy(ph2_ir->func_name, ph1_ir->src0->var_name);
            }
            break;
        case OP_indirect:
            if (PH1_IR[i - 1].op != OP_push)
                spill_used_regs(fn, -1, 0);

            ofs = ph1_ir->src0->offset;

            /* workaround: load into register t6 */
            ph2_ir = add_ph2_ir(OP_load);
            ph2_ir->src0 = ofs;
            ph2_ir->dest = 21;
            ph2_ir = add_ph2_ir(OP_indirect);
            argument_idx = 0;
            break;
        case OP_assign:
        case OP_negate:
        case OP_bit_not:
        case OP_log_not:
            reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, reg_idx_src0, -1, 0);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->dest = reg_idx;
            break;
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_mod:
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
            reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            reg_idx_src1 = get_src_reg(fn, ph1_ir->src1, reg_idx_src0);
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, reg_idx_src0,
                                   reg_idx_src1, 0);
            ph2_ir = add_ph2_ir(ph1_ir->op);
            ph2_ir->src0 = reg_idx_src0;
            ph2_ir->src1 = reg_idx_src1;
            ph2_ir->dest = reg_idx;
            break;
        case OP_log_and:
            /* workaround: see details at the codegen of OP_log_and */
            reg_idx_src0 = get_src_reg(fn, ph1_ir->src0, -1);
            reg_idx_src1 = get_src_reg(fn, ph1_ir->src1, reg_idx_src0);
            reg_idx = get_dest_reg(fn, ph1_ir->dest, i, reg_idx_src0,
                                   reg_idx_src1, 1);
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

    add_label("__syscall", 24 + 20 + 16);

    /* calculate the offset of labels */
    elf_code_idx = 92 + 16; /* 72 + 20 */
    for (i = 0; i < ph2_ir_idx; i++) {
        ph2_ir = &PH2_IR[i];

        switch (ph2_ir->op) {
        case OP_define:
            fn = find_func(ph2_ir->func_name);
            add_label(ph2_ir->func_name, elf_code_idx);
            elf_code_idx += 12 + 8;
            break;
        case OP_block_start:
            block_lv++;
            break;
        case OP_block_end:
            /* handle the function with the implicit return */
            --block_lv;
            if (block_lv != 0)
                break;
            if (!strcmp(fn->return_def.type_name, "void"))
                elf_code_idx += 16 + 8;
            break;
        case OP_label:
            add_label(ph2_ir->func_name, elf_code_idx);
            break;
        case OP_assign:
            if (ph2_ir->dest != ph2_ir->src0)
                elf_code_idx += 4;
            break;
        case OP_store:
            if (ph2_ir->src1 < -2048 || ph2_ir->src1 > 2047)
                elf_code_idx += 16;
            else
                elf_code_idx += 4;
            break;
        case OP_load:
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047)
                elf_code_idx += 16;
            else
                elf_code_idx += 4;
            break;
        case OP_global_load:
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047)
                elf_code_idx += 16;
            else
                elf_code_idx += 4;
            break;
        case OP_global_store:
            if (ph2_ir->src1 < -2048 || ph2_ir->src1 > 2047)
                elf_code_idx += 16;
            else
                elf_code_idx += 4;
            break;
        case OP_global_addr_of:
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047)
                elf_code_idx += 12;
            else
                elf_code_idx += 4;
            break;
        case OP_address_of:
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047)
                elf_code_idx += 12;
            else
                elf_code_idx += 4;
            break;
        case OP_jump:
            if (!strcmp(ph2_ir->func_name, "main"))
                elf_code_idx += 20;
            elf_code_idx += 4;
            break;
        case OP_call:
        case OP_read:
        case OP_write:
        case OP_negate:
        case OP_add:
        case OP_sub:
        case OP_mul:
        case OP_div:
        case OP_mod:
        case OP_gt:
        case OP_lt:
        case OP_bit_and:
        case OP_bit_or:
        case OP_bit_xor:
        case OP_bit_not:
        case OP_rshift:
        case OP_lshift:
        case OP_indirect:
            elf_code_idx += 4;
            break;
        case OP_load_constant:
            if (ph2_ir->src0 > -2048 && ph2_ir->src0 < 2047)
                elf_code_idx += 4;
            else
                elf_code_idx += 8;
            break;
        case OP_load_data_address:
        case OP_neq:
        case OP_geq:
        case OP_leq:
        case OP_log_or:
        case OP_log_not:
            elf_code_idx += 8;
            break;
        case OP_eq:
        case OP_func_addr:
            elf_code_idx += 12;
            break;
        case OP_log_and:
            elf_code_idx += 16;
            break;
        case OP_branch:
            elf_code_idx += 20;
            break;
        case OP_return:
            elf_code_idx += 20 + 8;
            break;
        default:
            break;
        }
    }

    elf_data_start = elf_code_start + elf_code_idx;
    block_lv = 0;
    elf_code_idx = 0;

    /* manually insert the entry, exit and syscall */
    elf_add_symbol("__start", strlen("__start"), 0);
    emit(__sw(__gp, __sp, -4));
    emit(__lw(__a0, __sp, 0));   /* argc */
    emit(__addi(__a1, __sp, 4)); /* argv */
    emit(__lui(__a7, rv_hi(FUNCS[0].stack_size + 4)));
    emit(__addi(__a7, __a7, rv_lo(FUNCS[0].stack_size + 4)));
    emit(__sub(__sp, __sp, __a7));
    emit(__addi(__gp, __sp, 0));
    emit(__jal(__ra, 108 - elf_code_idx));

    elf_add_symbol("__exit", strlen("__exit"), elf_code_idx);
    emit(__lui(__a7, rv_hi(FUNCS[0].stack_size + 4)));
    emit(__addi(__a7, __a7, rv_lo(FUNCS[0].stack_size + 4)));
    emit(__add(__sp, __sp, __a7));
    emit(__lw(__gp, __sp, -4));
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

    /* use brace in switch statement to lower the MAX_LOCALS when
     * self-hosting */
    for (i = 0; i < ph2_ir_idx; i++) {
        ph2_ir = &PH2_IR[i];

        switch (ph2_ir->op) {
        case OP_define: {
            fn = find_func(ph2_ir->func_name);
            emit(__sw(__ra, __sp, -8));
            emit(__sw(__s0, __sp, -4));
            emit(__lui(__s0, rv_hi(fn->stack_size + 8)));
            emit(__addi(__s0, __s0, rv_lo(fn->stack_size + 8)));
            emit(__sub(__sp, __sp, __s0));
        } break;
        case OP_block_start:
            block_lv++;
            break;
        case OP_block_end: {
            --block_lv;
            if (block_lv != 0)
                break;
            if (!strcmp(fn->return_def.type_name, "void")) {
                emit(__lui(__s0, rv_hi(fn->stack_size + 8)));
                emit(__addi(__s0, __s0, rv_lo(fn->stack_size + 8)));
                emit(__add(__sp, __sp, __s0));
                emit(__lw(__s0, __sp, -4));
                emit(__lw(__ra, __sp, -8));
                emit(__jalr(__zero, __ra, 0));
            }
        } break;
        case OP_load_constant: {
            if (ph2_ir->src0 > -2048 && ph2_ir->src0 < 2047)
                emit(__addi(ph2_ir->dest + 10, __zero, ph2_ir->src0));
            else {
                emit(__lui(ph2_ir->dest + 10, rv_hi(ph2_ir->src0)));
                emit(__addi(ph2_ir->dest + 10, ph2_ir->dest + 10,
                            rv_lo(ph2_ir->src0)));
            }
        } break;
        case OP_load_data_address: {
            emit(
                __lui(ph2_ir->dest + 10, rv_hi(ph2_ir->src0 + elf_data_start)));
            emit(__addi(ph2_ir->dest + 10, ph2_ir->dest + 10,
                        rv_lo(ph2_ir->src0 + elf_data_start)));
        } break;
        case OP_address_of: {
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src0)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src0)));
                emit(__add(ph2_ir->dest + 10, __t6, __sp));
            } else
                emit(__addi(ph2_ir->dest + 10, __sp, ph2_ir->src0));
        } break;
        case OP_assign: {
            if (ph2_ir->dest != ph2_ir->src0)
                emit(__addi(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
        } break;
        case OP_branch: {
            /* the absolute address is the offset + the beginning of the
             * instrution (0x10054)*/
            ofs = find_label_offset(ph2_ir->false_label);
            emit(__lui(__t6, rv_hi(ofs + 65620)));
            emit(__addi(__t6, __t6, rv_lo(ofs + 65620)));
            emit(__bne(ph2_ir->src0 + 10, __zero, 8));
            emit(__jalr(__zero, __t6, 0));

            ofs = find_label_offset(ph2_ir->true_label);
            emit(__jal(__zero, ofs - elf_code_idx));
        } break;
        case OP_jump: {
            if (!strcmp(ph2_ir->func_name, "main")) {
                emit(__lui(__t6, rv_hi(FUNCS[0].stack_size + 4)));
                emit(__addi(__t6, __t6, rv_lo(FUNCS[0].stack_size + 4)));
                emit(__add(__t6, __sp, __t6));
                emit(__lw(__a0, __t6, 0));
                emit(__addi(__a1, __t6, 4));
            }

            ofs = find_label_offset(ph2_ir->func_name);
            emit(__jal(__zero, ofs - elf_code_idx));
        } break;
        case OP_load: {
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src0)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src0)));
                emit(__add(__t6, __t6, __sp));
                emit(__lw(ph2_ir->dest + 10, __t6, 0));
            } else
                emit(__lw(ph2_ir->dest + 10, __sp, ph2_ir->src0));
        } break;
        case OP_store: {
            if (ph2_ir->src1 < -2048 || ph2_ir->src1 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src1)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src1)));
                emit(__add(__t6, __t6, __sp));
                emit(__sw(ph2_ir->src0 + 10, __t6, 0));
            } else
                emit(__sw(ph2_ir->src0 + 10, __sp, ph2_ir->src1));
        } break;
        case OP_global_load: {
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src0)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src0)));
                emit(__add(__t6, __t6, __gp));
                emit(__lw(ph2_ir->dest + 10, __t6, 0));
            } else
                emit(__lw(ph2_ir->dest + 10, __gp, ph2_ir->src0));
        } break;
        case OP_global_store: {
            if (ph2_ir->src1 < -2048 || ph2_ir->src1 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src1)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src1)));
                emit(__add(__t6, __t6, __gp));
                emit(__sw(ph2_ir->src0 + 10, __t6, 0));
            } else
                emit(__sw(ph2_ir->src0 + 10, __gp, ph2_ir->src1));
        } break;
        case OP_global_addr_of: {
            if (ph2_ir->src0 < -2048 || ph2_ir->src0 > 2047) {
                emit(__lui(__t6, rv_hi(ph2_ir->src0)));
                emit(__addi(__t6, __t6, rv_lo(ph2_ir->src0)));
                emit(__add(ph2_ir->dest + 10, __t6, __gp));
            } else
                emit(__addi(ph2_ir->dest + 10, __gp, ph2_ir->src0));
        } break;
        case OP_read: {
            if (ph2_ir->src1 == 1)
                emit(__lb(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
            else if (ph2_ir->src1 == 4)
                emit(__lw(ph2_ir->dest + 10, ph2_ir->src0 + 10, 0));
            else
                abort();
        } break;
        case OP_write:
            if (ph2_ir->dest == 1)
                emit(__sb(ph2_ir->src0 + 10, ph2_ir->src1 + 10, 0));
            else if (ph2_ir->dest == 4)
                emit(__sw(ph2_ir->src0 + 10, ph2_ir->src1 + 10, 0));
            else
                abort();
            break;
        case OP_func_addr:
            /* the absolute address is the offset + the beginning of the
             * instrution (0x10054)*/
            ofs = find_label_offset(ph2_ir->func_name);
            emit(__lui(__t6, rv_hi(ofs + 65620)));
            emit(__addi(__t6, __t6, rv_lo(ofs + 65620)));
            emit(__sw(__t6, ph2_ir->src0 + 10, 0));
            break;
        case OP_indirect:
            emit(__jalr(__ra, __t6, 0));
            break;
        case OP_call:
            ofs = find_label_offset(ph2_ir->func_name);
            emit(__jal(__ra, ofs - elf_code_idx));
            break;
        case OP_return: {
            if (ph2_ir->src0 == -1)
                emit(__addi(__zero, __zero, 0));
            else
                emit(__addi(__a0, ph2_ir->src0 + 10, 0));
            emit(__lui(__s0, rv_hi(fn->stack_size + 8)));
            emit(__addi(__s0, __s0, rv_lo(fn->stack_size + 8)));
            emit(__add(__sp, __sp, __s0));
            emit(__lw(__s0, __sp, -4));
            emit(__lw(__ra, __sp, -8));
            emit(__jalr(__zero, __ra, 0));
        } break;
        case OP_negate:
            emit(__sub(ph2_ir->dest + 10, __zero, ph2_ir->src0 + 10));
            break;
        case OP_add: {
            emit(
                __add(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_sub: {
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_mul: {
            emit(
                __mul(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_div: {
            emit(
                __div(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_mod: {
            emit(
                __mod(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_eq: {
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
        } break;
        case OP_neq: {
            emit(
                __sub(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
        } break;
        case OP_gt: {
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src1 + 10, ph2_ir->src0 + 10));
        } break;
        case OP_lt: {
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_geq: {
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
        } break;
        case OP_leq: {
            emit(
                __slt(ph2_ir->dest + 10, ph2_ir->src1 + 10, ph2_ir->src0 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
        } break;
        case OP_bit_and: {
            emit(
                __and(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_bit_or: {
            emit(__or(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_bit_xor: {
            emit(
                __xor(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
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
        case OP_log_or: {
            emit(__or(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->dest + 10));
        } break;
        case OP_log_not: {
            emit(__sltu(ph2_ir->dest + 10, __zero, ph2_ir->src0 + 10));
            emit(__xori(ph2_ir->dest + 10, ph2_ir->dest + 10, 1));
        } break;
        case OP_rshift: {
            emit(
                __sra(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_lshift: {
            emit(
                __sll(ph2_ir->dest + 10, ph2_ir->src0 + 10, ph2_ir->src1 + 10));
        } break;
        case OP_label:
            break;
        default:
            abort();
            break;
        }
    }
}

/* Not support "%c" in printf() yet */
void dump_ph2_ir()
{
    ph2_ir_t *ph2_ir;
    int i;

    if (dump_ir == 0)
        return;

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
        case OP_global_addr_of:
            printf("\t%%a%c = %%gp + %d", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_load_data_address:
            printf("\t%%a%c = .data(%d)", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_assign:
            printf("\t%%a%c = %%a%c", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_branch:
            printf("\tbr %%a%c, %s, %s", ph2_ir->src0 + 48, ph2_ir->true_label,
                   ph2_ir->false_label);
            break;
        case OP_label:
            printf("%s:", ph2_ir->func_name);
            break;
        case OP_jump:
            printf("\tj %s", ph2_ir->func_name);
            break;
        case OP_load:
            printf("\tload %%a%c, %d(sp)", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_store:
            printf("\tstore %%a%c, %d(sp)", ph2_ir->src0 + 48, ph2_ir->src1);
            break;
        case OP_global_load:
            printf("\tload %%a%c, %d(gp)", ph2_ir->dest + 48, ph2_ir->src0);
            break;
        case OP_global_store:
            printf("\tstore %%a%c, %d(gp)", ph2_ir->src0 + 48, ph2_ir->src1);
            break;
        case OP_read:
            printf("\t%%a%c = (%%a%c)", ph2_ir->dest + 48, ph2_ir->src0 + 48);
            break;
        case OP_write:
            printf("\t(%%a%c) = %%a%c", ph2_ir->src1 + 48, ph2_ir->src0 + 48);
            break;
        case OP_func_addr:
            printf("\t(%%a%c) = @%s", ph2_ir->src0 + 48, ph2_ir->func_name);
            break;
        case OP_indirect:
            printf("\tindirect call @(%%t6)");
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
        case OP_mod:
            printf("\t%%a%c = mod %%a%c, %%a%c", ph2_ir->dest + 48,
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
            printf("\t%%a%c = lt %%a%c, %%a%c", ph2_ir->dest + 48,
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
