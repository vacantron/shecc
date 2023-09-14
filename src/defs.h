/* definitions */

/* Limitations */
#define MAX_TOKEN_LEN 256
#define MAX_ID_LEN 64
#define MAX_LINE_LEN 256
#define MAX_VAR_LEN 32
#define MAX_TYPE_LEN 32
#define MAX_PARAMS 8
#define MAX_LOCALS 960
#define MAX_FIELDS 32
#define MAX_FUNCS 256
#define MAX_BLOCKS 625
#define MAX_TYPES 64
#define MAX_IR_INSTR 32768
#define MAX_GLOBAL_IR 256
#define MAX_LABEL 4096
#define MAX_SOURCE 262144
#define MAX_CODE 262144
#define MAX_DATA 262144
#define MAX_SYMTAB 65536
#define MAX_STRTAB 65536
#define MAX_HEADER 1024
#define MAX_SECTION 1024
#define MAX_ALIASES 1024
#define MAX_CONSTANTS 1024
#define MAX_CASES 128
#define MAX_NESTING 128

#define ELF_START 0x10000
#define PTR_SIZE 4

#define REG_CNT 8

/* builtin types */
typedef enum { TYPE_void = 0, TYPE_int, TYPE_char, TYPE_struct } base_type_t;

/* IR opcode */
typedef enum {
    /* generic: intermediate use in front-end. No code generation */
    OP_generic,

    OP_define,
    OP_allocat,
    OP_assign,
    OP_store,
    OP_load,
    OP_global_store,
    OP_global_load,
    OP_global_addr_of,
    OP_branch,
    OP_func_ret,
    OP_func_addr,

    /* calling convention */
    OP_func_extry, /* function entry point */
    OP_exit,       /* program exit routine */
    OP_call,       /* function call */
    OP_indirect,   /* indirect call with function pointer */
    OP_func_exit,  /* function exit code */
    OP_return,     /* jump to function exit */

    OP_load_constant,     /* load constant */
    OP_load_data_address, /* lookup address of a constant in data section */

    /* stack operations */
    OP_push, /* push onto stack */
    OP_pop,  /* pop from stack */

    /* control flow */
    OP_jump,        /* unconditional jump */
    OP_label,       /* note label */
    OP_jz,          /* jump if false */
    OP_jnz,         /* jump if true */
    OP_block_start, /* code block start */
    OP_block_end,   /* code block end */

    /* memory address operations */
    OP_address_of, /* lookup variable's address */
    OP_read,       /* read from memory address */
    OP_write,      /* write to memory address */

    /* arithmetic operators */
    OP_add,
    OP_sub,
    OP_mul,
    OP_div,     /* signed division */
    OP_mod,     /* modulo */
    OP_ternary, /* ? : */
    OP_lshift,
    OP_rshift,
    OP_log_and,
    OP_log_or,
    OP_log_not,
    OP_eq,  /* equal */
    OP_neq, /* not equal */
    OP_lt,  /* less than */
    OP_leq, /* less than or equal */
    OP_gt,  /* greater than */
    OP_geq, /* greater than or equal */
    OP_bit_or,
    OP_bit_and,
    OP_bit_xor,
    OP_bit_not,
    OP_negate,

    /* platform-specific */
    OP_syscall,
    OP_start
} opcode_t;

/* variable definition */
typedef struct {
    char type_name[MAX_TYPE_LEN];
    char var_name[MAX_VAR_LEN];
    int is_ptr;
    int is_func;
    int array_size;
    int offset;   /* offset from stack or frame, index 0 is reserved */
    int init_val; /* for global initialization */

    int is_global;
    int eol; /* end-of-life */
    int in_loop;
} var_t;

/* function definition */
typedef struct {
    var_t return_def;
    var_t param_defs[MAX_PARAMS];
    int num_params;
    int va_args;
    int stack_size;
} func_t;

/* block definition */
typedef struct block_t {
    var_t locals[MAX_LOCALS];
    int next_local;
    struct block_t *parent;
    func_t *func;
    int locals_size;
    int index;
} block_t;

/* phase-1 IR definition */
typedef struct {
    opcode_t op;
    char func_name[32];
    int param_num;
    int size;
    var_t *dest;
    var_t *src0;
    var_t *src1;
} ph1_ir_t;

/* label lookup table*/
typedef struct {
    char name[32];
    int offset;
} label_lut_t;

typedef struct {
    var_t *var;
    int polluted;
} regfile_t;

/* phase-2 IR definition */
typedef struct {
    opcode_t op;
    int src0;
    int src1;
    int dest;
    char func_name[MAX_VAR_LEN];
    char true_label[MAX_VAR_LEN];
    char false_label[MAX_VAR_LEN];
} ph2_ir_t;

/* type definition */
typedef struct {
    char type_name[MAX_TYPE_LEN];
    base_type_t base_type;
    int size;
    var_t fields[MAX_FIELDS];
    int num_fields;
} type_t;

/* lvalue details */
typedef struct {
    int size;
    int is_ptr;
    int is_func;
    int is_reference;
    type_t *type;
} lvalue_t;

/* alias for #defines */
typedef struct {
    char alias[MAX_VAR_LEN];
    char value[MAX_VAR_LEN];
} alias_t;

/* constants for enums */
typedef struct {
    char alias[MAX_VAR_LEN];
    int value;
} constant_t;
