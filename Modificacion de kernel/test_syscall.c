#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

/* Mismo número que en syscall_64.tbl */
#define SYS_GET_PROCESS_INFO 548

struct process_info {
    int   pid;
    char  name[16];
    long  runtime_sec;
    long  mem_kb;
};

void consultar_pid(pid_t pid) {
    struct process_info info;
    long ret;

    memset(&info, 0, sizeof(info));

    ret = syscall(SYS_GET_PROCESS_INFO, pid, &info);

    if (ret != 0) {
        printf("Error consultando PID %d: %s\n", pid, strerror(errno));
        return;
    }

    printf("╔══════════════════════════════╗\n");
    printf("║  PID          : %d\n",    info.pid);
    printf("║  Nombre       : %s\n",    info.name);
    printf("║  Tiempo CPU   : %ld seg\n", info.runtime_sec);
    printf("║  Memoria      : %ld KB\n",  info.mem_kb);
    printf("╚══════════════════════════════╝\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        /* Prueba automática con 3 PIDs conocidos */
        printf("=== Prueba 1: PID 1 (init/systemd) ===\n");
        consultar_pid(1);

        printf("=== Prueba 2: PID propio ===\n");
        consultar_pid(getpid());

        printf("=== Prueba 3: PID del padre ===\n");
        consultar_pid(getppid());
    } else {
        /* PID pasado por argumento */
        consultar_pid(atoi(argv[1]));
    }

    return 0;
}
