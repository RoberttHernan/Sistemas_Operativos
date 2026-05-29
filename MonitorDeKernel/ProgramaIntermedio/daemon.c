#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <grp.h>
#include <pwd.h>
#include <microhttpd.h>
#include <cjson/cJSON.h>

/*  Números de syscall  */
#define SYS_GET_PROCESS_INFO    548
#define SYS_GET_SYSTEM_MONITOR  549
#define SYS_FILE_ANALIZE        550
#define SYS_SCAN_PROCESSES      551
#define SYS_QUARANTINE_FILE     552
#define SYS_RESTORE_FILE        553
#define SYS_GET_QUARANTINE_LIST 554
#define SYS_SIMULATE_PANIC      555

/*  Config  */
#define PORT            8080
#define MONITOR_DIR     "./monitor_dir"
#define BLACKLIST_FILE  "./hash_blacklist.json"
#define MAX_ALERTS      256
#define MAX_PROCS       512
#define MAX_QLIST       64
#define MAX_FILES       128
#define TOP_PROCS       10
#define MONITOR_INTERVAL 5
#define SCAN_INTERVAL    10

/*  Structs kernel  */
struct process_info {
    int  pid;
    char name[16];
    long cpu_time;
    long mem_kb;
};

struct top_proc {
    int  pid;
    char name[16];
    unsigned long mem_kb;
    unsigned int  mem_pct_x100;
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

/*  Structs internos  */
typedef enum { SEV_LOW, SEV_MEDIUM, SEV_HIGH } severity_t;

typedef struct {
    char      type[32];
    char      description[256];
    severity_t severity;
    time_t    timestamp;
} alert_t;

typedef struct {
    char path[256];
    char hash[65];
    long long last_modified;
    int  status; /* 0=limpio 1=modificado 2=sospechoso */
} file_record_t;

typedef struct {
    char hash[65];
    char name[64];
    severity_t severity;
    char description[128];
} blacklist_entry_t;

/*  Estado global  */
static alert_t        alerts[MAX_ALERTS];
static int            alert_count = 0;
static pthread_mutex_t alerts_mutex = PTHREAD_MUTEX_INITIALIZER;

static file_record_t  file_records[MAX_FILES];
static int            file_count = 0;
static pthread_mutex_t files_mutex = PTHREAD_MUTEX_INITIALIZER;

static blacklist_entry_t blacklist[32];
static int               blacklist_count = 0;

static struct system_monitor_info g_sysinfo;
static pthread_mutex_t sysinfo_mutex = PTHREAD_MUTEX_INITIALIZER;

static int scan_active = 1;
static pthread_mutex_t scan_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile int running = 1;

/*  Tokens de sesión simples  */
#define MAX_SESSIONS 16
typedef struct {
    char token[64];
    char username[64];
    int  is_admin;
    time_t created;
} session_t;
static session_t sessions[MAX_SESSIONS];
static int session_count = 0;
static pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;


/*  Generar token simple  */
static void gen_token(char *out, size_t len)
{
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    srand(time(NULL) ^ getpid());
    for (size_t i = 0; i < len - 1; i++)
        out[i] = chars[rand() % (sizeof(chars) - 1)];
    out[len - 1] = '\0';
}

/*  Agregar alerta  */
static void add_alert(const char *type, const char *desc, severity_t sev)
{
    pthread_mutex_lock(&alerts_mutex);
    if (alert_count < MAX_ALERTS) {
        strncpy(alerts[alert_count].type, type, 31);
        strncpy(alerts[alert_count].description, desc, 255);
        alerts[alert_count].severity  = sev;
        alerts[alert_count].timestamp = time(NULL);
        alert_count++;
    } else {
        /* Desplazar para mantener los más recientes */
        memmove(&alerts[0], &alerts[1], (MAX_ALERTS-1)*sizeof(alert_t));
        strncpy(alerts[MAX_ALERTS-1].type, type, 31);
        strncpy(alerts[MAX_ALERTS-1].description, desc, 255);
        alerts[MAX_ALERTS-1].severity  = sev;
        alerts[MAX_ALERTS-1].timestamp = time(NULL);
    }
    pthread_mutex_unlock(&alerts_mutex);

    /* Si es HIGH invocar simulate_panic */
    if (sev == SEV_HIGH)
        syscall(SYS_SIMULATE_PANIC, desc);
}

static const char *sev_str(severity_t s)
{
    switch(s) {
        case SEV_LOW:    return "LOW";
        case SEV_MEDIUM: return "MEDIUM";
        case SEV_HIGH:   return "HIGH";
    }
    return "LOW";
}

/*  Cargar blacklist  */
static void load_blacklist(void)
{
    FILE *f = fopen(BLACKLIST_FILE, "r");
    if (!f) { fprintf(stderr, "[daemon] No se encontro blacklist\n"); return; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { fprintf(stderr, "[daemon] Error parseando blacklist\n"); return; }

    cJSON *sigs = cJSON_GetObjectItem(root, "signatures");
    int n = cJSON_GetArraySize(sigs);
    blacklist_count = 0;

    for (int i = 0; i < n && i < 32; i++) {
        cJSON *e    = cJSON_GetArrayItem(sigs, i);
        cJSON *hash = cJSON_GetObjectItem(e, "hash");
        cJSON *name = cJSON_GetObjectItem(e, "name");
        cJSON *sev  = cJSON_GetObjectItem(e, "severity");
        cJSON *desc = cJSON_GetObjectItem(e, "description");

        if (!hash || !name || !sev || !desc) continue;

        strncpy(blacklist[blacklist_count].hash,        hash->valuestring, 64);
        strncpy(blacklist[blacklist_count].name,        name->valuestring, 63);
        strncpy(blacklist[blacklist_count].description, desc->valuestring, 127);

        if (strcmp(sev->valuestring, "HIGH") == 0)
            blacklist[blacklist_count].severity = SEV_HIGH;
        else if (strcmp(sev->valuestring, "MEDIUM") == 0)
            blacklist[blacklist_count].severity = SEV_MEDIUM;
        else
            blacklist[blacklist_count].severity = SEV_LOW;

        blacklist_count++;
    }
    cJSON_Delete(root);
    printf("[daemon] Blacklist cargada: %d firmas\n", blacklist_count);
}

/*  Buscar hash en blacklist  */
static blacklist_entry_t *check_blacklist(const char *hash)
{
    for (int i = 0; i < blacklist_count; i++)
        if (strcmp(blacklist[i].hash, hash) == 0)
            return &blacklist[i];
    return NULL;
}



/*  PAM  */
struct pam_creds { const char *user; const char *pass; };

static int pam_conv_fn(int num_msg, const struct pam_message **msg,
                       struct pam_response **resp, void *data)
{
    struct pam_creds *creds = data;
    *resp = calloc(num_msg, sizeof(struct pam_response));
    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
            (*resp)[i].resp = strdup(creds->pass);
    }
    return PAM_SUCCESS;
}

static int check_group(const char *username, const char *groupname)
{
    struct group *grp = getgrnam(groupname);
    if (!grp) return 0;
    for (int i = 0; grp->gr_mem[i]; i++)
        if (strcmp(grp->gr_mem[i], username) == 0) return 1;
    return 0;
}

/* Retorna: 2=admin, 1=user, 0=denegado */
static int authenticate(const char *username, const char *password)
{
    struct pam_creds creds = { username, password };
    struct pam_conv  conv  = { pam_conv_fn, &creds };
    pam_handle_t    *pamh  = NULL;
    int ret;

    ret = pam_start("login", username, &conv, &pamh);
    if (ret != PAM_SUCCESS) return 0;

    ret = pam_authenticate(pamh, 0);
    pam_end(pamh, ret);
    if (ret != PAM_SUCCESS) return 0;

    int is_admin = check_group(username, "admin_user");
    int is_user  = check_group(username, "common_user");

    if (is_admin) return 2;
    if (is_user)  return 1;
    return 0;
}



/*  Thread 1: Monitoreo  */
static void *thread_monitor(void *arg)
{
    (void)arg;
    int consecutive_critical = 0;
    unsigned long long prev_minor = 0, prev_major = 0;

    while (running) {
        struct system_monitor_info si;
        memset(&si, 0, sizeof(si));

        if (syscall(SYS_GET_SYSTEM_MONITOR, &si) == 0) {
            pthread_mutex_lock(&sysinfo_mutex);
            g_sysinfo = si;
            pthread_mutex_unlock(&sysinfo_mutex);

            /* Evaluar alertas de memoria */
            unsigned long long total = si.memoria_total;
            unsigned long long used  = si.memoria_usada;
            if (total > 0 && used * 100 / total > 80) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "Alto consumo de memoria: %llu MB / %llu MB",
                    used/1024, total/1024);
                add_alert("memoria", msg, SEV_MEDIUM);
                consecutive_critical++;
            } else {
                consecutive_critical = 0;
            }

            /* Page faults elevados */
            unsigned long long dm = si.fallos_mayores - prev_major;
            if (dm > 100) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                    "Page faults mayores elevados: %llu en intervalo", dm);
                add_alert("memoria", msg, SEV_MEDIUM);
            }
            prev_minor = si.fallos_menores;
            prev_major = si.fallos_mayores;

            /* Comportamiento anómalo persistente */
            if (consecutive_critical >= 3) {
                add_alert("sistema",
                    "Condicion critica persistente: alto consumo de memoria en multiples ciclos",
                    SEV_HIGH);
                consecutive_critical = 0;
            }

            /* Escaneo de procesos */
            struct process_scan_entry procs[MAX_PROCS];
            int n = syscall(SYS_SCAN_PROCESSES, procs, MAX_PROCS);
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    if (total > 0 && procs[i].mem_kb * 1024 * 100 / (total*1024) > 50) {
                        char msg[128];
                        snprintf(msg, sizeof(msg),
                            "Proceso %s (PID %d) consume >50%% de memoria (%lu KB)",
                            procs[i].name, procs[i].pid, procs[i].mem_kb);
                        add_alert("proceso", msg, SEV_HIGH);
                    }
                }
            }
        }
        sleep(MONITOR_INTERVAL);
    }
    return NULL;
}

/*  Thread 2: Escaneo de archivos  */
static void *thread_scanner(void *arg)
{
    (void)arg;

    while (running) {
        pthread_mutex_lock(&scan_mutex);
        int active = scan_active;
        pthread_mutex_unlock(&scan_mutex);

        if (!active) { sleep(2); continue; }

        DIR *dir = opendir(MONITOR_DIR);
        if (!dir) { sleep(SCAN_INTERVAL); continue; }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;

            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s",
                     MONITOR_DIR, ent->d_name);

            struct file_info fi;
            if (syscall(SYS_FILE_ANALIZE, full_path, &fi) != 0) continue;

            pthread_mutex_lock(&files_mutex);

            /* Buscar registro previo */
            int found = -1;
            for (int i = 0; i < file_count; i++) {
                if (strcmp(file_records[i].path, full_path) == 0) {
                    found = i; break;
                }
            }

            if (found < 0 && file_count < MAX_FILES) {
                /* Archivo nuevo */
                strncpy(file_records[file_count].path, full_path, 255);
                strncpy(file_records[file_count].hash, fi.sha256, 64);
                file_records[file_count].last_modified = fi.last_modified;
                file_records[file_count].status = 0;
                found = file_count++;
                pthread_mutex_unlock(&files_mutex);

                char msg[256];
                snprintf(msg, sizeof(msg), "Archivo nuevo detectado: %s", full_path);
                add_alert("archivo", msg, SEV_LOW);

            } else if (found >= 0) {
                int changed = strcmp(file_records[found].hash, fi.sha256) != 0;
                if (changed) {
                    strncpy(file_records[found].hash, fi.sha256, 64);
                    file_records[found].last_modified = fi.last_modified;
                    file_records[found].status = 1;
                    pthread_mutex_unlock(&files_mutex);

                    char msg[256];
                    snprintf(msg, sizeof(msg), "Archivo modificado: %s", full_path);
                    add_alert("archivo", msg, SEV_MEDIUM);
                } else {
                    pthread_mutex_unlock(&files_mutex);
                }
            } else {
                pthread_mutex_unlock(&files_mutex);
            }

            /* Verificar blacklist */
            blacklist_entry_t *bl = check_blacklist(fi.sha256);
            if (bl) {
                pthread_mutex_lock(&files_mutex);
                if (found >= 0) file_records[found].status = 2;
                pthread_mutex_unlock(&files_mutex);

                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Hash malicioso detectado: %s en %s (%s)",
                    bl->name, full_path, bl->description);
                add_alert("archivo", msg, bl->severity);

                /* Cuarentena automática */
                syscall(SYS_QUARANTINE_FILE, full_path);
            }
        }
        closedir(dir);
        sleep(SCAN_INTERVAL);
    }
    return NULL;
}



/*  Helpers JSON  */
static char *build_metrics_json(void)
{
    pthread_mutex_lock(&sysinfo_mutex);
    struct system_monitor_info s = g_sysinfo;
    pthread_mutex_unlock(&sysinfo_mutex);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "memoria_total",     (double)s.memoria_total);
    cJSON_AddNumberToObject(root, "memoria_usada",     (double)s.memoria_usada);
    cJSON_AddNumberToObject(root, "memoria_libre",     (double)s.memoria_libre);
    cJSON_AddNumberToObject(root, "memoria_cache",     (double)s.memoria_cache);
    cJSON_AddNumberToObject(root, "swap_total",        (double)s.swap_total);
    cJSON_AddNumberToObject(root, "swap_usada",        (double)s.swap_usada);
    cJSON_AddNumberToObject(root, "fallos_menores",    (double)s.fallos_menores);
    cJSON_AddNumberToObject(root, "fallos_mayores",    (double)s.fallos_mayores);
    cJSON_AddNumberToObject(root, "paginas_activas",   (double)s.paginas_activas);
    cJSON_AddNumberToObject(root, "paginas_inactivas", (double)s.paginas_inactivas);

    cJSON *arr = cJSON_AddArrayToObject(root, "procesos_top");
    for (int i = 0; i < s.top_count; i++) {
        cJSON *p = cJSON_CreateObject();
        cJSON_AddNumberToObject(p, "pid",     s.top_processes[i].pid);
        cJSON_AddStringToObject(p, "nombre",  s.top_processes[i].name);
        cJSON_AddNumberToObject(p, "mem_kb",  (double)s.top_processes[i].mem_kb);
        cJSON_AddNumberToObject(p, "mem_pct", (double)s.top_processes[i].mem_pct_x100 / 100.0);
        cJSON_AddItemToArray(arr, p);
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}

static char *build_alerts_json(void)
{
    pthread_mutex_lock(&alerts_mutex);
    cJSON *arr = cJSON_CreateArray();
    for (int i = alert_count - 1; i >= 0; i--) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "tipo",        alerts[i].type);
        cJSON_AddStringToObject(a, "descripcion", alerts[i].description);
        cJSON_AddStringToObject(a, "severidad",   sev_str(alerts[i].severity));
        cJSON_AddNumberToObject(a, "timestamp",   (double)alerts[i].timestamp);
        cJSON_AddItemToArray(arr, a);
    }
    pthread_mutex_unlock(&alerts_mutex);
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

static char *build_files_json(void)
{
    pthread_mutex_lock(&files_mutex);
    cJSON *arr = cJSON_CreateArray();
    const char *states[] = { "limpio", "modificado", "sospechoso" };
    for (int i = 0; i < file_count; i++) {
        cJSON *f = cJSON_CreateObject();
        cJSON_AddStringToObject(f, "path",   file_records[i].path);
        cJSON_AddStringToObject(f, "hash",   file_records[i].hash);
        cJSON_AddStringToObject(f, "estado", states[file_records[i].status]);
        cJSON_AddNumberToObject(f, "timestamp", (double)file_records[i].last_modified);
        cJSON_AddItemToArray(arr, f);
    }
    pthread_mutex_unlock(&files_mutex);
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}

/*  Validar token  */
static session_t *find_session(const char *token)
{
    if (!token) return NULL;
    pthread_mutex_lock(&sessions_mutex);
    for (int i = 0; i < session_count; i++) {
        if (strcmp(sessions[i].token, token) == 0) {
            pthread_mutex_unlock(&sessions_mutex);
            return &sessions[i];
        }
    }
    pthread_mutex_unlock(&sessions_mutex);
    return NULL;
}

static const char *get_header(struct MHD_Connection *con, const char *key)
{
    return MHD_lookup_connection_value(con, MHD_HEADER_KIND, key);
}

/*  HTTP handler  */
struct req_body { char *data; size_t size; };

static enum MHD_Result http_handler(void *cls,
    struct MHD_Connection *con,
    const char *url, const char *method,
    const char *version, const char *upload,
    size_t *upload_size, void **con_cls)
{
    (void)cls; (void)version;

    /* Acumular body POST */
    if (*con_cls == NULL) {
        struct req_body *rb = calloc(1, sizeof(*rb));
        *con_cls = rb;
        return MHD_YES;
    }
    struct req_body *rb = *con_cls;
    if (*upload_size > 0) {
        rb->data = realloc(rb->data, rb->size + *upload_size + 1);
        memcpy(rb->data + rb->size, upload, *upload_size);
        rb->size += *upload_size;
        rb->data[rb->size] = '\0';
        *upload_size = 0;
        return MHD_YES;
    }

    const char *ctype = "application/json";
    char *body = NULL;
    int   status = MHD_HTTP_OK;
    int   free_body = 1;


  /* OPTIONS — preflight CORS */
    if (strcmp(method, "OPTIONS") == 0) {
        struct MHD_Response *oresp = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
        MHD_add_response_header(oresp, "Access-Control-Allow-Origin",  "*");
        MHD_add_response_header(oresp, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        MHD_add_response_header(oresp, "Access-Control-Allow-Headers", "Authorization, Content-Type");
        MHD_add_response_header(oresp, "Access-Control-Max-Age",       "86400");
        enum MHD_Result ro = MHD_queue_response(con, MHD_HTTP_NO_CONTENT, oresp);
        MHD_destroy_response(oresp);
        if (rb) { free(rb->data); free(rb); *con_cls = NULL; }
        return ro;
    }

    /*  POST /login  */
    if (strcmp(method, "POST") == 0 && strcmp(url, "/login") == 0) {
        cJSON *req = rb->data ? cJSON_Parse(rb->data) : NULL;
        const char *user = NULL, *pass = NULL;
        if (req) {
            cJSON *u = cJSON_GetObjectItem(req, "username");
            cJSON *p = cJSON_GetObjectItem(req, "password");
            if (u) user = u->valuestring;
            if (p) pass = p->valuestring;
        }

        int role = (user && pass) ? authenticate(user, pass) : 0;
        cJSON *resp = cJSON_CreateObject();

        if (role > 0) {
            session_t ns;
            gen_token(ns.token, 32);
            strncpy(ns.username, user, 63);
            ns.is_admin = (role == 2);
            ns.created  = time(NULL);

            pthread_mutex_lock(&sessions_mutex);
            if (session_count < MAX_SESSIONS)
                sessions[session_count++] = ns;
            pthread_mutex_unlock(&sessions_mutex);

            cJSON_AddStringToObject(resp, "token",    ns.token);
            cJSON_AddStringToObject(resp, "rol",      ns.is_admin ? "admin" : "user");
            cJSON_AddStringToObject(resp, "username", user);
        } else {
            cJSON_AddStringToObject(resp, "error", "Credenciales invalidas o sin permisos");
            status = MHD_HTTP_UNAUTHORIZED;
        }
        if (req) cJSON_Delete(req);
        body = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        goto send;
    }

    /*  GET /metrics  */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/metrics") == 0) {
        const char *tok = get_header(con, "Authorization");
        if (!find_session(tok)) {
            body = strdup("{\"error\":\"no autorizado\"}");
            status = MHD_HTTP_UNAUTHORIZED; goto send;
        }
        body = build_metrics_json();
        goto send;
    }

    /*  GET /alerts  */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/alerts") == 0) {
        const char *tok = get_header(con, "Authorization");
        if (!find_session(tok)) {
            body = strdup("{\"error\":\"no autorizado\"}");
            status = MHD_HTTP_UNAUTHORIZED; goto send;
        }
        body = build_alerts_json();
        goto send;
    }

    /*  GET /files  */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/files") == 0) {
        const char *tok = get_header(con, "Authorization");
        if (!find_session(tok)) {
            body = strdup("{\"error\":\"no autorizado\"}");
            status = MHD_HTTP_UNAUTHORIZED; goto send;
        }
        body = build_files_json();
        goto send;
    }

    /*  GET /quarantine  */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/quarantine") == 0) {
        const char *tok = get_header(con, "Authorization");
        session_t *s = find_session(tok);
        if (!s || !s->is_admin) {
            body = strdup("{\"error\":\"solo administradores\"}");
            status = MHD_HTTP_FORBIDDEN; goto send;
        }
        struct quarantine_entry qlist[MAX_QLIST];
        int n = syscall(SYS_GET_QUARANTINE_LIST, qlist, MAX_QLIST);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < n; i++) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "path",      qlist[i].path);
            cJSON_AddNumberToObject(e, "timestamp", (double)qlist[i].timestamp);
            cJSON_AddItemToArray(arr, e);
        }
        body = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);
        goto send;
    }

    /*  GET /process/:pid  */
    if (strcmp(method, "GET") == 0 && strncmp(url, "/process/", 9) == 0) {
        const char *tok = get_header(con, "Authorization");
        session_t *s = find_session(tok);
        if (!s || !s->is_admin) {
            body = strdup("{\"error\":\"solo administradores\"}");
            status = MHD_HTTP_FORBIDDEN; goto send;
        }
        int pid = atoi(url + 9);
        struct process_info pi;
        cJSON *resp = cJSON_CreateObject();
        if (pid > 0 && syscall(SYS_GET_PROCESS_INFO, pid, &pi) == 0) {
            cJSON_AddNumberToObject(resp, "pid",      pi.pid);
            cJSON_AddStringToObject(resp, "nombre",   pi.name);
            cJSON_AddNumberToObject(resp, "cpu_time", (double)pi.cpu_time);
            cJSON_AddNumberToObject(resp, "mem_kb",   (double)pi.mem_kb);
        } else {
            cJSON_AddStringToObject(resp, "error", "proceso no encontrado");
            status = MHD_HTTP_NOT_FOUND;
        }
        body = cJSON_PrintUnformatted(resp);
        cJSON_Delete(resp);
        goto send;
    }


    /*  GET /threats  */
    if (strcmp(method, "GET") == 0 && strcmp(url, "/threats") == 0) {
        const char *tok = get_header(con, "Authorization");
        if (!find_session(tok)) {
            body = strdup("{\"error\":\"no autorizado\"}");
            status = MHD_HTTP_UNAUTHORIZED; goto send;
        }
        pthread_mutex_lock(&files_mutex);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < file_count; i++) {
            if (file_records[i].status != 2) continue;
            blacklist_entry_t *bl = check_blacklist(file_records[i].hash);
            cJSON *t = cJSON_CreateObject();
            cJSON_AddStringToObject(t, "archivo",     file_records[i].path);
            cJSON_AddStringToObject(t, "hash",        file_records[i].hash);
            cJSON_AddStringToObject(t, "nombre",      bl ? bl->name        : "Desconocido");
            cJSON_AddStringToObject(t, "severidad",   bl ? sev_str(bl->severity) : "HIGH");
            cJSON_AddStringToObject(t, "descripcion", bl ? bl->description : "Hash malicioso");
            cJSON_AddNumberToObject(t, "timestamp",   (double)file_records[i].last_modified);
            cJSON_AddItemToArray(arr, t);
        }
        pthread_mutex_unlock(&files_mutex);
        body = cJSON_PrintUnformatted(arr);
        cJSON_Delete(arr);
        goto send;
    }

    /*  POST /scan/start  */
    if (strcmp(method, "POST") == 0 && strcmp(url, "/scan/start") == 0) {
        const char *tok = get_header(con, "Authorization");
        session_t *s = find_session(tok);
        if (!s || !s->is_admin) {
            body = strdup("{\"error\":\"solo administradores\"}");
            status = MHD_HTTP_FORBIDDEN; goto send;
        }
        pthread_mutex_lock(&scan_mutex);
        scan_active = 1;
        pthread_mutex_unlock(&scan_mutex);
        body = strdup("{\"status\":\"escaneo activado\"}");
        goto send;
    }

    /*  POST /scan/stop  */
    if (strcmp(method, "POST") == 0 && strcmp(url, "/scan/stop") == 0) {
        const char *tok = get_header(con, "Authorization");
        session_t *s = find_session(tok);
        if (!s || !s->is_admin) {
            body = strdup("{\"error\":\"solo administradores\"}");
            status = MHD_HTTP_FORBIDDEN; goto send;
        }
        pthread_mutex_lock(&scan_mutex);
        scan_active = 0;
        pthread_mutex_unlock(&scan_mutex);
        body = strdup("{\"status\":\"escaneo desactivado\"}");
        goto send;
    }

    body = strdup("{\"error\":\"endpoint no encontrado\"}");
    status = MHD_HTTP_NOT_FOUND;

send:;
    if (!body) { body = strdup("{\"error\":\"internal\"}"); status = 500; }

    struct MHD_Response *resp = MHD_create_response_from_buffer(
        strlen(body), body, MHD_RESPMEM_MUST_COPY);
    if (free_body) free(body);
    MHD_add_response_header(resp, "Content-Type", ctype);
    MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
    MHD_add_response_header(resp, "Access-Control-Allow-Headers",
                            "Authorization, Content-Type");

    enum MHD_Result r = MHD_queue_response(con, status, resp);
    MHD_destroy_response(resp);

    if (rb) { free(rb->data); free(rb); *con_cls = NULL; }
    return r;
}

/*  Signal handler  */
static void sig_handler(int s) { (void)s; running = 0; }

/*  Main  */
int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    /* Crear directorio de monitoreo si no existe */
    mkdir(MONITOR_DIR, 0755);

    load_blacklist();

    /* Lanzar threads */
    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_monitor, NULL);
    pthread_create(&t2, NULL, thread_scanner, NULL);
    pthread_detach(t1);
    pthread_detach(t2);

    /* Iniciar HTTP */
    struct MHD_Daemon *d = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD,
        PORT, NULL, NULL,
        &http_handler, NULL,
        MHD_OPTION_END);

    if (!d) { fprintf(stderr, "[daemon] Error iniciando HTTP\n"); return 1; }

    printf("[daemon] Corriendo en http://0.0.0.0:%d\n", PORT);
    printf("[daemon] Directorio monitoreado: %s\n", MONITOR_DIR);
    printf("[daemon] Presiona Ctrl+C para detener\n");

    while (running) sleep(1);

    MHD_stop_daemon(d);
    printf("[daemon] Detenido.\n");
    return 0;
}
