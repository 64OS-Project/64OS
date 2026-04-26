#ifndef CMD_H
#define CMD_H

#include <kernel/types.h>

extern void cmd_echo(char *args);
extern void cmd_pwd(void);
extern void cmd_help(void);
extern void cmd_clear(void);
extern void cmd_terminfo(void);
extern void cmd_disklist(void);
extern void cmd_diskinfo(char *args);
extern void cmd_cat(char *args);
extern void cmd_format(char *args);
extern void cmd_mount(char *args);
extern void cmd_touch(char *args);
extern void cmd_mkdir(char *args);
extern void cmd_rm(char *args);
extern void cmd_write(char *args);
extern void cmd_ls(char *args);
extern void cmd_rmdir(char *args);
extern void cmd_cd(char *args);
extern void cmd_part_list(void);
extern void cmd_part_scan(void);
extern void cmd_part_info(char *args);
extern void cmd_part_create(char *args);
extern void cmd_part_delete(char *args);
extern void cmd_part_gpt(char *args);
extern void cmd_part_mount(char *args);
extern void cmd_rootlist(void);
extern void cmd_setroot(char *args);
extern void cmd_chooseroot(char *args);
extern void cmd_raid_create(char *args);
extern void cmd_raid_list(char *args);
extern void cmd_raid_delete(char *args);
extern void cmd_time(void);
extern void cmd_meminfo(void);
extern void cmd_reboot(void);
extern void cmd_shutdown(void);
extern void cmd_ps_simple(void);
extern void cmd_ps_detailed(void);
extern void cmd_kill(char* args);
extern void cmd_termcolor(char *args);
extern void cmd_errlist(char *args);

#endif
