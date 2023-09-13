/* Global objects */

block_t *BLOCKS;
int blocks_idx = 0;

func_t *FUNCS;
int funcs_idx = 1;

type_t *TYPES;
int types_idx = 0;

ph1_ir_t *GLOBAL_IR;
int global_ir_idx = 0;

ph1_ir_t *PH1_IR;
int ph1_ir_idx = 0;

ph2_ir_t *PH2_IR;
int ph2_ir_idx = 0;

label_lut_t *LABEL_LUT;
int label_lut_idx = 0;

regfile_t REG[REG_CNT];

alias_t *ALIASES;
int aliases_idx = 0;

constant_t *CONSTANTS;
int constants_idx = 0;

char *SOURCE;
int source_idx = 0;

/* ELF sections */

char *elf_code;
int elf_code_idx = 0;
char *elf_data;
int elf_data_idx = 0;
char *elf_header;
int elf_header_idx = 0;
int elf_header_len; /* ELF fixed: 0x34 + 1 * 0x20 */
int elf_code_start;
char *elf_symtab;
char *elf_strtab;
char *elf_section;

/* options */

int dump_ir = 0;

type_t *find_type(char *type_name)
{
    int i;
    for (i = 0; i < types_idx; i++)
        if (!strcmp(TYPES[i].type_name, type_name))
            return &TYPES[i];
    return NULL;
}

ph1_ir_t *add_global_ir(opcode_t op)
{
    ph1_ir_t *ir = &GLOBAL_IR[global_ir_idx++];
    ir->op = op;
    return ir;
}

ph1_ir_t *add_ph1_ir(opcode_t op)
{
    ph1_ir_t *ph1_ir = &PH1_IR[ph1_ir_idx++];
    ph1_ir->op = op;
    return ph1_ir;
}

ph2_ir_t *add_ph2_ir(opcode_t op)
{
    ph2_ir_t *ph2_ir = &PH2_IR[ph2_ir_idx++];
    ph2_ir->op = op;
    return ph2_ir;
}

void set_var_liveout(var_t *var, int end)
{
    if (var->eol >= end)
        return;
    var->eol = end;
}

void add_label(char *name, int offset)
{
    label_lut_t *lut = &LABEL_LUT[label_lut_idx++];
    strcpy(lut->name, name);
    lut->offset = offset;
}

int find_label_offset(char name[])
{
    int i;
    for (i = 0; i < label_lut_idx; i++)
        if (!strcmp(LABEL_LUT[i].name, name))
            return LABEL_LUT[i].offset;
    return -1;
}

block_t *add_block(block_t *parent, func_t *func)
{
    block_t *blk = &BLOCKS[blocks_idx];
    blk->index = blocks_idx++;
    blk->parent = parent;
    blk->func = func;
    blk->next_local = 0;
    return blk;
}

void add_alias(char *alias, char *value)
{
    alias_t *al = &ALIASES[aliases_idx++];
    strcpy(al->alias, alias);
    strcpy(al->value, value);
}

char *find_alias(char alias[])
{
    int i;
    for (i = 0; i < aliases_idx; i++)
        if (!strcmp(alias, ALIASES[i].alias))
            return ALIASES[i].value;
    return NULL;
}

func_t *add_func(char *name)
{
    func_t *fn;
    int i;

    /* return existing if found */
    for (i = 0; i < funcs_idx; i++)
        if (!strcmp(FUNCS[i].return_def.var_name, name))
            return &FUNCS[i];

    fn = &FUNCS[funcs_idx++];
    fn->stack_size = 4;
    strcpy(fn->return_def.var_name, name);
    return fn;
}

type_t *add_type()
{
    return &TYPES[types_idx++];
}

type_t *add_named_type(char *name)
{
    type_t *type = add_type();
    strcpy(type->type_name, name);
    return type;
}

void add_constant(char alias[], int value)
{
    constant_t *constant = &CONSTANTS[constants_idx++];
    strcpy(constant->alias, alias);
    constant->value = value;
}

constant_t *find_constant(char alias[])
{
    int i;
    for (i = 0; i < constants_idx; i++)
        if (!strcmp(CONSTANTS[i].alias, alias))
            return &CONSTANTS[i];
    return NULL;
}

func_t *find_func(char func_name[])
{
    int i;
    for (i = 0; i < funcs_idx; i++)
        if (!strcmp(FUNCS[i].return_def.var_name, func_name))
            return &FUNCS[i];
    return NULL;
}

var_t *find_member(char token[], type_t *type)
{
    int i;
    for (i = 0; i < type->num_fields; i++)
        if (!strcmp(type->fields[i].var_name, token))
            return &type->fields[i];
    return NULL;
}

var_t *find_local_var(char *token, block_t *block)
{
    int i;
    func_t *fn = block->func;

    for (; block; block = block->parent) {
        for (i = 0; i < block->next_local; i++)
            if (!strcmp(block->locals[i].var_name, token))
                return &block->locals[i];
    }

    if (fn) {
        for (i = 0; i < fn->num_params; i++)
            if (!strcmp(fn->param_defs[i].var_name, token))
                return &fn->param_defs[i];
    }
    return NULL;
}

var_t *find_global_var(char *token)
{
    int i;
    block_t *block = &BLOCKS[0];

    for (i = 0; i < block->next_local; i++)
        if (!strcmp(block->locals[i].var_name, token))
            return &block->locals[i];
    return NULL;
}

var_t *find_var(char *token, block_t *parent)
{
    var_t *var = find_local_var(token, parent);
    if (!var)
        var = find_global_var(token);
    return var;
}

int size_var(var_t *var)
{
    int s = 0;

    if (var->is_ptr > 0 || var->is_func > 0) {
        s += 4;
    } else {
        type_t *td = find_type(var->type_name);
        int bs = td->size;
        if (var->array_size > 0) {
            int j = 0;
            for (; j < var->array_size; j++)
                s += bs;
        } else
            s += bs;
    }
    return s;
}

/* This routine is required because the global variable initializations are
 * not supported now.
 */
void global_init()
{
    elf_header_len = 0x54;
    elf_code_start = ELF_START + elf_header_len;

    BLOCKS = malloc(MAX_BLOCKS * sizeof(block_t));
    FUNCS = malloc(MAX_FUNCS * sizeof(func_t));
    TYPES = malloc(MAX_TYPES * sizeof(type_t));
    GLOBAL_IR = malloc(MAX_GLOBAL_IR * sizeof(ph1_ir_t));
    PH1_IR = malloc(MAX_IR_INSTR * sizeof(ph1_ir_t));
    PH2_IR = malloc(MAX_IR_INSTR * sizeof(ph2_ir_t));
    LABEL_LUT = malloc(MAX_LABEL * sizeof(label_lut_t));
    SOURCE = malloc(MAX_SOURCE);
    ALIASES = malloc(MAX_ALIASES * sizeof(alias_t));
    CONSTANTS = malloc(MAX_CONSTANTS * sizeof(constant_t));

    elf_code = malloc(MAX_CODE);
    elf_data = malloc(MAX_DATA);
    elf_header = malloc(MAX_HEADER);
    elf_symtab = malloc(MAX_SYMTAB);
    elf_strtab = malloc(MAX_STRTAB);
    elf_section = malloc(MAX_SECTION);

    FUNCS[0].stack_size = 4;
}

void error(char *msg)
{
    /* TODO: figure out the corresponding C source and report line number */
    printf("Error %s at source location %d\n", msg, source_idx);
    abort();
}

void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent; i++)
        printf("\t");
}

void dump_ph1_ir()
{
    int indent = 0;
    ph1_ir_t *ph1_ir;
    func_t *fn;
    int i, j, k;

    if (dump_ir == 0)
        return;

    for (i = 0; i < ph1_ir_idx; i++) {
        ph1_ir = &PH1_IR[i];

        switch (ph1_ir->op) {
        case OP_define:
            fn = find_func(ph1_ir->func_name);
            printf("def %s", fn->return_def.type_name);

            for (j = 0; j < fn->return_def.is_ptr; j++)
                printf("*");
            printf(" @%s(", ph1_ir->func_name);

            for (j = 0; j < fn->num_params; j++) {
                if (j != 0)
                    printf(", ");
                printf("%s", fn->param_defs[j].type_name);

                for (k = 0; k < fn->param_defs[j].is_ptr; k++)
                    printf("*");
                printf(" %%%s", fn->param_defs[j].var_name);
            }
            printf(")");
            break;
        case OP_block_start:
            print_indent(indent);
            printf("{");
            indent++;
            break;
        case OP_block_end:
            indent--;
            print_indent(indent);
            printf("}");
            break;
        case OP_allocat:
            print_indent(indent);
            printf("allocat %s", ph1_ir->src0->type_name);
            for (j = 0; j < ph1_ir->src0->is_ptr; j++)
                printf("*");
            printf(" %%%s", ph1_ir->src0->var_name);

            if (ph1_ir->src0->array_size > 0)
                printf("[%d]", ph1_ir->src0->array_size);
            break;
        case OP_label:
            print_indent(0);
            printf("%s", ph1_ir->src0->var_name);
            break;
        case OP_branch:
            print_indent(indent);
            printf("br %%%s, %s, %s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_jump:
            print_indent(indent);
            printf("j %s", ph1_ir->dest->var_name);
            break;
        case OP_load_constant:
            print_indent(indent);
            printf("const %%%s, $%d", ph1_ir->dest->var_name,
                   ph1_ir->dest->init_val);
            break;
        case OP_assign:
            print_indent(indent);
            printf("%%%s = %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name);
            break;
        case OP_push:
            print_indent(indent);
            printf("push %%%s", ph1_ir->src0->var_name);
            break;
        case OP_call:
            print_indent(indent);
            printf("call @%s, %d", ph1_ir->func_name, ph1_ir->param_num);
            break;
        case OP_func_ret:
            print_indent(indent);
            printf("retval %%%s", ph1_ir->dest->var_name);
            break;
        case OP_return:
            print_indent(indent);
            if (ph1_ir->src0)
                printf("ret %%%s", ph1_ir->src0->var_name);
            else
                printf("ret");
            break;
        case OP_load_data_address:
            print_indent(indent);
            /* offset from .data section */
            printf("%%%s = .data (%d)", ph1_ir->dest->var_name,
                   ph1_ir->dest->init_val);
            break;
        case OP_address_of:
            print_indent(indent);
            printf("%%%s = &(%%%s)", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name);
            break;
        case OP_read:
            print_indent(indent);
            printf("%%%s = (%%%s), %d", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->size);
            break;
        case OP_write:
            print_indent(indent);
            if (ph1_ir->src0->is_func)
                printf("(%%%s) = @%s", ph1_ir->dest->var_name,
                       ph1_ir->src0->var_name);
            else
                printf("(%%%s) = %%%s, %d", ph1_ir->dest->var_name,
                       ph1_ir->src0->var_name, ph1_ir->size);
            break;
        case OP_indirect:
            print_indent(indent);
            printf("indirect call @(%%%s)", ph1_ir->src0->var_name);
            break;
        case OP_negate:
            print_indent(indent);
            printf("neg %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name);
            break;
        case OP_add:
            print_indent(indent);
            printf("%%%s = add %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_sub:
            print_indent(indent);
            printf("%%%s = sub %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_mul:
            print_indent(indent);
            printf("%%%s = mul %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_div:
            print_indent(indent);
            printf("%%%s = div %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_mod:
            print_indent(indent);
            printf("%%%s = mod %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_eq:
            print_indent(indent);
            printf("%%%s = eq %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_neq:
            print_indent(indent);
            printf("%%%s = neq %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_gt:
            print_indent(indent);
            printf("%%%s = gt %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_lt:
            print_indent(indent);
            printf("%%%s = lt %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_geq:
            print_indent(indent);
            printf("%%%s = geq %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_leq:
            print_indent(indent);
            printf("%%%s = leq %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_bit_and:
            print_indent(indent);
            printf("%%%s = and %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_bit_or:
            print_indent(indent);
            printf("%%%s = or %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_bit_not:
            print_indent(indent);
            printf("%%%s = not %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name);
            break;
        case OP_bit_xor:
            print_indent(indent);
            printf("%%%s = xor %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_log_and:
            print_indent(indent);
            printf("%%%s = and %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_log_or:
            print_indent(indent);
            printf("%%%s = or %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_log_not:
            print_indent(indent);
            printf("%%%s = not %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name);
            break;
        case OP_rshift:
            print_indent(indent);
            printf("%%%s = rshift %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        case OP_lshift:
            print_indent(indent);
            printf("%%%s = lshift %%%s, %%%s", ph1_ir->dest->var_name,
                   ph1_ir->src0->var_name, ph1_ir->src1->var_name);
            break;
        default:
            break;
        }
        printf("\n");
    }
    printf("===\n");
}
