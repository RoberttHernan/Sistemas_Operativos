#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>

SYSCALL_DEFINE1(simulate_panic, const char __user *, umsg)
{
    char msg[256];

    if (!umsg) {
        printk(KERN_EMERG "[SECURITY] Simulated kernel panic triggered\n");
        return 0;
    }

    if (strncpy_from_user(msg, umsg, sizeof(msg)) < 0)
        return -EFAULT;

    msg[sizeof(msg) - 1] = '\0';

    printk(KERN_EMERG "[SECURITY] Simulated kernel panic: %s\n", msg);
    return 0;
}
