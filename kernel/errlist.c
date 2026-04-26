#include <kernel/errlist.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <ktime/clock.h>

errlist_t g_errlist = {0};

void errlist_init(void) {
    memset(&g_errlist, 0, sizeof(errlist_t));
    g_errlist.enabled = true;
    terminal_printf("[ERRLIST] Initialized\n");
}

void errlist_add(const char *subsystem, const char *message, u32 code, bool is_error) {
    errlist_add_ext(subsystem, message, code, is_error, NULL, 0);
}

void errlist_add_ext(const char *subsystem, const char *message, u32 code, 
                     bool is_error, const char *file, u32 line) {
    if (!g_errlist.enabled) return;
    if (g_errlist.count >= ERRLIST_MAX_ENTRIES) return;
    if (!subsystem || !message) return;
    
    errlist_entry_t *entry = &g_errlist.entries[g_errlist.count];
    
    strncpy(entry->subsystem, subsystem, ERRLIST_MAX_SUBSYSTEM - 1);
    strncpy(entry->message, message, ERRLIST_MAX_MESSAGE - 1);
    entry->code = code;
    entry->is_error = is_error;
    entry->timestamp = system_clock;
    
    if (file) {
        strncpy(entry->file, file, 63);
        entry->line = line;
    } else {
        entry->file[0] = '\0';
        entry->line = 0;
    }
    
    g_errlist.count++;
    
    // Если ошибка критичная, выводим в терминал
    if (is_error && code != 0) {
        terminal_error_printf("[%s] %s (code=%d)\n", subsystem, message, code);
    }
}

void errlist_clear(void) {
    memset(&g_errlist, 0, sizeof(errlist_t));
    g_errlist.enabled = true;
    terminal_printf("[ERRLIST] Cleared\n");
}

void errlist_clear_by_subsystem(const char *subsystem) {
    if (!subsystem) return;
    
    u32 new_count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (strcmp(g_errlist.entries[i].subsystem, subsystem) != 0) {
            if (new_count != i) {
                memcpy(&g_errlist.entries[new_count], &g_errlist.entries[i], sizeof(errlist_entry_t));
            }
            new_count++;
        }
    }
    g_errlist.count = new_count;
    terminal_printf("[ERRLIST] Cleared entries for subsystem: %s\n", subsystem);
}

void errlist_clear_by_code(u32 code) {
    u32 new_count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (g_errlist.entries[i].code != code) {
            if (new_count != i) {
                memcpy(&g_errlist.entries[new_count], &g_errlist.entries[i], sizeof(errlist_entry_t));
            }
            new_count++;
        }
    }
    g_errlist.count = new_count;
    terminal_printf("[ERRLIST] Cleared entries with code: %d\n", code);
}

errlist_entry_t* errlist_find_by_subsystem(const char *subsystem, u32 *count) {
    static errlist_entry_t results[ERRLIST_MAX_ENTRIES];
    *count = 0;
    
    for (u32 i = 0; i < g_errlist.count && *count < ERRLIST_MAX_ENTRIES; i++) {
        if (strcmp(g_errlist.entries[i].subsystem, subsystem) == 0) {
            memcpy(&results[*count], &g_errlist.entries[i], sizeof(errlist_entry_t));
            (*count)++;
        }
    }
    
    return results;
}

errlist_entry_t* errlist_find_by_code(u32 code, u32 *count) {
    static errlist_entry_t results[ERRLIST_MAX_ENTRIES];
    *count = 0;
    
    for (u32 i = 0; i < g_errlist.count && *count < ERRLIST_MAX_ENTRIES; i++) {
        if (g_errlist.entries[i].code == code) {
            memcpy(&results[*count], &g_errlist.entries[i], sizeof(errlist_entry_t));
            (*count)++;
        }
    }
    
    return results;
}

errlist_entry_t* errlist_find_by_message(const char *substr, u32 *count) {
    static errlist_entry_t results[ERRLIST_MAX_ENTRIES];
    *count = 0;
    
    for (u32 i = 0; i < g_errlist.count && *count < ERRLIST_MAX_ENTRIES; i++) {
        if (strstr(g_errlist.entries[i].message, substr)) {
            memcpy(&results[*count], &g_errlist.entries[i], sizeof(errlist_entry_t));
            (*count)++;
        }
    }
    
    return results;
}

void errlist_show_all(void) {
    terminal_printf("Error List (%d entries)\n", g_errlist.count);
    
    if (g_errlist.count == 0) {
        terminal_printf("No errors or warnings\n");
        return;
    }
    
    for (u32 i = 0; i < g_errlist.count; i++) {
        errlist_entry_t *e = &g_errlist.entries[i];
        char time_str[9];
        format_clock(time_str, e->timestamp);
        
        terminal_printf("[%s] %s", time_str, e->subsystem);
        
        if (e->is_error) {
            terminal_info_printf(" ERROR");
        } else {
            terminal_info_printf(" WARN");
        }
        
        terminal_printf(": %s", e->message);
        if (e->code != 0) terminal_printf(" (code=%d)", e->code);
        if (e->file[0]) terminal_printf(" [%s:%u]", e->file, e->line);
        terminal_printf("\n");
    }
}

void errlist_show_by_subsystem(const char *subsystem) {
    u32 count;
    errlist_entry_t *entries = errlist_find_by_subsystem(subsystem, &count);
    
    terminal_printf("Errors in %s (%d entries)\n", subsystem, count);
    
    for (u32 i = 0; i < count; i++) {
        char time_str[9];
        format_clock(time_str, entries[i].timestamp);
        
        terminal_printf("[%s] %s", time_str, entries[i].message);
        if (entries[i].code != 0) terminal_printf(" (code=%d)", entries[i].code);
        terminal_printf("\n");
    }
}

void errlist_show_by_code(u32 code) {
    u32 count;
    errlist_entry_t *entries = errlist_find_by_code(code, &count);
    
    terminal_printf("Errors with code %d (%d entries)\n", code, count);
    
    for (u32 i = 0; i < count; i++) {
        char time_str[9];
        format_clock(time_str, entries[i].timestamp);
        
        terminal_printf("[%s] %s: %s\n", time_str, 
                       entries[i].subsystem, entries[i].message);
    }
}

void errlist_show_errors_only(void) {
    u32 err_count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (g_errlist.entries[i].is_error) {
            char time_str[9];
            format_clock(time_str, g_errlist.entries[i].timestamp);
            
            terminal_info_printf("[%s] %s: %s (code=%d)\n", 
                                 time_str,
                                 g_errlist.entries[i].subsystem,
                                 g_errlist.entries[i].message,
                                 g_errlist.entries[i].code);
            err_count++;
        }
    }
    
    if (err_count == 0) {
        terminal_printf("No errors\n");
    }
}

void errlist_show_warnings_only(void) {
    u32 warn_count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (!g_errlist.entries[i].is_error) {
            char time_str[9];
            format_clock(time_str, g_errlist.entries[i].timestamp);
            
            terminal_info_printf("[%s] %s: %s\n", 
                                time_str,
                                g_errlist.entries[i].subsystem,
                                g_errlist.entries[i].message);
            warn_count++;
        }
    }
    
    if (warn_count == 0) {
        terminal_printf("No warnings\n");
    }
}

void errlist_show_last(u32 n) {
    if (n > g_errlist.count) n = g_errlist.count;
    
    terminal_printf("Last %d errors\n", n);
    
    for (u32 i = g_errlist.count - n; i < g_errlist.count; i++) {
        errlist_entry_t *e = &g_errlist.entries[i];
        char time_str[9];
        format_clock(time_str, e->timestamp);
        
        terminal_printf("[%s] %s: %s", time_str, e->subsystem, e->message);
        if (e->code != 0) terminal_printf(" (code=%d)", e->code);
        terminal_printf("\n");
    }
}

u32 errlist_get_count(void) {
    return g_errlist.count;
}

u32 errlist_get_error_count(void) {
    u32 count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (g_errlist.entries[i].is_error) count++;
    }
    return count;
}

u32 errlist_get_warning_count(void) {
    u32 count = 0;
    for (u32 i = 0; i < g_errlist.count; i++) {
        if (!g_errlist.entries[i].is_error) count++;
    }
    return count;
}