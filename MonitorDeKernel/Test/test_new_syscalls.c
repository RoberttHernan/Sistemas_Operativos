#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>

#define SYS_FILE_ANALIZE        550
#define SYS_SCAN_PROCESSES      551
#define SYS_QUARANTINE_FILE     552
#define SYS_RESTORE_FILE        553
#define SYS_GET_QUARANTINE_LIST 554
#define SYS_SIMULATE_PANIC      555

#define MAX_PROCS 50
#define MAX_QLIST 64

struct file_info {
    long long size;
    long long last_modified;
    char sha256[65];
};

struct process_scan_entry {
    int  pid;
    char name[16];
    unsigned long mem_kb;
    unsigned long long cpu_time;
};

struct quarantine_entry {
    char path[256];
    long long timestamp;
};

int main(void)
{
    int ret, i;

    /* ---- 1. sys_file_analize ---- */
    printf("=== sys_file_analize ===\n");
    struct file_info finfo;
    ret = syscall(SYS_FILE_ANALIZE, "/etc/hostname", &finfo);
    if (ret == 0) {
        printf("Tamanio       : %lld bytes\n", finfo.size);
        printf("Ultima modif  : %lld\n", finfo.last_modified);
        printf("SHA-256       : %s\n\n", finfo.sha256);
    } else {
        perror("sys_file_analize fallo");
    }

    /* ---- 2. sys_scan_processes ---- */
    printf("=== sys_scan_processes (primeros 10) ===\n");
    struct process_scan_entry procs[MAX_PROCS];
    ret = syscall(SYS_SCAN_PROCESSES, procs, MAX_PROCS);
    if (ret > 0) {
        printf("%-6s %-16s %10s\n", "PID", "NOMBRE", "MEM(KB)");
        for (i = 0; i < ret && i < 10; i++)
            printf("%-6d %-16s %10lu\n",
                procs[i].pid, procs[i].name, procs[i].mem_kb);
        printf("Total procesos escaneados: %d\n\n", ret);
    } else {
        perror("sys_scan_processes fallo");
    }

    /* ---- 3. sys_quarantine_file ---- */
    printf("=== sys_quarantine_file ===\n");
    ret = syscall(SYS_QUARANTINE_FILE, "/etc/hostname");
    printf("Cuarentena /etc/hostname: %s\n", ret == 0 ? "OK" : "FALLO");

    ret = syscall(SYS_QUARANTINE_FILE, "/etc/hostname");
    printf("Duplicado (debe fallar) : %s (ret=%d)\n\n", ret != 0 ? "OK" : "FALLO", ret);

    /* ---- 4. sys_get_quarantine_list ---- */
    printf("=== sys_get_quarantine_list ===\n");
    struct quarantine_entry qlist[MAX_QLIST];
    ret = syscall(SYS_GET_QUARANTINE_LIST, qlist, MAX_QLIST);
    if (ret >= 0) {
        printf("Archivos en cuarentena: %d\n", ret);
        for (i = 0; i < ret; i++)
            printf("  [%d] %s (ts=%lld)\n", i+1, qlist[i].path, qlist[i].timestamp);
        printf("\n");
    } else {
        perror("sys_get_quarantine_list fallo");
    }

    /* ---- 5. sys_restore_file ---- */
    printf("=== sys_restore_file ===\n");
    ret = syscall(SYS_RESTORE_FILE, "/etc/hostname");
    printf("Restaurar /etc/hostname : %s\n", ret == 0 ? "OK" : "FALLO");

    ret = syscall(SYS_RESTORE_FILE, "/etc/hostname");
    printf("Restaurar no existe (debe fallar): %s (ret=%d)\n\n",
           ret != 0 ? "OK" : "FALLO", ret);

    /* ---- 6. sys_simulate_panic ---- */
    printf("=== sys_simulate_panic ===\n");
    ret = syscall(SYS_SIMULATE_PANIC, "TEST: simulacion desde userspace");
    printf("simulate_panic: %s\n", ret == 0 ? "OK (ver dmesg)" : "FALLO");
    printf("Verifica con: sudo dmesg | tail -5\n\n");

    return 0;
}
