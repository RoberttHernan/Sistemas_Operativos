/*
 * Práctica 6 – Sistemas Operativos 2 – USAC
 * Archivo  : query_process.c
 * Función  : Consulta un PID específico usando sys_get_process_info (548)
 *            e imprime el resultado en JSON. El backend lo llama como subproceso.
 *
 * Compilar : gcc query_process.c -o query_process
 * Uso      : ./query_process <PID>
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#define SYS_GET_PROCESS_INFO 548

/* Debe coincidir exactamente con la struct de Práctica 5 */
struct process_info {
    int                pid;
    char               name[16];
    unsigned long long cpu_time_sec;
    unsigned long      mem_kb;
};

int main(int argc, char *argv[])
{
    struct process_info info;
    int   pid;
    long  ret;

    if (argc != 2) {
        printf("{\"error\":\"Uso: query_process <pid>\"}\n");
        return 1;
    }

    pid = atoi(argv[1]);
    if (pid <= 0) {
        printf("{\"error\":\"PID inválido\"}\n");
        return 1;
    }

    ret = syscall(SYS_GET_PROCESS_INFO, pid, &info);
    if (ret < 0) {
        printf("{\"error\":\"syscall falló\",\"errno\":%d,\"pid\":%d}\n",
               errno, pid);
        return 1;
    }

    printf("{\"pid\":%d,\"name\":\"%s\",\"cpu_time_sec\":%llu,\"mem_kb\":%lu}\n",
           info.pid, info.name, info.cpu_time_sec, info.mem_kb);

    return 0;
}
