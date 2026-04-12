#include <kernel/ud.h>
#include <kernel/panic.h>
#include <kernel/terminal.h>
#include <libk/string.h>
#include <asm/cpu.h>

#define MAX_INSTRUCTION_BYTES 16

/*
 * Emulated Instruction Table (Extensible)
 */
typedef struct {
    u8 bytes[MAX_INSTRUCTION_BYTES];
    u32 length;
    bool (*emulate)(exception_frame_t *frame);
} emulated_insn_t;

static emulated_insn_t emulated_insns[4];
static u32 emulated_count = 0;

/*
 * Registering instructions for emulation
 */
void register_emulated_instruction(const u8 *bytes, u32 length, 
                                    bool (*emulate)(exception_frame_t *frame)) {
    if (emulated_count >= 4) return;
    memcpy(emulated_insns[emulated_count].bytes, bytes, length);
    emulated_insns[emulated_count].length = length;
    emulated_insns[emulated_count].emulate = emulate;
    emulated_count++;
}

/*
 * Dump instructions to RIP address
 */
void dump_invalid_instruction(u64 rip) {
    u8 bytes[MAX_INSTRUCTION_BYTES];
    u32 i;
    
    terminal_error_printf("[#UD] Invalid instruction at RIP: 0x%llx\n", rip);
    terminal_error_printf("[#UD] Opcode bytes: ");
    
    for (i = 0; i < MAX_INSTRUCTION_BYTES; i++) {
        bytes[i] = *(volatile u8*)(rip + i);
        terminal_error_printf("%02x ", bytes[i]);
    }
    terminal_error_printf("\n");
    
    /*
 * Trying to define an instruction
 */
    if (bytes[0] == 0x0F) {
        if (bytes[1] == 0x0B) terminal_error_printf("[#UD] UD2 instruction (intentional)\n");
        else if (bytes[1] == 0x01 && bytes[2] == 0xC2) terminal_error_printf("[#UD] VMCALL (hypervisor)\n");
        else if (bytes[1] == 0x01 && bytes[2] == 0xD9) terminal_error_printf("[#UD] VMMCALL (hypervisor)\n");
        else terminal_error_printf("[#UD] Unknown 0F-prefixed instruction\n");
    } else if (bytes[0] == 0xF1) {
        terminal_error_printf("[#UD] ICEBP (intentional debug)\n");
    } else if (bytes[0] == 0xFF && (bytes[1] & 0xF8) == 0xE0) {
        terminal_error_printf("[#UD] Invalid register jump/call\n");
    } else {
        terminal_error_printf("[#UD] Unknown opcode\n");
    }
}

/*
 * Checking if an instruction is emulated
 */
static bool try_emulate(exception_frame_t *frame) {
    u8 bytes[MAX_INSTRUCTION_BYTES];
    u32 i;
    
    /*
 * Reading the instruction bytes
 */
    for (i = 0; i < MAX_INSTRUCTION_BYTES; i++) {
        bytes[i] = *(volatile u8*)(frame->rip + i);
    }
    
    /*
 * Looking in the emulation table
 */
    for (i = 0; i < emulated_count; i++) {
        if (memcmp(bytes, emulated_insns[i].bytes, emulated_insns[i].length) == 0) {
            return emulated_insns[i].emulate(frame);
        }
    }
    
    return false;
}

/*
 * Main #UD handler
 */
ud_result_t handle_invalid_opcode(exception_frame_t *frame, ud_context_t context) {
    ud_result_t result = { .handled = false, .reason = NULL };
    
    terminal_error_printf("[#UD] Invalid Opcode Exception\n");
    terminal_error_printf("[#UD] RIP: 0x%llx, CS: 0x%llx, RFLAGS: 0x%llx\n", 
                          frame->rip, frame->cs, frame->rflags);
    
    /*
 * We are trying to emulate
 */
    if (try_emulate(frame)) {
        terminal_warn_printf("[#UD] Instruction emulated successfully\n");
        result.handled = true;
        return result;
    }
    
    /*
 * Dump instructions for debugging
 */
    dump_invalid_instruction(frame->rip);
    
    /*
 * Context-sensitive processing
 */
    if (context == UD_CONTEXT_KERNEL) {
        result.reason = "KERNEL_INVALID_OPCODE";
        return result;  /*
 * Panic will be outside
 */
    } else {
        /*
 * User mode - kill the process
 */
        terminal_error_printf("[#UD] User process executed invalid opcode\n");
        result.handled = true;  /*
 * The SIGILL signal will be sent
 */
        result.reason = "SIGILL";
        return result;
    }
}
