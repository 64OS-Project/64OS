#include <kernel/types.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <libk/string.h>
#include <mm/heap.h>
#include <acpi.h>
#include <ktime/clock.h>
#include <mb.h>

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
