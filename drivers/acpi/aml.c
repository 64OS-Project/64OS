#include <aml.h>
#include <acpi.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <kernel/terminal.h>
#include <asm/io.h>
#include <kernel/timer.h>

static aml_context_t g_aml_ctx;
static bool debug_enabled = false;

// ==================== УТИЛИТЫ ====================

static void aml_stall(u32 microseconds) {
    timer_udelay(microseconds);
}

static void aml_msleep(u32 milliseconds) {
    timer_mdelay(milliseconds);
}

// ==================== ПАРСИНГ PKGLENGTH ====================

static sz aml_get_pkg_length(u8 *data, sz len, sz pos, sz *new_pos) {
    if (pos >= len) return 0;
    
    u8 lead = data[pos];
    *new_pos = pos + 1;
    
    if (!(lead & 0x80)) {
        return lead & 0x3F;
    }
    
    if ((lead & 0x40) && (lead & 0x1F) == 0) {
        if (pos + 1 >= len) return 0;
        *new_pos = pos + 2;
        return ((lead & 0x3F) << 8) | data[pos + 1];
    }
    
    if ((lead & 0x20) && (lead & 0x0F) == 0) {
        if (pos + 2 >= len) return 0;
        *new_pos = pos + 3;
        return ((lead & 0x0F) << 16) | (data[pos + 1] << 8) | data[pos + 2];
    }
    
    if ((lead & 0x10) && (lead & 0x07) == 0) {
        if (pos + 3 >= len) return 0;
        *new_pos = pos + 4;
        return ((lead & 0x07) << 24) | (data[pos + 1] << 16) | 
               (data[pos + 2] << 8) | data[pos + 3];
    }
    
    *new_pos = pos + 1;
    return 0;
}

static sz aml_skip_pkg_length(u8 *data, sz len, sz pos) {
    sz new_pos;
    aml_get_pkg_length(data, len, pos, &new_pos);
    return new_pos;
}

// ==================== ПАРСИНГ ЦЕЛЫХ ЧИСЕЛ ====================

static u64 aml_parse_integer(u8 *data, sz len, sz *pos) {
    if (*pos >= len) return 0;
    
    u8 op = data[*pos];
    
    switch (op) {
        case AML_ZERO_OP:
            (*pos)++;
            return 0;
        case AML_ONE_OP:
            (*pos)++;
            return 1;
        case AML_ONES_OP:
            (*pos)++;
            return ~0ULL;
        case AML_BYTE_PREFIX:
            if (*pos + 2 <= len) {
                u64 val = data[*pos + 1];
                *pos += 2;
                return val;
            }
            break;
        case AML_WORD_PREFIX:
            if (*pos + 3 <= len) {
                u64 val = data[*pos + 1] | (data[*pos + 2] << 8);
                *pos += 3;
                return val;
            }
            break;
        case AML_DWORD_PREFIX:
            if (*pos + 5 <= len) {
                u64 val = data[*pos + 1] | (data[*pos + 2] << 8) |
                          (data[*pos + 3] << 16) | (data[*pos + 4] << 24);
                *pos += 5;
                return val;
            }
            break;
        case AML_QWORD_PREFIX:
            if (*pos + 9 <= len) {
                u64 val = (u64)data[*pos + 1] |
                          ((u64)data[*pos + 2] << 8) |
                          ((u64)data[*pos + 3] << 16) |
                          ((u64)data[*pos + 4] << 24) |
                          ((u64)data[*pos + 5] << 32) |
                          ((u64)data[*pos + 6] << 40) |
                          ((u64)data[*pos + 7] << 48) |
                          ((u64)data[*pos + 8] << 56);
                *pos += 9;
                return val;
            }
            break;
    }
    
    return 0;
}

// ==================== ПАРСИНГ ИМЁН ====================

static int aml_parse_namestring(u8 *data, sz len, sz *pos, char *buf, sz buf_len) {
    if (!data || !pos || *pos >= len || !buf || buf_len == 0) return -1;
    
    int idx = 0;
    sz start = *pos;
    
    if (data[*pos] == '\\') {
        if (idx < buf_len - 1) buf[idx++] = '\\';
        (*pos)++;
    }
    
    while (*pos < len && data[*pos] == '^') {
        if (idx < buf_len - 1) buf[idx++] = '^';
        (*pos)++;
    }
    
    if (*pos >= len) goto error;
    
    u8 lead = data[*pos];
    
    if (lead == 0) {
        (*pos)++;
        buf[0] = '\0';
        return 0;
    }
    
    if (lead == AML_DUAL_NAME_PREFIX) {
        (*pos)++;
        for (int seg = 0; seg < 2; seg++) {
            if (*pos + 4 > len) goto error;
            if (seg > 0 && idx < buf_len - 1) buf[idx++] = '.';
            for (int i = 0; i < 4 && *pos < len; i++) {
                u8 c = data[*pos];
                if (c == 0) break;
                if (idx < buf_len - 1) buf[idx++] = c;
                (*pos)++;
            }
        }
        buf[idx] = '\0';
        return idx;
    }
    
    if (lead == AML_MULTI_NAME_PREFIX) {
        (*pos)++;
        if (*pos >= len) goto error;
        u8 count = data[*pos];
        (*pos)++;
        for (int seg = 0; seg < count; seg++) {
            if (*pos + 4 > len) goto error;
            if (seg > 0 && idx < buf_len - 1) buf[idx++] = '.';
            for (int i = 0; i < 4 && *pos < len; i++) {
                u8 c = data[*pos];
                if (c == 0) break;
                if (idx < buf_len - 1) buf[idx++] = c;
                (*pos)++;
            }
        }
        buf[idx] = '\0';
        return idx;
    }
    
    if (*pos + 4 <= len) {
        for (int i = 0; i < 4 && *pos < len; i++) {
            u8 c = data[*pos];
            if (c == 0) break;
            if (idx < buf_len - 1) buf[idx++] = c;
            (*pos)++;
        }
        buf[idx] = '\0';
        return idx;
    }
    
error:
    *pos = start;
    if (buf_len > 0) buf[0] = '\0';
    return -1;
}

// ==================== УПРАВЛЕНИЕ ОБЪЕКТАМИ ====================

static aml_object_t *aml_create_integer(u64 value) {
    aml_object_t *obj = (aml_object_t*)malloc(sizeof(aml_object_t));
    if (!obj) return NULL;
    obj->type = AML_OBJECT_TYPE_INTEGER;
    obj->value.integer = value;
    obj->next = NULL;
    return obj;
}

static aml_object_t *aml_create_string(const char *str) {
    aml_object_t *obj = (aml_object_t*)malloc(sizeof(aml_object_t));
    if (!obj) return NULL;
    obj->type = AML_OBJECT_TYPE_STRING;
    obj->value.string = (char*)malloc(strlen(str) + 1);
    if (!obj->value.string) { free(obj); return NULL; }
    strcpy(obj->value.string, str);
    obj->next = NULL;
    return obj;
}

static void aml_free_object(aml_object_t *obj) {
    if (!obj) return;
    switch (obj->type) {
        case AML_OBJECT_TYPE_STRING:
            if (obj->value.string) free(obj->value.string);
            break;
        case AML_OBJECT_TYPE_BUFFER:
            if (obj->value.buffer.data) free(obj->value.buffer.data);
            break;
        case AML_OBJECT_TYPE_PACKAGE:
            if (obj->value.package.elements) {
                for (u32 i = 0; i < obj->value.package.count; i++)
                    if (obj->value.package.elements[i]) aml_free_object(obj->value.package.elements[i]);
                free(obj->value.package.elements);
            }
            break;
        default: break;
    }
    free(obj);
}

// ==================== СТЕК ====================

static void aml_stack_push(aml_context_t *ctx, aml_object_t *obj) {
    if (ctx->exec.sp >= 63) {
        terminal_error_printf("[AML] Stack overflow\n");
        if (obj) aml_free_object(obj);
        return;
    }
    ctx->exec.stack[++ctx->exec.sp] = obj;
}

static aml_object_t *aml_stack_pop(aml_context_t *ctx) {
    if (ctx->exec.sp < 0) {
        terminal_error_printf("[AML] Stack underflow\n");
        return NULL;
    }
    return ctx->exec.stack[ctx->exec.sp--];
}

// ==================== ПРОСТРАНСТВО ИМЁН ====================

static aml_namespace_node_t *aml_namespace_lookup(aml_context_t *ctx, const char *path) {
    if (!ctx || !path) return NULL;
    
    aml_namespace_node_t *node = ctx->root_node;
    char path_copy[256];
    strncpy(path_copy, path, sizeof(path_copy) - 1);
    
    char *component = path_copy;
    if (component[0] == '\\') component++;
    
    char *next;
    while (component && *component) {
        next = strchr(component, '.');
        if (next) *next++ = '\0';
        
        aml_namespace_node_t *child = node->children;
        while (child) {
            if (strcmp(child->simple_name, component) == 0) {
                node = child;
                break;
            }
            child = child->next;
        }
        if (!child) return NULL;
        component = next;
    }
    return node;
}

static aml_namespace_node_t *aml_namespace_create(aml_context_t *ctx, const char *path, aml_object_type_t type) {
    if (!ctx || !path) return NULL;
    
    aml_namespace_node_t *existing = aml_namespace_lookup(ctx, path);
    if (existing) return existing;
    
    aml_namespace_node_t *node = (aml_namespace_node_t*)malloc(sizeof(aml_namespace_node_t));
    if (!node) return NULL;
    memset(node, 0, sizeof(aml_namespace_node_t));
    
    strncpy(node->name, path, sizeof(node->name) - 1);
    node->type = type;
    
    const char *last_dot = strrchr(path, '.');
    if (last_dot) {
        strncpy(node->simple_name, last_dot + 1, 4);
    } else {
        strncpy(node->simple_name, path, 4);
    }
    node->simple_name[4] = '\0';
    
    char parent_path[256];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    char *last_dot_pos = strrchr(parent_path, '.');
    if (last_dot_pos) {
        *last_dot_pos = '\0';
        node->parent = aml_namespace_lookup(ctx, parent_path);
    } else {
        node->parent = ctx->root_node;
    }
    
    if (node->parent) {
        node->next = node->parent->children;
        node->parent->children = node;
    }
    
    return node;
}

// ==================== ПАРСИНГ МЕТОДОВ ====================

static void aml_parse_method(aml_context_t *ctx, u8 *data, sz len, sz *pos) {
    if (!ctx || !data || !pos || *pos >= len) return;
    
    sz start = *pos;
    (*pos)++;
    
    sz pkg_end = *pos;
    sz pkg_len = aml_get_pkg_length(data, len, *pos, &pkg_end);
    if (pkg_len < 5) { *pos = start + 1; return; }
    *pos = pkg_end;
    
    char method_name[256];
    if (aml_parse_namestring(data, len, pos, method_name, sizeof(method_name)) < 0) {
        *pos = start + pkg_len;
        return;
    }
    
    if (*pos >= len) { *pos = start + pkg_len; return; }
    
    u8 flags = data[*pos];
    (*pos)++;
    
    u8 arg_count = flags & 0x07;
    bool serialized = (flags & 0x08) != 0;
    
    char full_name[256];
    // aml_resolve_path
    if (method_name[0] == '\\') {
        strcpy(full_name, method_name);
    } else {
        snprintf(full_name, sizeof(full_name), "%s.%s", ctx->current_scope, method_name);
    }
    
    if (ctx->method_count < 256) {
        aml_method_t *method = &ctx->methods[ctx->method_count];
        strcpy(method->name, full_name);
        method->code = data + *pos;
        method->code_len = start + pkg_len - *pos;
        method->code_start = *pos;
        method->arg_count = arg_count;
        method->serialized = serialized;
        ctx->method_count++;
        
        if (debug_enabled) {
            terminal_printf("[AML] Method: %s (args=%d)\n", full_name, arg_count);
        }
        
        aml_namespace_node_t *node = aml_namespace_create(ctx, full_name, AML_OBJECT_TYPE_METHOD);
        if (node) {
            node->data.method.code = method->code;
            node->data.method.code_len = method->code_len;
            node->data.method.arg_count = arg_count;
        }
    }
    
    *pos = start + pkg_len;
}

// ==================== ПАРСИНГ РЕГИОНОВ ====================

static void aml_parse_region(aml_context_t *ctx, u8 *data, sz len, sz *pos) {
    if (!ctx || !data || !pos || *pos >= len) return;
    
    sz start = *pos;
    (*pos)++;
    
    if (*pos >= len || data[*pos] != AML_EXT_REGION_OP) { *pos = start; return; }
    (*pos)++;
    
    sz pkg_end = *pos;
    sz pkg_len = aml_get_pkg_length(data, len, *pos, &pkg_end);
    if (pkg_len == 0) { *pos = start; return; }
    *pos = pkg_end;
    
    char region_name[128];
    if (aml_parse_namestring(data, len, pos, region_name, sizeof(region_name)) < 0) {
        *pos = start + pkg_len;
        return;
    }
    
    if (*pos >= len) { *pos = start + pkg_len; return; }
    u8 region_space = data[*pos];
    (*pos)++;
    
    u64 region_offset = aml_parse_integer(data, len, pos);
    u64 region_len = aml_parse_integer(data, len, pos);
    
    char full_name[256];
    if (region_name[0] == '\\') {
        strcpy(full_name, region_name);
    } else {
        snprintf(full_name, sizeof(full_name), "%s.%s", ctx->current_scope, region_name);
    }
    
    if (debug_enabled) {
        terminal_printf("[AML] Region: %s (space=0x%02X, base=0x%llx, len=0x%llx)\n",
                        full_name, region_space, region_offset, region_len);
    }
    
    if (ctx->region_count < 64) {
        aml_region_t *region = &ctx->regions[ctx->region_count];
        strcpy(region->name, full_name);
        region->space = region_space;
        region->base = region_offset;
        region->length = region_len;
        ctx->region_count++;
    }
    
    aml_namespace_node_t *node = aml_namespace_create(ctx, full_name, AML_OBJECT_TYPE_REGION);
    if (node) {
        node->data.region.space = region_space;
        node->data.region.base = region_offset;
        node->data.region.length = region_len;
    }
    
    *pos = start + pkg_len;
}

// ==================== ПАРСИНГ УСТРОЙСТВ ====================

static void aml_parse_device(aml_context_t *ctx, u8 *data, sz len, sz *pos) {
    if (!ctx || !data || !pos || *pos >= len) return;
    
    sz start = *pos;
    (*pos)++;
    
    if (*pos >= len || data[*pos] != AML_EXT_DEVICE_OP) { *pos = start; return; }
    (*pos)++;
    
    sz pkg_end = *pos;
    sz pkg_len = aml_get_pkg_length(data, len, *pos, &pkg_end);
    if (pkg_len == 0) { *pos = start; return; }
    *pos = pkg_end;
    
    char device_name[128];
    if (aml_parse_namestring(data, len, pos, device_name, sizeof(device_name)) < 0) {
        *pos = start + pkg_len;
        return;
    }
    
    char full_name[256];
    if (device_name[0] == '\\') {
        strcpy(full_name, device_name);
    } else {
        snprintf(full_name, sizeof(full_name), "%s.%s", ctx->current_scope, device_name);
    }
    
    if (debug_enabled) {
        terminal_printf("[AML] Device: %s\n", full_name);
    }
    
    aml_namespace_node_t *device_node = aml_namespace_create(ctx, full_name, AML_OBJECT_TYPE_DEVICE);
    
    if (strstr(full_name, "PWRB") || strstr(full_name, "PWRN") || 
        strstr(full_name, "PBTN") || strstr(full_name, "PWRF")) {
        if (debug_enabled) terminal_printf("[AML] Found power button device: %s\n", full_name);
        ctx->pwr_info.has_pwrb_device = true;
        strncpy(ctx->pwr_info.pwrb_path, full_name, sizeof(ctx->pwr_info.pwrb_path) - 1);
        ctx->pwr_info.pwrb_node = device_node;
        ctx->pwr_info.found = true;
        if (device_node) device_node->data.device.has_power_button = true;
    }
    
    char old_scope[256];
    strncpy(old_scope, ctx->current_scope, sizeof(old_scope));
    strncpy(ctx->current_scope, full_name, sizeof(ctx->current_scope) - 1);
    
    while (*pos < start + pkg_len) {
        u8 op = data[*pos];
        
        if (op == AML_EXT_OP_PREFIX) {
            if (*pos + 1 < len) {
                u8 ext_op = data[*pos + 1];
                if (ext_op == AML_EXT_REGION_OP) {
                    aml_parse_region(ctx, data, len, pos);
                } else if (ext_op == AML_EXT_DEVICE_OP) {
                    aml_parse_device(ctx, data, len, pos);
                } else if (ext_op == AML_EXT_PROCESSOR_OP) {
                    sz new_pos = aml_skip_pkg_length(data, len, *pos);
                    *pos = new_pos;
                } else {
                    (*pos) += 2;
                }
            } else (*pos)++;
        } else if (op == AML_NAME_OP) {
            (*pos)++;
            char name[128];
            aml_parse_namestring(data, len, pos, name, sizeof(name));
            aml_parse_integer(data, len, pos);
        } else if (op == AML_METHOD_OP) {
            aml_parse_method(ctx, data, len, pos);
        } else if (op == AML_SCOPE_OP) {
            // aml_parse_scope (упрощённо)
            (*pos)++;
            sz scope_pkg_end = *pos;
            sz scope_pkg_len = aml_get_pkg_length(data, len, *pos, &scope_pkg_end);
            *pos = scope_pkg_end;
            char scope_name[128];
            aml_parse_namestring(data, len, pos, scope_name, sizeof(scope_name));
            char full_scope[256];
            snprintf(full_scope, sizeof(full_scope), "%s.%s", ctx->current_scope, scope_name);
            char old_scope2[256];
            strcpy(old_scope2, ctx->current_scope);
            strcpy(ctx->current_scope, full_scope);
            *pos = start + pkg_len;
            strcpy(ctx->current_scope, old_scope2);
        } else {
            (*pos)++;
        }
    }
    
    strncpy(ctx->current_scope, old_scope, sizeof(ctx->current_scope));
    *pos = start + pkg_len;
}

// ==================== ОБХОД ВСЕХ ТАБЛИЦ ====================

void aml_parse_all_tables(aml_context_t *ctx) {
    if (!ctx) return;
    
    terminal_printf("[AML] Parsing AML tables...\n");
    
    ctx->root_node = (aml_namespace_node_t*)malloc(sizeof(aml_namespace_node_t));
    memset(ctx->root_node, 0, sizeof(aml_namespace_node_t));
    strcpy(ctx->root_node->name, "\\");
    strcpy(ctx->root_node->simple_name, "\\");
    
    if (ctx->dsdt_data && ctx->dsdt_len > 36) {
        terminal_printf("[AML] Parsing DSDT (%zu bytes)...\n", ctx->dsdt_len);
        strcpy(ctx->current_scope, "\\");
        sz pos = 36;
        while (pos < ctx->dsdt_len) {
            u8 op = ctx->dsdt_data[pos];
            if (op == AML_EXT_OP_PREFIX && pos + 1 < ctx->dsdt_len) {
                if (ctx->dsdt_data[pos + 1] == AML_EXT_DEVICE_OP) {
                    aml_parse_device(ctx, ctx->dsdt_data, ctx->dsdt_len, &pos);
                } else if (ctx->dsdt_data[pos + 1] == AML_EXT_REGION_OP) {
                    aml_parse_region(ctx, ctx->dsdt_data, ctx->dsdt_len, &pos);
                } else if (ctx->dsdt_data[pos + 1] == AML_EXT_PROCESSOR_OP) {
                    pos = aml_skip_pkg_length(ctx->dsdt_data, ctx->dsdt_len, pos);
                    pos++;
                } else {
                    pos++;
                }
            } else if (op == AML_SCOPE_OP) {
                sz scope_pkg_end = pos;
                sz scope_pkg_len = aml_get_pkg_length(ctx->dsdt_data, ctx->dsdt_len, pos + 1, &scope_pkg_end);
                pos = scope_pkg_end + scope_pkg_len;
            } else if (op == AML_METHOD_OP) {
                aml_parse_method(ctx, ctx->dsdt_data, ctx->dsdt_len, &pos);
            } else {
                pos++;
            }
        }
    }
    
    for (int i = 0; i < ctx->ssdt_count; i++) {
        if (ctx->ssdt_data[i] && ctx->ssdt_len[i] > 36) {
            terminal_printf("[AML] Parsing SSDT[%d] (%zu bytes)...\n", i, ctx->ssdt_len[i]);
            strcpy(ctx->current_scope, "\\");
            sz pos = 36;
            while (pos < ctx->ssdt_len[i]) {
                u8 op = ctx->ssdt_data[i][pos];
                if (op == AML_EXT_OP_PREFIX && pos + 1 < ctx->ssdt_len[i] &&
                    ctx->ssdt_data[i][pos + 1] == AML_EXT_DEVICE_OP) {
                    u8 *save = ctx->dsdt_data;
                    sz save_len = ctx->dsdt_len;
                    ctx->dsdt_data = ctx->ssdt_data[i];
                    ctx->dsdt_len = ctx->ssdt_len[i];
                    aml_parse_device(ctx, ctx->ssdt_data[i], ctx->ssdt_len[i], &pos);
                    ctx->dsdt_data = save;
                    ctx->dsdt_len = save_len;
                } else {
                    pos++;
                }
            }
        }
    }
    
    terminal_printf("[AML] Parsing complete: %d methods, %d regions\n",
                    ctx->method_count, ctx->region_count);
}

// ==================== ПОИСК КНОПКИ ПИТАНИЯ ====================

bool aml_find_power_button(aml_context_t *ctx) {
    if (!ctx || !ctx->initialized) return false;
    
    terminal_printf("[AML] Searching for power button...\n");
    
    memset(&ctx->pwr_info, 0, sizeof(power_button_info_t));
    ctx->gpe_count = 0;
    
    for (int i = 0; i < ctx->method_count; i++) {
        aml_method_t *method = &ctx->methods[i];
        
        if (strstr(method->name, "\\_GPE._L") || strstr(method->name, "\\_GPE._E")) {
            int gpe_num = 0;
            const char *gpe_part = strstr(method->name, "_L");
            if (!gpe_part) gpe_part = strstr(method->name, "_E");
            
            if (gpe_part && gpe_part[2] >= '0' && gpe_part[2] <= '9') {
                gpe_num = gpe_part[2] - '0';
                if (gpe_part[3] >= '0' && gpe_part[3] <= '9') {
                    gpe_num = gpe_num * 10 + (gpe_part[3] - '0');
                }
            }
            
            u8 type = (strstr(method->name, "_E") != NULL) ? 1 : 0;
            
            if (debug_enabled) {
                terminal_printf("[AML] Found GPE method: %s (GPE %d, type %s)\n",
                                method->name, gpe_num, type ? "Edge" : "Level");
            }
            
            if (ctx->gpe_count < 32) {
                aml_gpe_info_t *gpe = &ctx->gpe_info[ctx->gpe_count];
                strcpy(gpe->method_name, method->name);
                gpe->gpe_number = gpe_num;
                gpe->gpe_type = type;
                gpe->found = true;
                
                sz pos = method->code_start;
                u8 *code = method->code;
                sz code_len = method->code_len;
                
                while (pos < code_len) {
                    if (code[pos] == AML_NOTIFY_OP) {
                        pos++;
                        char notify_target[128];
                        sz notify_pos = pos;
                        if (aml_parse_namestring(code, code_len, &notify_pos, notify_target, sizeof(notify_target)) >= 0) {
                            if (strstr(notify_target, "PWRB") || strstr(notify_target, "PWRN") || strstr(notify_target, "PBTN")) {
                                if (debug_enabled) terminal_printf("[AML]   -> Notify(PWRB) found!\n");
                                gpe->is_power_button = true;
                                ctx->pwr_info.gpe = *gpe;
                                ctx->pwr_info.found = true;
                            }
                        }
                        break;
                    }
                    pos++;
                }
                ctx->gpe_count++;
            }
        }
    }
    
    if (ctx->pwr_info.gpe.found) {
        terminal_printf("[AML] Power button found via GPE %d\n", ctx->pwr_info.gpe.gpe_number);
        ctx->pwr_info.is_legacy = false;
        return true;
    }
    
    if (ctx->pwr_info.has_pwrb_device) {
        terminal_printf("[AML] Power button found via PWRB device\n");
        ctx->pwr_info.is_legacy = false;
        return true;
    }
    
    terminal_printf("[AML] No power button found, using legacy PM1 method\n");
    ctx->pwr_info.is_legacy = true;
    ctx->pwr_info.found = true;
    return false;
}

// ==================== ВЫПОЛНЕНИЕ БАЙТКОДА (упрощённая версия) ====================

static int aml_execute_bytecode(aml_context_t *ctx, u8 *code, sz len, sz *pos) {
    if (!ctx || !code || !pos) return -1;
    
    while (*pos < len) {
        u8 op = code[*pos];
        
        switch (op) {
            case AML_ZERO_OP:
                aml_stack_push(ctx, aml_create_integer(0));
                (*pos)++;
                break;
            case AML_ONE_OP:
                aml_stack_push(ctx, aml_create_integer(1));
                (*pos)++;
                break;
            case AML_ONES_OP:
                aml_stack_push(ctx, aml_create_integer(~0ULL));
                (*pos)++;
                break;
            case AML_BYTE_PREFIX:
            case AML_WORD_PREFIX:
            case AML_DWORD_PREFIX:
            case AML_QWORD_PREFIX: {
                u64 val = aml_parse_integer(code, len, pos);
                aml_stack_push(ctx, aml_create_integer(val));
                break;
            }
            case AML_ADD_OP:
                (*pos)++;
                {
                    aml_object_t *op2 = aml_stack_pop(ctx);
                    aml_object_t *op1 = aml_stack_pop(ctx);
                    if (op1 && op2 && op1->type == AML_OBJECT_TYPE_INTEGER && op2->type == AML_OBJECT_TYPE_INTEGER) {
                        aml_stack_push(ctx, aml_create_integer(op1->value.integer + op2->value.integer));
                    }
                    if (op1) aml_free_object(op1);
                    if (op2) aml_free_object(op2);
                }
                break;
            case AML_SUBTRACT_OP:
                (*pos)++;
                {
                    aml_object_t *op2 = aml_stack_pop(ctx);
                    aml_object_t *op1 = aml_stack_pop(ctx);
                    if (op1 && op2 && op1->type == AML_OBJECT_TYPE_INTEGER && op2->type == AML_OBJECT_TYPE_INTEGER) {
                        aml_stack_push(ctx, aml_create_integer(op1->value.integer - op2->value.integer));
                    }
                    if (op1) aml_free_object(op1);
                    if (op2) aml_free_object(op2);
                }
                break;
            case AML_RETURN_OP:
                (*pos)++;
                return 0;
            case AML_NOOP_OP:
                (*pos)++;
                break;
            case AML_EXT_OP_PREFIX:
                (*pos)++;
                if (*pos >= len) break;
                op = code[*pos];
                (*pos)++;
                if (op == AML_EXT_STALL_OP) {
                    aml_object_t *time = aml_stack_pop(ctx);
                    if (time && time->type == AML_OBJECT_TYPE_INTEGER) aml_stall(time->value.integer);
                    if (time) aml_free_object(time);
                } else if (op == AML_EXT_SLEEP_OP) {
                    aml_object_t *time = aml_stack_pop(ctx);
                    if (time && time->type == AML_OBJECT_TYPE_INTEGER) aml_msleep(time->value.integer);
                    if (time) aml_free_object(time);
                }
                break;
            default:
                (*pos)++;
                break;
        }
    }
    return 0;
}

// ==================== ВЫПОЛНЕНИЕ GPE ====================

int aml_execute_gpe(aml_context_t *ctx, u8 gpe_number) {
    if (!ctx) return -1;
    
    for (int i = 0; i < ctx->gpe_count; i++) {
        if (ctx->gpe_info[i].gpe_number == gpe_number && ctx->gpe_info[i].gpe_type == 0) {
            aml_method_t *method = NULL;
            for (int j = 0; j < ctx->method_count; j++) {
                if (strcmp(ctx->methods[j].name, ctx->gpe_info[i].method_name) == 0) {
                    method = &ctx->methods[j];
                    break;
                }
            }
            if (method) {
                sz pos = method->code_start;
                return aml_execute_bytecode(ctx, method->code, method->code_len, &pos);
            }
        }
    }
    return -1;
}

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

bool aml_init_context(aml_context_t *ctx, acpi_t *acpi) {
    if (!ctx || !acpi) return false;
    
    memset(ctx, 0, sizeof(aml_context_t));
    ctx->acpi_ctx = acpi;
    
    if (acpi->dsdt) {
        ctx->dsdt_data = (u8*)acpi->dsdt;
        ctx->dsdt_len = acpi->dsdt->length;
        terminal_printf("[AML] DSDT: %zu bytes\n", ctx->dsdt_len);
    } else {
        terminal_error_printf("[AML] No DSDT\n");
        return false;
    }
    
    for (int i = 0; i < acpi->ssdt_count && i < 16; i++) {
        if (acpi->ssdts[i]) {
            ctx->ssdt_data[i] = (u8*)acpi->ssdts[i];
            ctx->ssdt_len[i] = acpi->ssdts[i]->length;
            ctx->ssdt_count++;
            terminal_printf("[AML] SSDT[%d]: %zu bytes\n", i, ctx->ssdt_len[i]);
        }
    }
    
    aml_parse_all_tables(ctx);
    ctx->initialized = true;
    aml_find_power_button(ctx);
    ctx->exec.sp = -1;
    
    return true;
}

aml_context_t *aml_get_context(void) {
    return &g_aml_ctx;
}

bool aml_get_power_button_info(power_button_info_t *info) {
    if (!info) return false;
    *info = g_aml_ctx.pwr_info;
    return info->found || info->is_legacy;
}

void aml_set_debug(bool enable) {
    debug_enabled = enable;
    g_aml_ctx.debug_mode = enable;
}

void aml_dump_namespace(aml_context_t *ctx) {
    if (!ctx || !ctx->root_node) return;
    
    void dump_node(aml_namespace_node_t *node, int depth) {
        for (int i = 0; i < depth; i++) terminal_printf("  ");
        terminal_printf("%s", node->simple_name);
        switch (node->type) {
            case AML_OBJECT_TYPE_DEVICE: terminal_printf(" [DEV]\n"); break;
            case AML_OBJECT_TYPE_METHOD: terminal_printf(" [MET]\n"); break;
            case AML_OBJECT_TYPE_REGION: terminal_printf(" [REG]\n"); break;
            default: terminal_printf("\n"); break;
        }
        aml_namespace_node_t *child = node->children;
        while (child) {
            dump_node(child, depth + 1);
            child = child->next;
        }
    }
    
    terminal_printf("\n=== AML Namespace ===\n");
    dump_node(ctx->root_node, 0);
    terminal_printf("=====================\n");
}