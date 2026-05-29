#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SYS_GET_PROCESS_INFO   548
#define SYS_GET_SYSTEM_MONITOR 549
#define TOP_PROCS 10

struct process_info {
    int pid;
    char name[16];
    long cpu_time;
    long mem_kb;
};

struct top_proc {
    int pid;
    char name[16];
    unsigned long mem_kb;
    unsigned int mem_pct_x100;
};

struct system_monitor_info {
    unsigned long long memoria_total;
    unsigned long long memoria_usada;
    unsigned long long memoria_libre;
    unsigned long long memoria_cache;
    unsigned long long swap_total;
    unsigned long long swap_usada;
    unsigned long long fallos_menores;
    unsigned long long fallos_mayores;
    unsigned long long paginas_activas;
    unsigned long long paginas_inactivas;
    struct top_proc top_processes[TOP_PROCS];
    int top_count;
};

int main() {
    // --- Test sys_get_process_info ---
    struct process_info pinfo;
    int pid = getpid();
    long ret = syscall(SYS_GET_PROCESS_INFO, pid, &pinfo);
    if (ret == 0) {
        printf("=== sys_get_process_info ===\n");
        printf("PID     : %d\n", pinfo.pid);
        printf("Nombre  : %s\n", pinfo.name);
        printf("CPU time: %ld\n", pinfo.cpu_time);
        printf("Memoria : %ld KB\n\n", pinfo.mem_kb);
    } else {
        perror("sys_get_process_info fallo");
    }

    // --- Test sys_get_system_monitor ---
    struct system_monitor_info sinfo;
    ret = syscall(SYS_GET_SYSTEM_MONITOR, &sinfo);
    if (ret == 0) {
        printf("=== sys_get_system_monitor ===\n");
        printf("Memoria total    : %llu MB\n", sinfo.memoria_total / 1024);
        printf("Memoria usada    : %llu MB\n", sinfo.memoria_usada / 1024);
        printf("Memoria libre    : %llu MB\n", sinfo.memoria_libre / 1024);
        printf("Memoria cache    : %llu MB\n", sinfo.memoria_cache / 1024);
        printf("Swap total       : %llu MB\n", sinfo.swap_total / 1024);
        printf("Swap usada       : %llu MB\n", sinfo.swap_usada / 1024);
        printf("Fallos menores   : %llu\n", sinfo.fallos_menores);
        printf("Fallos mayores   : %llu\n", sinfo.fallos_mayores);
        printf("Paginas activas  : %llu\n", sinfo.paginas_activas);
        printf("Paginas inactivas: %llu\n\n", sinfo.paginas_inactivas);

        printf("=== TOP %d PROCESOS ===\n", sinfo.top_count);
        printf("%-6s %-16s %10s %8s\n", "PID", "NOMBRE", "MEM(KB)", "%MEM");
        for (int i = 0; i < sinfo.top_count; i++) {
            struct top_proc *p = &sinfo.top_processes[i];
            printf("%-6d %-16s %10lu %5u.%02u%%\n",
                p->pid, p->name, p->mem_kb,
                p->mem_pct_x100 / 100, p->mem_pct_x100 % 100);
        }
    } else {
        perror("sys_get_system_monitor fallo");
    }

    return 0;
}
