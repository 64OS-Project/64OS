#ifndef AML_PARSER_H
#define AML_PARSER_H

#include <kernel/types.h>
#include <acpi.h>

// ==================== AML ОПКОДЫ ====================
#define AML_ZERO_OP                  0x00
#define AML_ONE_OP                   0x01
#define AML_ALIAS_OP                 0x06
#define AML_NAME_OP                  0x08
#define AML_BYTE_PREFIX              0x0A
#define AML_WORD_PREFIX              0x0B
#define AML_DWORD_PREFIX             0x0C
#define AML_STRING_PREFIX            0x0D
#define AML_QWORD_PREFIX             0x0E
#define AML_SCOPE_OP                 0x10
#define AML_BUFFER_OP                0x11
#define AML_PACKAGE_OP               0x12
#define AML_VAR_PACKAGE_OP           0x13
#define AML_METHOD_OP                0x14
#define AML_EXTERNAL_OP              0x15
#define AML_DUAL_NAME_PREFIX         0x2E
#define AML_MULTI_NAME_PREFIX        0x2F
#define AML_EXT_OP_PREFIX            0x5B
#define AML_ROOT_CHAR                0x5C
#define AML_PARENT_PREFIX            0x5E
#define AML_LOCAL0                   0x60
#define AML_LOCAL1                   0x61
#define AML_LOCAL2                   0x62
#define AML_LOCAL3                   0x63
#define AML_LOCAL4                   0x64
#define AML_LOCAL5                   0x65
#define AML_LOCAL6                   0x66
#define AML_LOCAL7                   0x67
#define AML_ARG0                     0x68
#define AML_ARG1                     0x69
#define AML_ARG2                     0x6A
#define AML_ARG3                     0x6B
#define AML_ARG4                     0x6C
#define AML_ARG5                     0x6D
#define AML_ARG6                     0x6E
#define AML_STORE_OP                 0x70
#define AML_REF_OF_OP                0x71
#define AML_ADD_OP                   0x72
#define AML_CONCAT_OP                0x73
#define AML_SUBTRACT_OP              0x74
#define AML_INCREMENT_OP             0x75
#define AML_DECREMENT_OP             0x76
#define AML_MULTIPLY_OP              0x77
#define AML_DIVIDE_OP                0x78
#define AML_SHIFT_LEFT_OP            0x79
#define AML_SHIFT_RIGHT_OP           0x7A
#define AML_AND_OP                   0x7B
#define AML_NAND_OP                  0x7C
#define AML_OR_OP                    0x7D
#define AML_NOR_OP                   0x7E
#define AML_XOR_OP                   0x7F
#define AML_NOT_OP                   0x80
#define AML_FIND_SET_LEFT_BIT_OP     0x81
#define AML_FIND_SET_RIGHT_BIT_OP    0x82
#define AML_DEREF_OF_OP              0x83
#define AML_CONCAT_RES_OP            0x84
#define AML_MOD_OP                   0x85
#define AML_NOTIFY_OP                0x86
#define AML_SIZE_OF_OP               0x87
#define AML_INDEX_OP                 0x88
#define AML_MATCH_OP                 0x89
#define AML_CREATE_DWORD_FIELD_OP    0x8A
#define AML_CREATE_WORD_FIELD_OP     0x8B
#define AML_CREATE_BYTE_FIELD_OP     0x8C
#define AML_CREATE_BIT_FIELD_OP      0x8D
#define AML_OBJECT_TYPE_OP           0x8E
#define AML_CREATE_QWORD_FIELD_OP    0x8F
#define AML_LAND_OP                  0x90
#define AML_LOR_OP                   0x91
#define AML_LNOT_OP                  0x92
#define AML_LEQUAL_OP                0x93
#define AML_LGREATER_OP              0x94
#define AML_LLESS_OP                 0x95
#define AML_TO_BUFFER_OP             0x96
#define AML_TO_DECIMAL_STRING_OP     0x97
#define AML_TO_HEX_STRING_OP         0x98
#define AML_TO_INTEGER_OP            0x99
#define AML_TO_STRING_OP             0x9A
#define AML_COPY_OBJECT_OP           0x9B
#define AML_MID_OP                   0x9C
#define AML_CONTINUE_OP              0x9D
#define AML_IF_OP                    0xA0
#define AML_ELSE_OP                  0xA1
#define AML_WHILE_OP                 0xA2
#define AML_NOOP_OP                  0xA3
#define AML_RETURN_OP                0xA4
#define AML_BREAK_OP                 0xA5
#define AML_BREAK_POINT_OP           0xCC
#define AML_ONES_OP                  0xFF

// ==================== EXTENDED ОПКОДЫ ====================
#define AML_EXT_MUTEX_OP             0x01
#define AML_EXT_EVENT_OP             0x02
#define AML_EXT_COND_REF_OF_OP       0x12
#define AML_EXT_CREATE_FIELD_OP      0x13
#define AML_EXT_LOAD_TABLE_OP        0x1F
#define AML_EXT_LOAD_OP              0x20
#define AML_EXT_STALL_OP             0x21
#define AML_EXT_SLEEP_OP             0x22
#define AML_EXT_ACQUIRE_OP           0x23
#define AML_EXT_SIGNAL_OP            0x24
#define AML_EXT_WAIT_OP              0x25
#define AML_EXT_RESET_OP             0x26
#define AML_EXT_RELEASE_OP           0x27
#define AML_EXT_FROM_BCD_OP          0x28
#define AML_EXT_TO_BCD_OP            0x29
#define AML_EXT_UNLOAD_OP            0x2A
#define AML_EXT_REVISION_OP          0x30
#define AML_EXT_DEBUG_OP             0x31
#define AML_EXT_FATAL_OP             0x32
#define AML_EXT_TIMER_OP             0x33
#define AML_EXT_REGION_OP            0x80
#define AML_EXT_FIELD_OP             0x81
#define AML_EXT_DEVICE_OP            0x82
#define AML_EXT_PROCESSOR_OP         0x83
#define AML_EXT_POWER_RES_OP         0x84
#define AML_EXT_THERMAL_ZONE_OP      0x85
#define AML_EXT_INDEX_FIELD_OP       0x86
#define AML_EXT_BANK_FIELD_OP        0x87
#define AML_EXT_DATA_REGION_OP       0x88

// ==================== ТИПЫ РЕГИОНОВ ====================
#define AML_REGION_SYSTEM_MEMORY   0x00
#define AML_REGION_SYSTEM_IO       0x01
#define AML_REGION_PCI_CONFIG      0x02
#define AML_REGION_EMBEDDED_CTRL   0x03
#define AML_REGION_SMBUS           0x04
#define AML_REGION_SYSTEM_CMOS     0x05
#define AML_REGION_PCI_BAR_TARGET  0x06
#define AML_REGION_IPMI            0x07
#define AML_REGION_GENERAL_PURPOSE 0x08
#define AML_REGION_GENERIC_SERIAL  0x09
#define AML_REGION_PCC             0x0A

// ==================== ТИПЫ ОБЪЕКТОВ ====================
typedef enum {
    AML_OBJECT_TYPE_UNKNOWN = 0,
    AML_OBJECT_TYPE_INTEGER,
    AML_OBJECT_TYPE_STRING,
    AML_OBJECT_TYPE_BUFFER,
    AML_OBJECT_TYPE_PACKAGE,
    AML_OBJECT_TYPE_FIELD_UNIT,
    AML_OBJECT_TYPE_DEVICE,
    AML_OBJECT_TYPE_EVENT,
    AML_OBJECT_TYPE_METHOD,
    AML_OBJECT_TYPE_MUTEX,
    AML_OBJECT_TYPE_REGION,
    AML_OBJECT_TYPE_POWER_RESOURCE,
    AML_OBJECT_TYPE_PROCESSOR,
    AML_OBJECT_TYPE_THERMAL_ZONE,
    AML_OBJECT_TYPE_BUFFER_FIELD,
    AML_OBJECT_TYPE_DDB_HANDLE,
    AML_OBJECT_TYPE_DEBUG_OBJECT,
    AML_OBJECT_TYPE_REFERENCE,
} aml_object_type_t;

// ==================== СТРУКТУРЫ ====================

typedef struct aml_object aml_object_t;
typedef struct aml_namespace_node aml_namespace_node_t;
typedef struct aml_method aml_method_t;
typedef struct aml_region aml_region_t;
typedef struct aml_context aml_context_t;

struct aml_object {
    aml_object_type_t type;
    union {
        u64 integer;
        char *string;
        struct {
            u8 *data;
            u32 length;
        } buffer;
        struct {
            aml_object_t **elements;
            u32 count;
        } package;
        struct {
            aml_namespace_node_t *node;
        } reference;
    } value;
    aml_object_t *next;
};

struct aml_namespace_node {
    char name[256];
    char simple_name[5];
    aml_object_type_t type;
    
    union {
        struct {
            u8 *code;
            sz code_len;
            u8 arg_count;
            bool serialized;
            u8 sync_level;
        } method;
        struct {
            u8 space;
            u64 base;
            u64 length;
        } region;
        struct {
            u32 hid;
            u32 uid;
            bool has_power_button;
        } device;
        struct {
            aml_region_t *region;
            u64 offset;
            u64 length;
        } field;
    } data;
    
    aml_namespace_node_t *parent;
    aml_namespace_node_t *children;
    aml_namespace_node_t *next;
};

struct aml_region {
    char name[256];
    u8 space;
    u64 base;
    u64 length;
    aml_namespace_node_t *node;
};

struct aml_method {
    char name[256];
    u8 *code;
    sz code_len;
    sz code_start;
    u8 arg_count;
    bool serialized;
    u8 sync_level;
};

typedef struct {
    u8 gpe_number;
    u16 gpe_bit;
    u8 gpe_type;
    u32 gpe_blk_addr;
    char method_name[32];
    bool found;
    bool is_power_button;
} aml_gpe_info_t;

typedef struct {
    bool found;
    bool is_legacy;
    aml_gpe_info_t gpe;
    bool has_pwrb_device;
    char pwrb_path[128];
    void *pwrb_node;
    u16 pm1_evt_blk;
    u8 pm1_pwr_btn_bit;
} power_button_info_t;

typedef struct {
    u64 locals[8];
    u64 args[8];
    int sp;
    aml_object_t *stack[64];
} aml_exec_stack_t;

struct aml_context {
    u8 *dsdt_data;
    sz dsdt_len;
    u8 *ssdt_data[16];
    sz ssdt_len[16];
    int ssdt_count;
    
    aml_namespace_node_t *root_node;
    aml_namespace_node_t *current_node;
    char current_scope[256];
    
    aml_method_t methods[256];
    int method_count;
    
    aml_region_t regions[64];
    int region_count;
    
    power_button_info_t pwr_info;
    aml_gpe_info_t gpe_info[32];
    int gpe_count;
    
    aml_exec_stack_t exec;
    
    bool initialized;
    bool debug_mode;
    
    acpi_t *acpi_ctx;
};

// ==================== ОСНОВНЫЕ ФУНКЦИИ ====================

bool aml_init_context(aml_context_t *ctx, acpi_t *acpi);
bool aml_find_power_button(aml_context_t *ctx);
bool aml_get_power_button_info(power_button_info_t *info);
void aml_parse_all_tables(aml_context_t *ctx);
int aml_execute_gpe(aml_context_t *ctx, u8 gpe_number);
int aml_call_method(aml_context_t *ctx, const char *name, aml_object_t **args, u32 arg_count);
void aml_dump_namespace(aml_context_t *ctx);
void aml_set_debug(bool enable);
aml_context_t *aml_get_context(void);

#endif