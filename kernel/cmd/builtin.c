#include <kernel/types.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <acpi.h>
#include <ktime/clock.h>
#include <mb.h>
#include <rgbcolor.h>
#include <kernel/errlist.h>

extern vfs_inode_t *fs_root;
extern terminal_t g_terminal;
extern char current_path[PATH_MAX];
extern multiboot2_info_t mbinfo;

//Helper function to handle redirection
static int handle_redirecterminaln(const char *cmdline, char **output_text, char **output_file, int *append) {
    char *gt_pos = NULL;
    char *gtgt_pos = NULL;
    
    //We are looking for ">>" (should be found first, because it is longer)
    gtgt_pos = strstr(cmdline, ">>");
    if (gtgt_pos) {
        *append = 1;
        *output_file = gtgt_pos + 2;
    } else {
        gt_pos = strstr(cmdline, ">");
        if (gt_pos) {
            *append = 0;
            *output_file = gt_pos + 1;
        }
    }
    
    if (!gt_pos && !gtgt_pos) {
        //No redirect
        *output_text = (char*)cmdline;
        *output_file = NULL;
        return 0;
    }
    
    //Copy the text up to the symbol >
    int text_len = (gtgt_pos ? (gtgt_pos - cmdline) : (gt_pos - cmdline));
    *output_text = (char*)malloc(text_len + 1);
    if (!*output_text) return -1;
    
    memcpy(*output_text, cmdline, text_len);
    (*output_text)[text_len] = '\0';
    
    //Remove spaces at the beginning of the file name
    while (**output_file == ' ') (*output_file)++;
    
    //Remove spaces at the end of the file name
    char *end = *output_file + strlen(*output_file) - 1;
    while (end > *output_file && (*end == ' ' || *end == '\t')) {
        *end = '\0';
        end--;
    }
    
    //Checking that the file name is not empty
    if (strlen(*output_file) == 0) {
        free(*output_text);
        return -1;
    }
    
    return 0;
}

//Writing to a file with redirection
static int write_to_file_with_redirecterminaln(const char *filepath, const char *content, int append) {
    if (!fs_root) return -1;
    
    u32 flags = O_WRITE | O_CREAT;
    if (append) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }
    
    vfs_file_t *file;
    if (vfs_open(fs_root, filepath, flags, &file) != 0) {
        return -1;
    }
    
    //Adding a line feed
    char *content_with_nl = malloc(strlen(content) + 2);
    if (!content_with_nl) {
        vfs_close(file);
        return -1;
    }
    
    strcpy(content_with_nl, content);
    strcat(content_with_nl, "\n");
    
    u32 written;
    int ret = vfs_write(file, content_with_nl, strlen(content_with_nl), &written);
    
    free(content_with_nl);
    vfs_close(file);
    
    return ret;
}

void cmd_echo(char *args) {
    if (!args || args[0] == '\0') {
        //Empty echo - output an empty line
        if (strstr(args, ">") || strstr(args, ">>")) {
            //Redirecting an empty line - just write \n
            char *output_text = NULL;
            char *output_file = NULL;
            int append = 0;
            
            if (handle_redirecterminaln(args, &output_text, &output_file, &append) == 0 && output_file) {
                //output_text will be empty, write only \n
                vfs_file_t *file;
                u32 flags = O_WRITE | O_CREAT;
                if (append) flags |= O_APPEND;
                else flags |= O_TRUNC;
                
                if (vfs_open(fs_root, output_file, flags, &file) == 0) {
                    u32 written;
                    vfs_write(file, "\n", 1, &written);
                    vfs_close(file);
                }
                if (output_text) free(output_text);
            }
        } else {
            terminal_printf("\n");
        }
        return;
    }
    
    char *output_text = NULL;
    char *output_file = NULL;
    int append = 0;
    
    if (handle_redirecterminaln(args, &output_text, &output_file, &append) != 0) {
        terminal_error_printf("echo: invalid redirecterminaln syntax\n");
        return;
    }
    
    if (output_file) {
        if (write_to_file_with_redirecterminaln(output_file, output_text, append) != 0) {
            terminal_error_printf("echo: cannot write to '%s'\n", output_file);
        }
        free(output_text);
    } else {
        //Normal screen output - also with a new line
        terminal_printf("%s\n", args);
    }
}

void cmd_help(void) {
    terminal_printf("Commands:\n");
    terminal_printf("  help                - Show this help\n");
    terminal_printf("  clear               - Clear screen\n");
    terminal_printf("  terminfo            - Show terminal info\n");
    terminal_printf("  echo <text>         - Print text\n");
    terminal_printf("  meminfo             - Print memory info\n");
    terminal_printf("\n");
    terminal_printf("Disk commands:\n");
    terminal_printf("  disklist            - List all block devices\n");
    terminal_printf("  diskinfo <disk>     - Show disk informaterminaln\n");
    terminal_printf("\n");
    terminal_printf("Filesystem commands:\n");
    terminal_printf("  mount <fs> <num>    - Mount filesystem (exfat, fat32)\n");
    terminal_printf("  format <fs> <num>   - Format disk (exfat, fat32)\n");
    terminal_printf("  ls [path]           - List directory\n");
    terminal_printf("  cd [path]           - Change directory\n");
    terminal_printf("  pwd                 - Print working directory\n");
    terminal_printf("  cat <file>          - Display file content\n");
    terminal_printf("  touch <file>        - Create empty file\n");
    terminal_printf("  mkdir <dir>         - Create directory\n");
    terminal_printf("  rm <path>           - Remove file or directory\n");
    terminal_printf("  rmdir <dir>         - Remove empty directory\n");
    terminal_printf("  write <file> <text> - Write text to file\n");
    terminal_printf("  rootlist            - List all filesystems with .root marker\n");
    terminal_printf("  setroot <fs> <disk> - Set root filesystem (create .root)\n");
    terminal_printf("  chooseroot <index>  - Choose root from multiple candidates\n");
    terminal_printf("\nPartiterminaln commands:\n");
    terminal_printf("  part list                    - List all partiterminalns\n");
    terminal_printf("  part scan                    - Scan all disks for partiterminalns\n");
    terminal_printf("  part info <part>             - Show partiterminaln info\n");
    terminal_printf("  part create <disk> <type> <size> - Create partiterminaln\n");
    terminal_printf("  part delete <disk> <num>     - Delete partiterminaln\n");
    terminal_printf("  part gpt <disk>              - Create GPT on disk\n");
    terminal_printf("  part mount <part> [path]     - Mount partiterminaln\n");
    terminal_printf("\nPower commands:\n");
    terminal_printf("  shutdown                     - Shutdown system\n");
    terminal_printf("  reboot                       - Reboot system\n");
    terminal_printf("\nTask control commands:\n");
    terminal_printf("  ps <-l>                      - Process list show\n");
    terminal_printf("  kill <pid>                   - Kill process by PID\n");
}

void cmd_clear(void) {
    terminal_clear_screen();
}

void cmd_time(void) {
    char time_str[9];
    format_clock(time_str, system_clock);
    terminal_printf("Current time: %s\n", time_str);
}

void cmd_terminfo(void) {
    terminal_printf("'64Shell' Info:\n");
    terminal_printf("  Total lines: %d\n", g_terminal.total_lines);
    terminal_printf("  Visible lines: %d\n", g_terminal.visible_lines);
    terminal_printf("  Mode: %s\n", 
        g_terminal.mode == TERM_MODE_NORMAL ? "Normal" : "Prompt");
}

void cmd_meminfo(void) {
    kmalloc_stats_t stats;
    get_kmalloc_stats(&stats);
    
    terminal_printf("Kernel Heap Statistics:\n");
    terminal_printf("  Total managed:   %lu bytes\n", stats.total_managed);
    terminal_printf("  Used payload:    %lu bytes\n", stats.used_payload);
    terminal_printf("  Free payload:    %lu bytes\n", stats.free_payload);
    terminal_printf("  Largest free:    %lu bytes\n", stats.largest_free);
    terminal_printf("  Number of blocks:%lu\n", stats.num_blocks);
    terminal_printf("  Used blocks:     %lu\n", stats.num_used);
    terminal_printf("  Free blocks:     %lu\n", stats.num_free);
    
    u64 total_memory = multiboot2_get_usable_memory(&mbinfo);
    terminal_printf("\nSystem Memory:\n");
    terminal_printf("  Total RAM:       %lu MB\n", total_memory / (1024 * 1024));
    terminal_printf("  Used by kernel:  %lu KB\n", stats.total_managed / 1024);
}

void cmd_reboot(void) {
    terminal_printf("Rebooting system...\n");
    acpi_reboot();
    //If ACPI does not work, try the 8042 controller
    asm volatile("outb %%al, %%dx" : : "a"((u8)0xFE), "d"((u16)0x64));
    while(1);
}

void cmd_shutdown(void) {
    terminal_printf("Shutting down...\n");
    acpi_shutdown();
    //If ACPI did not work, just stop it
    terminal_error_printf("ACPI shutdown failed. System halted.\n");
    while(1) asm volatile("hlt");
}

static u32 parse_hex_color(const char *str) {
    if (!str || str[0] != '0' || str[1] != 'x') return 0xFFFFFF;
    
    u32 color = 0;
    const char *p = str + 2;
    
    for (int i = 0; i < 6 && *p; i++) {
        color <<= 4;
        if (*p >= '0' && *p <= '9') color |= (*p - '0');
        else if (*p >= 'A' && *p <= 'F') color |= (*p - 'A' + 10);
        else if (*p >= 'a' && *p <= 'f') color |= (*p - 'a' + 10);
        else break;
        p++;
    }
    
    return color;
}

void cmd_termcolor(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: termcolor fg=0xRRGGBB bg=0xRRGGBB\n");
        terminal_printf("Example: termcolor fg=0xFFFFFF bg=0x000000\n");
        terminal_printf("         termcolor fg=0x00FF00 bg=0x000000\n");
        terminal_printf("         termcolor bg=0x001133\n");
        terminal_printf("\nCurrent colors:\n");
        terminal_printf("  Foreground: #%06X\n", 
                       (g_terminal.color_fg.r << 16) | 
                       (g_terminal.color_fg.g << 8) | 
                       g_terminal.color_fg.b);
        terminal_printf("  Background: #%06X\n",
                       (g_terminal.color_bg.r << 16) | 
                       (g_terminal.color_bg.g << 8) | 
                       g_terminal.color_bg.b);
        return;
    }
    
    u32 fg_color = 0xFFFFFF;
    u32 bg_color = 0x000000;
    bool fg_set = false;
    bool bg_set = false;
    
    // Парсим аргументы
    char *saveptr;
    char *token = strtok_r(args, " ", &saveptr);
    
    while (token) {
        if (strncmp(token, "fg=", 3) == 0) {
            fg_color = parse_hex_color(token + 3);
            fg_set = true;
        } else if (strncmp(token, "bg=", 3) == 0) {
            bg_color = parse_hex_color(token + 3);
            bg_set = true;
        } else if (strncmp(token, "help", 4) == 0) {
            terminal_printf("termcolor - change terminal colors\n");
            terminal_printf("Usage: termcolor fg=0xRRGGBB bg=0xRRGGBB\n");
            terminal_printf("Examples:\n");
            terminal_printf("  termcolor fg=0xFFFFFF bg=0x000000  # White on black\n");
            terminal_printf("  termcolor fg=0x00FF00 bg=0x000000  # Green on black\n");
            terminal_printf("  termcolor fg=0x000000 bg=0xFFFFFF  # Black on white\n");
            terminal_printf("  termcolor bg=0x001133              # Navy background\n");
            return;
        }
        token = strtok_r(NULL, " ", &saveptr);
    }
    
    // Применяем цвета
    if (fg_set) {
        g_terminal.color_fg = FB_RGB((fg_color >> 16) & 0xFF, 
                                      (fg_color >> 8) & 0xFF, 
                                      fg_color & 0xFF);
    }
    
    if (bg_set) {
        g_terminal.color_bg = FB_RGB((bg_color >> 16) & 0xFF, 
                                      (bg_color >> 8) & 0xFF, 
                                      bg_color & 0xFF);
        // Также обновляем цвет фона терминала при очистке
        fb_clear(g_terminal.color_bg);
    }
    
    terminal_printf("Colors changed:\n");
    if (fg_set) terminal_printf("  Foreground: #%06X -> #%06X\n", 
                                fg_color, fg_color);
    if (bg_set) terminal_printf("  Background: #%06X -> #%06X\n", 
                                bg_color, bg_color);
    
    // Обновляем экран
    terminal_refresh();
}

void cmd_errlist(char *args) {
    if (!args || args[0] == '\0') {
        terminal_printf("Usage: errlist <command> [options]\n");
        terminal_printf("Commands:\n");
        terminal_printf("  show                    - Show all errors/warnings\n");
        terminal_printf("  show -e                 - Show only errors\n");
        terminal_printf("  show -w                 - Show only warnings\n");
        terminal_printf("  show -g <subsystem>     - Show by subsystem\n");
        terminal_printf("  show -d <description>   - Show by description\n");
        terminal_printf("  show -c <code>          - Show by error code\n");
        terminal_printf("  show -l <n>             - Show last n entries\n");
        terminal_printf("  clear                   - Clear all entries\n");
        terminal_printf("  clear -g <subsystem>    - Clear by subsystem\n");
        terminal_printf("  clear -c <code>         - Clear by code\n");
        terminal_printf("  stats                   - Show statistics\n");
        return;
    }
    
    // Парсим команду
    char *cmd = strtok_r(args, " ", &args);
    
    if (strcmp(cmd, "show") == 0) {
        char *opt = strtok_r(NULL, " ", &args);
        
        if (!opt) {
            errlist_show_all();
        } else if (strcmp(opt, "-e") == 0) {
            errlist_show_errors_only();
        } else if (strcmp(opt, "-w") == 0) {
            errlist_show_warnings_only();
        } else if (strcmp(opt, "-g") == 0) {
            char *subsys = strtok_r(NULL, " ", &args);
            if (subsys) errlist_show_by_subsystem(subsys);
            else terminal_error_printf("Missing subsystem name\n");
        } else if (strcmp(opt, "-d") == 0) {
            char *desc = strtok_r(NULL, " ", &args);
            if (desc) {
                u32 count;
                errlist_find_by_message(desc, &count);
                terminal_printf("Found %d entries containing '%s'\n", count, desc);
                // Можно вывести найденные
            } else terminal_error_printf("Missing description\n");
        } else if (strcmp(opt, "-c") == 0) {
            char *code_str = strtok_r(NULL, " ", &args);
            if (code_str) {
                u32 code = atoi(code_str);
                errlist_show_by_code(code);
            } else terminal_error_printf("Missing code\n");
        } else if (strcmp(opt, "-l") == 0) {
            char *n_str = strtok_r(NULL, " ", &args);
            if (n_str) {
                u32 n = atoi(n_str);
                errlist_show_last(n);
            } else terminal_error_printf("Missing number\n");
        } else {
            terminal_error_printf("Unknown option: %s\n", opt);
        }
    } 
    else if (strcmp(cmd, "clear") == 0) {
        char *opt = strtok_r(NULL, " ", &args);
        
        if (!opt) {
            errlist_clear();
        } else if (strcmp(opt, "-g") == 0) {
            char *subsys = strtok_r(NULL, " ", &args);
            if (subsys) errlist_clear_by_subsystem(subsys);
            else terminal_error_printf("Missing subsystem name\n");
        } else if (strcmp(opt, "-c") == 0) {
            char *code_str = strtok_r(NULL, " ", &args);
            if (code_str) {
                u32 code = atoi(code_str);
                errlist_clear_by_code(code);
            } else terminal_error_printf("Missing code\n");
        } else {
            terminal_error_printf("Unknown option: %s\n", opt);
        }
    }
    else if (strcmp(cmd, "stats") == 0) {
        terminal_printf("\n=== Error List Statistics ===\n");
        terminal_printf("Total entries:  %u\n", errlist_get_count());
        terminal_printf("Errors:         %u\n", errlist_get_error_count());
        terminal_printf("Warnings:       %u\n", errlist_get_warning_count());
        
        // Статистика по подсистемам
        terminal_printf("\nBy subsystem:\n");
        char subsystems[32][ERRLIST_MAX_SUBSYSTEM];
        u32 subsys_counts[32] = {0};
        u32 subsys_count = 0;
        
        for (u32 i = 0; i < g_errlist.count; i++) {
            bool found = false;
            for (u32 j = 0; j < subsys_count; j++) {
                if (strcmp(subsystems[j], g_errlist.entries[i].subsystem) == 0) {
                    subsys_counts[j]++;
                    found = true;
                    break;
                }
            }
            if (!found && subsys_count < 32) {
                strcpy(subsystems[subsys_count], g_errlist.entries[i].subsystem);
                subsys_counts[subsys_count] = 1;
                subsys_count++;
            }
        }
        
        for (u32 i = 0; i < subsys_count; i++) {
            terminal_printf("  %-20s: %u\n", subsystems[i], subsys_counts[i]);
        }
        terminal_printf("=============================\n\n");
    }
    else {
        terminal_error_printf("Unknown command: %s\n", cmd);
    }
}