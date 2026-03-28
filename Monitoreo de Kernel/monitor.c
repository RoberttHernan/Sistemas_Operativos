/*
 * Práctica 6 – Sistemas Operativos 2
 * Archivo  : monitor_daemon.c
 * Función  : Daemon de monitoreo. Invoca sys_get_system_monitor (549)
 *            periódicamente y envía los datos en JSON al backend vía HTTP.
 *
 * Compilar : gcc monitor_daemon.c -o monitor_daemon -lcurl
 * Ejecutar : sudo ./monitor_daemon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <curl/curl.h>

/* ── Números de syscall ─────────────────────────────────────── */
#define SYS_GET_PROCESS_INFO    548
#define SYS_GET_SYSTEM_MONITOR  549

/* ── Intervalo de muestreo (segundos) ───────────────────────── */
#define INTERVAL_SEC  3

/* ── URL del backend ────────────────────────────────────────── */
#define BACKEND_URL "http://127.0.0.1:8080/api/metrics"

/* ── Definición de estructuras (deben coincidir con el kernel) ─ */

#define MAX_TOP_PROCS 10

struct sys_proc_mem_info {
    int           pid;
    char          name[16];
    unsigned long mem_percent_x100;
};

struct sys_monitor_info {
    unsigned long mem_total_kb;
    unsigned long mem_used_kb;
    unsigned long mem_free_kb;
    unsigned long mem_cache_kb;
    unsigned long swap_total_kb;
    unsigned long swap_used_kb;
    unsigned long minor_faults;
    unsigned long major_faults;
    unsigned long pages_active;
    unsigned long pages_inactive;
    int           num_procs;
    struct sys_proc_mem_info top_procs[MAX_TOP_PROCS];
};

/* ── Wrappers de syscall ────────────────────────────────────── */

static long call_get_system_monitor(struct sys_monitor_info *info)
{
    return syscall(SYS_GET_SYSTEM_MONITOR, info);
}

/* ── Construcción del JSON ──────────────────────────────────── */

/*
 * Escribe el JSON de métricas del sistema en `buf`.
 * Retorna el número de bytes escritos.
 */
static int build_json(const struct sys_monitor_info *m, char *buf, size_t bufsz)
{
    int  n = 0;
    int  i;

    n += snprintf(buf + n, bufsz - n,
        "{"
        "\"mem_total_kb\":%lu,"
        "\"mem_used_kb\":%lu,"
        "\"mem_free_kb\":%lu,"
        "\"mem_cache_kb\":%lu,"
        "\"swap_total_kb\":%lu,"
        "\"swap_used_kb\":%lu,"
        "\"minor_faults\":%lu,"
        "\"major_faults\":%lu,"
        "\"pages_active\":%lu,"
        "\"pages_inactive\":%lu,"
        "\"procesos_top\":[",
        m->mem_total_kb,
        m->mem_used_kb,
        m->mem_free_kb,
        m->mem_cache_kb,
        m->swap_total_kb,
        m->swap_used_kb,
        m->minor_faults,
        m->major_faults,
        m->pages_active,
        m->pages_inactive);

    for (i = 0; i < m->num_procs; i++) {
        const struct sys_proc_mem_info *p = &m->top_procs[i];
        /* Escapar comillas en el nombre (poco probable pero seguro) */
        char safe_name[32];
        int  si_n = 0;
        const char *src = p->name;
        while (*src && si_n < 30) {
            if (*src == '"') safe_name[si_n++] = '\\';
            safe_name[si_n++] = *src++;
        }
        safe_name[si_n] = '\0';

        n += snprintf(buf + n, bufsz - n,
            "%s{\"pid\":%d,\"name\":\"%s\",\"mem_percent\":%.2f}",
            (i > 0 ? "," : ""),
            p->pid,
            safe_name,
            p->mem_percent_x100 / 100.0);
    }

    n += snprintf(buf + n, bufsz - n, "]}");
    return n;
}

/* ── Envío HTTP con libcurl ─────────────────────────────────── */

static void post_json(const char *json)
{
    CURL                *curl;
    struct curl_slist   *headers = NULL;
    CURLcode             res;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[daemon] curl_easy_init() falló\n");
        return;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            BACKEND_URL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,     json);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,     headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    /* Silenciar salida de respuesta */
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     (size_t(*)(void*,size_t,size_t,void*)) NULL);

    res = curl_easy_perform(curl);
    if (res != CURLE_OK)
        fprintf(stderr, "[daemon] POST falló: %s\n", curl_easy_strerror(res));

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

/* ── Bucle principal ────────────────────────────────────────── */

int main(void)
{
    struct sys_monitor_info info;
    char   json_buf[4096];
    long   ret;

    printf("[daemon] Iniciando monitoreo (intervalo %d s) → %s\n",
           INTERVAL_SEC, BACKEND_URL);

    curl_global_init(CURL_GLOBAL_ALL);

    for (;;) {
        memset(&info, 0, sizeof(info));

        /* 1. Llamar a sys_get_system_monitor */
        ret = call_get_system_monitor(&info);
        if (ret < 0) {
            fprintf(stderr, "[daemon] sys_get_system_monitor error: %ld (errno=%d)\n",
                    ret, errno);
            sleep(INTERVAL_SEC);
            continue;
        }

        /* 2. Construir JSON */
        build_json(&info, json_buf, sizeof(json_buf));

        /* 3. Imprimir en consola (depuración) */
        printf("[daemon] %s\n", json_buf);

        /* 4. Enviar al backend */
        post_json(json_buf);

        sleep(INTERVAL_SEC);
    }

    curl_global_cleanup();
    return 0;
}
