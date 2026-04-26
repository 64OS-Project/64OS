#include <fs/devfs.h>
#include <kernel/terminal.h>
#include <ps2kbd.h>
#include <libk/string.h>

//Handler for stdout
static int stdout_write(const void *buf, sz count, sz *written) {
    const char *str = (const char*)buf;
    for (sz i = 0; i < count; i++) {
        terminal_putchar(str[i]);
    }
    *written = count;
    return 0;
}

//Handler for stdin
static int stdin_read(void *buf, sz count, sz *read) {
    char *str = (char*)buf;
    sz i = 0;
    
    while (i < count) {
        char c = kbd_getchar();
        if (c != -1) {
            str[i++] = c;
            if (c == '\n') break;
        } else {
            for (volatile int j = 0; j < 1000; j++);
        }
    }
    
    *read = i;
    return 0;
}

//Handler for stderr
static int stderr_write(const void *buf, sz count, sz *written) {
    const char *str = (const char*)buf;
    
    //Temporary buffer for copying (since we have no guarantee that the string is null-terminated)
    char temp[1024];
    sz copy_len = count;
    if (copy_len > sizeof(temp) - 1) 
        copy_len = sizeof(temp) - 1;
    
    memcpy(temp, str, copy_len);
    temp[copy_len] = '\0';
    
    //Output via tio_printerr (red)
    terminal_error_printf("%s", temp);
    
    *written = count;
    return 0;
}

//Drivers
static device_driver_t stdin_driver = {
    .name = "stdin",
    .read = stdin_read,
    .write = NULL,
    .ioctl = NULL
};

static device_driver_t stdout_driver = {
    .name = "stdout",
    .read = NULL,
    .write = stdout_write,
    .ioctl = NULL
};

static device_driver_t stderr_driver = {
    .name = "stderr",
    .read = NULL,
    .write = stderr_write,
    .ioctl = NULL
};

//Initializing std devices in the specified directory
void devfs_init_std(vfs_inode_t *dir) {
    devfs_register_driver(&stdin_driver);
    devfs_register_driver(&stdout_driver);
    devfs_register_driver(&stderr_driver);
    
    devfs_mknod_in(dir, "stdin", FT_CHRDEV, &stdin_driver, NULL);
    devfs_mknod_in(dir, "stdout", FT_CHRDEV, &stdout_driver, NULL);
    devfs_mknod_in(dir, "stderr", FT_CHRDEV, &stderr_driver, NULL);
}
