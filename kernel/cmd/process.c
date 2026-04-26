#include <kernel/types.h>
#include <kernel/sched.h>
#include <kernel/terminal.h>
#include <libk/string.h>

void cmd_ps_simple(void) {
    task_info_t tasks[32];
    int count = task_list(tasks, 32);
    
    if (count == 0) {
        terminal_error_printf("No tasks running\n");
        return;
    }
    
    terminal_printf(" PID  STATE  NAME\n");
    terminal_printf("==== ====== ================\n");
    
    for (int i = 0; i < count; i++) {
        const char* state_str;
        switch (tasks[i].state) {
            case TASK_RUNNING: state_str = "RUN"; break;
            case TASK_READY: state_str = "RDY"; break;
            case TASK_BLOCKED: state_str = "BLK"; break;
            case TASK_ZOMBIE: state_str = "ZOM"; break;
            default: state_str = "UNK"; break;
        }
        
        terminal_printf("%4d  %-4s  %s\n", 
                   tasks[i].pid, state_str, tasks[i].name);
    }
}

void cmd_ps_detailed(void) {
    task_info_t tasks[32];
    int count = task_list(tasks, 32);
    
    if (count == 0) {
        terminal_error_printf("No tasks running\n");
        return;
    }
    
    terminal_printf(" PID  STATE     NAME             REGS     STACK   NEXT\n");
    terminal_printf("==== ======== ================ ======== ======== ========\n");
    
    for (int i = 0; i < count; i++) {
        const char* state_str;
        switch (tasks[i].state) {
            case TASK_RUNNING: state_str = "RUNNING  "; break;
            case TASK_READY: state_str = "READY    "; break;
            case TASK_BLOCKED: state_str = "BLOCKED  "; break;
            case TASK_ZOMBIE: state_str = "ZOMBIE   "; break;
            default: state_str = "UNKNOWN  "; break;
        }
        
        //Get the current task for additional information
        task_t* current = get_current_task();
        const char* current_mark = (current && current->pid == tasks[i].pid) ? "*" : " ";
        
        terminal_printf("%4d%s %s %-16s",
                   tasks[i].pid, current_mark, state_str, tasks[i].name);
        
        //For more detailed information, you need access to the task_t structure
        //Showing basic information
        terminal_printf(" UNK      UNK\n");
    }
    
    terminal_printf("\nTotal: %d task(s)\n", count);
    terminal_printf("Note: * = current task\n");
}

void cmd_kill(char* args) {
    if (!args || args[0] == '\0') {
        terminal_error_printf("Usage: kill <pid>\n");
        return;
    }
    
    int pid = atoi(args);
    if (pid < 0) {
        terminal_error_printf("Invalid PID: %s\n", args);
        return;
    }
    
    /*
 * We all understand that PID 0 - that is, the kernel - cannot be killed, but in 64OS it is possible :) (if the system freezes (and it will freeze or something like that, if the scheduler in principle allows PID 0 (task_stop) to be completed, the user is to blame, in 64OS the policy is freedom)
 */
    
    int result = task_stop(pid);
    if (result == 0) {
        terminal_printf("Process %d terminated\n", pid);
    } else {
        terminal_error_printf("Failed to kill process %d\n", pid);
    }
}
