#include <kernel/terminal.h>
#include <kernel/types.h>
#include <libk/string.h>
#include <kernel/cmd.h>
#include <ps2kbd.h>
#include <fs/vfs.h>

extern terminal_t g_terminal;
extern void term_add_line(const char* text, fb_color_t color);

static int tokenize_quoted(char *str, char **argv, int max_args) {
    int argc = 0;
    char *p = str;
    int in_quote = 0;
    char *start = NULL;
    
    while (*p && argc < max_args) {
        if (!in_quote && (*p == ' ' || *p == '\t')) {
            p++;
            continue;
        }
        
        if (!start) {
            start = p;
            if (*p == '\'' || *p == '"') {
                in_quote = *p++;
                start = p;
                continue;
            }
        }
        
        if ((in_quote && *p == in_quote) || (!in_quote && (*p == ' ' || *p == '\t'))) {
            if (in_quote) {
                p++;
                in_quote = 0;
            }
            *p = '\0';
            argv[argc++] = start;
            start = NULL;
            p++;
            continue;
        }
        
        p++;
    }
    
    if (start) {
        argv[argc++] = start;
    }
    
    return argc;
}

static void parse_command(char *cmdline, char *cmd, char **args) {
    int i = 0;
    
    //Copy the command up to the first space
    while (cmdline[i] != ' ' && cmdline[i] != '\0' && i < 63) {
        cmd[i] = cmdline[i];
        i++;
    }
    cmd[i] = '\0';
    
    //Skip spaces and find arguments
    while (cmdline[i] == ' ') i++;
    if (cmdline[i] != '\0') {
        *args = &cmdline[i];
    } else {
        *args = NULL;
    }
}

extern void terminal_generate_prompt(char *buffer, int buf_size);

void terminal_handle_input(char c) {
    if (!g_terminal.prompt_enabled || g_terminal.mode != TERM_MODE_PROMPT) {
        return;
    }
    
    if (c == '\n' || c == '\r') {
        if (g_terminal.input_pos > 0) {
            char prompt_text[PATH_MAX + 32];
            terminal_generate_prompt(prompt_text, sizeof(prompt_text));
            char cmd_display[PATH_MAX + 300];
            snprintf(cmd_display, sizeof(cmd_display), "%s%s", prompt_text, g_terminal.input_buffer);
            term_add_line(cmd_display, g_terminal.color_fg);
            terminal_refresh();
            
            char cmd[64];
            char *args = NULL;
            parse_command(g_terminal.input_buffer, cmd, &args);
            
            //=== BASIC COMMANDS ===
            if (strcmp(cmd, "help") == 0) {
                cmd_help();
            }
            else if (strcmp(cmd, "clear") == 0) {
                cmd_clear();
            }
            else if (strcmp(cmd, "terminfo") == 0) {
                cmd_terminfo();
            }
            else if (strcmp(cmd, "echo") == 0) {
                cmd_echo(args);
            }
            else if (strcmp(cmd, "pwd") == 0) {
                cmd_pwd();
            }
            //=== DISK COMMANDS ===
            else if (strcmp(cmd, "disklist") == 0) {
                cmd_disklist();
            }
            else if (strcmp(cmd, "diskinfo") == 0) {
                cmd_diskinfo(args);
            }
            //=== FILE COMMANDS (as in ALK - with args string) ===
            else if (strcmp(cmd, "mount") == 0) {
                cmd_mount(args);
            }
            else if (strcmp(cmd, "format") == 0) {
                cmd_format(args);
            }
            else if (strcmp(cmd, "cat") == 0) {
                cmd_cat(args);
            }
            else if (strcmp(cmd, "ls") == 0) {
                cmd_ls(args);
            }
            else if (strcmp(cmd, "touch") == 0) {
                cmd_touch(args);
            }
            else if (strcmp(cmd, "mkdir") == 0) {
                cmd_mkdir(args);
            }
            else if (strcmp(cmd, "rm") == 0) {
                cmd_rm(args);
            }
            else if (strcmp(cmd, "rmdir") == 0) {
                cmd_rmdir(args);
            }
            else if (strcmp(cmd, "write") == 0) {
                cmd_write(args);
            }
            else if (strcmp(cmd, "cd") == 0) {
                cmd_cd(args);
            } else if (strcmp(cmd, "rootlist") == 0) {
                cmd_rootlist();
            } else if (strcmp(cmd, "setroot") == 0) {
                cmd_setroot(args);
            } else if (strcmp(cmd, "chooseroot") == 0) {
                cmd_chooseroot(args);
            } else if (strcmp(cmd, "part") == 0) {
                char *subcmd = strtok_r(args, " ", &args);
    
                 if (!subcmd) {
                     terminal_printf("Usage: part <list|scan|info|create|delete|gpt|mount>\n");
                     return;
                 }
    
                 if (strcmp(subcmd, "list") == 0) {
                     cmd_part_list();
                 } else if (strcmp(subcmd, "scan") == 0) {
                     cmd_part_scan();
                 } else if (strcmp(subcmd, "info") == 0) {
                     cmd_part_info(args);
                 } else if (strcmp(subcmd, "create") == 0) {
                     cmd_part_create(args);
                 } else if (strcmp(subcmd, "delete") == 0) {
                     cmd_part_delete(args);
                 } else if (strcmp(subcmd, "gpt") == 0) {
                     cmd_part_gpt(args);
                 } else if (strcmp(subcmd, "mount") == 0) {
                     cmd_part_mount(args);
                 } else  {
                     terminal_error_printf("Unknown part command: %s\n", subcmd);
                 }
            } else if (strcmp(cmd, "ps") == 0) {
                if (args && strcmp(args, "-l") == 0) {
                    cmd_ps_detailed();
                } else {
                    cmd_ps_simple();
                }
            } else if (strcmp(cmd, "reboot") == 0) {
                cmd_reboot();
            } else if (strcmp(cmd, "shutdown") == 0) {
                cmd_shutdown();
            } else if (strcmp(cmd, "meminfo") == 0) {
                cmd_meminfo();
            } else if (strcmp(cmd, "kill") == 0) {
                cmd_kill(args);
            } else if (strcmp(cmd, "time") == 0) {
                cmd_time();
            }
            else {
                if (strlen(g_terminal.input_buffer) > 0) {
                    terminal_printf("Unknown command: %s\n", cmd);
                }
            }
        }
        
        g_terminal.cursor_pos = 0;
        g_terminal.input_pos = 0;
        memset(g_terminal.input_buffer, 0, sizeof(g_terminal.input_buffer));
        terminal_update_prompt_line();
        
    }
    if (c == '\b') {
        if (g_terminal.cursor_pos > 0) {
            //Shift all characters after the cursor to the left
            for (int i = g_terminal.cursor_pos; i < g_terminal.input_pos; i++) {
                g_terminal.input_buffer[i - 1] = g_terminal.input_buffer[i];
            }
            g_terminal.input_pos--;
            g_terminal.cursor_pos--;
            g_terminal.input_buffer[g_terminal.input_pos] = '\0';
            terminal_update_prompt_line();
        }
        return;
    }
    
    //Delete processing (if you add it)
    if (c == 0x7F || c == KEY_DELETE) {
        if (g_terminal.cursor_pos < g_terminal.input_pos) {
            for (int i = g_terminal.cursor_pos; i < g_terminal.input_pos - 1; i++) {
                g_terminal.input_buffer[i] = g_terminal.input_buffer[i + 1];
            }
            g_terminal.input_pos--;
            g_terminal.input_buffer[g_terminal.input_pos] = '\0';
            terminal_update_prompt_line();
        }
        return;
    }
    
    //Regular characters
    if (c >= 0x20 && c < 0x7F) {
        if (g_terminal.input_pos < sizeof(g_terminal.input_buffer) - 1) {
            //Shift characters after the cursor to the right
            for (int i = g_terminal.input_pos; i > g_terminal.cursor_pos; i--) {
                g_terminal.input_buffer[i] = g_terminal.input_buffer[i - 1];
            }
            g_terminal.input_buffer[g_terminal.cursor_pos] = c;
            g_terminal.cursor_pos++;
            g_terminal.input_pos++;
            g_terminal.input_buffer[g_terminal.input_pos] = '\0';
            terminal_update_prompt_line();
        }
        return;
    }
}
