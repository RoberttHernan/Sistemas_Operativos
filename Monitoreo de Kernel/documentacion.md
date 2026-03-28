# Práctica 6 — Monitoreo de Procesos en Tiempo Real mediante Syscalls y Visualización Gráfica

## Enunciado

> **Objetivo de la práctica:** Construir un sistema completo de monitoreo del kernel de Linux que consista en tres capas: (1) una nueva syscall en el kernel (`sys_get_system_monitor`, número 549) que exponga métricas globales de memoria y los 10 procesos con mayor consumo de RAM; (2) un daemon en C que invoque la syscall periódicamente y envíe los datos via HTTP a un backend; y (3) un dashboard web en HTML/JavaScript que visualice las métricas en tiempo real con gráficas actualizadas automáticamente. La práctica reutiliza además la syscall `sys_get_process_info` (548) de la Práctica 5 para consultar información de procesos individuales desde el frontend.

**Tareas requeridas:**

1. Implementar `sys_get_system_monitor` (syscall 549) en el kernel que retorne: memoria total/usada/libre/caché, swap, fallos de página y top-10 procesos por RAM.
2. Registrar la nueva syscall en `syscall_64.tbl` e `include/linux/syscalls.h`.
3. Compilar e instalar el kernel modificado en la máquina virtual.
4. Implementar `monitor_daemon.c` (C + libcurl): invoca la syscall cada 3 s y hace POST JSON al backend.
5. Implementar `backend.py` (Python 3): servidor HTTP que recibe métricas del daemon y las sirve al frontend.
6. Implementar `dashboard.html`: dashboard con 5 gráficas (Chart.js) que hace polling cada 3 s y permite consultar PIDs individuales.
7. Verificar el sistema completo: syscall → daemon → backend → dashboard.

---

## Tabla de Contenidos

1. [Descripción General](#1-descripción-general)
2. [Arquitectura del Sistema](#2-arquitectura-del-sistema)
3. [Llamadas al Sistema Implementadas](#3-llamadas-al-sistema-implementadas)
4. [Archivos Modificados en el Kernel](#4-archivos-modificados-en-el-kernel)
5. [Componentes de Usuario](#5-componentes-de-usuario)
6. [Proceso de Compilación del Kernel](#6-proceso-de-compilación-del-kernel)
7. [Compilación de Programas de Usuario](#7-compilación-de-programas-de-usuario)
8. [Ejecución del Sistema Completo](#8-ejecución-del-sistema-completo)
9. [Pruebas y Validación](#9-pruebas-y-validación)
10. [Decisiones de Diseño](#10-decisiones-de-diseño)
11. [Observaciones y Limitaciones](#11-observaciones-y-limitaciones)

---

## 1. Descripción General

Esta práctica construye un sistema de monitoreo de tres capas que se comunican entre sí:

- **Capa de kernel:** syscall personalizada `sys_get_system_monitor` (549) que accede directamente a las estructuras internas del kernel para obtener métricas de memoria y procesos.
- **Capa de middleware:** daemon en C (`monitor.c`) que invoca la syscall periódicamente y reenvía los datos al backend; programa auxiliar `query_process.c` que invoca la syscall 548 para consultas individuales de PID.
- **Capa de presentación:** servidor HTTP ligero en Python (`backend.py`) que almacena las métricas y las sirve al dashboard web (`dashboard.html`) con Chart.js.

---

## 2. Arquitectura del Sistema

```
┌────────────────────────────────────────────────────────┐
│                  MÁQUINA VIRTUAL                       │
│                                                        │
│  ┌──────────────────────────────────────────────────┐  │
│  │            KERNEL LINUX 6.12.69 (modificado)     │  │
│  │   sys_get_system_monitor  (syscall 549) — P6     │  │
│  │   sys_get_process_info    (syscall 548) — P5     │  │
│  └──────────────────────┬───────────────────────────┘  │
│                         │ syscall                       │
│  ┌──────────────────────▼───────────────────────────┐  │
│  │  monitor_daemon (C)  — invoca syscall 549        │  │
│  │  query_process  (C)  — invoca syscall 548        │  │
│  └──────────────────────┬───────────────────────────┘  │
│                         │ HTTP POST /api/metrics        │
│  ┌──────────────────────▼───────────────────────────┐  │
│  │  backend.py (Python) — API HTTP en 0.0.0.0:8080  │  │
│  └──────────────────────────────────────────────────┘  │
│                                                        │
└────────────────────────────┬───────────────────────────┘
                             │ HTTP GET /api/metrics
┌────────────────────────────▼───────────────────────────┐
│                  MÁQUINA HOST                          │
│  dashboard.html (navegador)                            │
│  ├── 5 gráficas con Chart.js (polling cada 3 s)        │
│  └── consulta por PID → GET /api/process/<pid>         │
└────────────────────────────────────────────────────────┘
```

---

## 3. Llamadas al Sistema Implementadas

### 3.1 `sys_get_process_info` (syscall 548) — Práctica 5

Retorna información detallada de un proceso específico identificado por su PID.

**Prototipo:**
```c
long sys_get_process_info(pid_t pid, struct process_info __user *info);
```

**Estructura retornada:**
```c
struct process_info {
    int                pid;
    char               name[16];
    unsigned long long cpu_time_sec;
    unsigned long      mem_kb;
};
```

**Mecanismo interno:**
- `find_task_by_vpid(pid)` — localiza el `task_struct` bajo RCU lock.
- `get_task_comm()` — obtiene el nombre del proceso de forma segura.
- `task->utime + task->stime` convertido a segundos mediante `jiffies`.
- `get_mm_rss(task->mm) * PAGE_SIZE / 1024` — memoria residente en KB.

---

### 3.2 `sys_get_system_monitor` (syscall 549) — Práctica 6

Retorna métricas globales de memoria del sistema y la lista de los 10 procesos con mayor consumo de RAM.

**Prototipo:**
```c
long sys_get_system_monitor(struct sys_monitor_info __user *info);
```

**Estructuras:**
```c
#define MAX_TOP_PROCS 10

struct sys_proc_mem_info {
    pid_t         pid;
    char          name[16];
    unsigned long mem_percent_x100;   /* porcentaje × 100 */
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
```

**Mecanismo interno por campo:**

| Campo            | Fuente en el kernel                                                          |
|------------------|------------------------------------------------------------------------------|
| `mem_total_kb`   | `si_meminfo()` → `si.totalram * PAGE_SIZE / 1024`                           |
| `mem_free_kb`    | `si.freeram * PAGE_SIZE / 1024`                                              |
| `mem_cache_kb`   | `global_node_page_state(NR_FILE_PAGES) − buffers − swap-cache`              |
| `mem_used_kb`    | `total − free − cache − buffers`                                             |
| `swap_total_kb`  | `si_swapinfo()` → `si.totalswap * PAGE_SIZE / 1024`                         |
| `swap_used_kb`   | `(si.totalswap − si.freeswap) * PAGE_SIZE / 1024`                           |
| `minor_faults`   | `all_vm_events(events)[PGFAULT] − [PGMAJFAULT]`                             |
| `major_faults`   | `all_vm_events(events)[PGMAJFAULT]`                                          |
| `pages_active`   | `NR_ACTIVE_ANON + NR_ACTIVE_FILE` (node page state)                         |
| `pages_inactive` | `NR_INACTIVE_ANON + NR_INACTIVE_FILE`                                        |
| `top_procs`      | Iteración con `for_each_process()` bajo RCU lock, selección top-K           |

**Algoritmo de selección top-K procesos:**
Se mantiene un arreglo fijo de `MAX_TOP_PROCS` entradas. Al recorrer todos los procesos, si el arreglo no está lleno se inserta directamente; si está lleno, se busca el proceso con menor porcentaje y se reemplaza si el candidato actual es mayor. Complejidad: O(N × K) donde N = número de procesos y K = 10.

---

## 4. Archivos Modificados en el Kernel

| Archivo | Cambio |
|---------|--------|
| `arch/x86/entry/syscalls/syscall_64.tbl` | Línea: `549 common get_system_monitor sys_get_system_monitor` |
| `include/linux/syscalls.h` | Prototipo: `asmlinkage long sys_get_system_monitor(void __user *info);` |
| `kernel/Makefile` | Agregar `obj-y += get_system_monitor/` |
| `kernel/get_system_monitor/get_system_monitor.c` | Archivo nuevo con la implementación |
| `kernel/get_system_monitor/Makefile` | `obj-y := get_system_monitor.o` |

---

## 5. Componentes de Usuario

### 5.1 `monitor.c` — Daemon de monitoreo

Invoca `sys_get_system_monitor` (549) cada 3 segundos, serializa el resultado a JSON y lo envía mediante HTTP POST al backend con libcurl.

**Archivo:** [`monitor.c`](./monitor.c)

```bash
# Compilar
gcc monitor.c -o monitor_daemon -lcurl

# Ejecutar (requiere kernel con syscall 549)
sudo ./monitor_daemon
```

### 5.2 `query_process.c` — Consulta individual de PID

Invoca `sys_get_process_info` (548) para un PID dado, serializa la respuesta a JSON en stdout. Es ejecutado por el backend cuando el frontend solicita información de un proceso específico.

**Archivo:** [`query_process.c`](./query_process.c)

```bash
# Compilar
gcc query_process.c -o query_process

# Uso
./query_process 1        # información de PID 1 (systemd)
./query_process 1234     # información de cualquier PID
```

### 5.3 `backend.py` — Servidor HTTP

Servidor HTTP ligero en Python 3 (sin dependencias externas) que expone tres endpoints:

| Endpoint              | Método | Descripción                                              |
|-----------------------|--------|----------------------------------------------------------|
| `/`                   | GET    | Sirve `dashboard.html`                                   |
| `/api/metrics`        | POST   | Recibe JSON de métricas del daemon                       |
| `/api/metrics`        | GET    | Retorna las últimas métricas al frontend                 |
| `/api/process/<pid>`  | GET    | Ejecuta `query_process` y retorna info del PID           |

**Archivo:** [`backend.py`](./backend.py)

```bash
python3 backend.py
```

### 5.4 `dashboard.html` — Dashboard web

Interfaz web que hace polling a `/api/metrics` cada 3 segundos y actualiza 5 gráficas con Chart.js:

1. **Memoria RAM** — gráfica de dona (usada / libre / caché)
2. **Swap** — gráfica de dona (usado / libre)
3. **Fallos de página** — gráfica de barras (minor / major por intervalo)
4. **Páginas de memoria** — gráfica de líneas (activas / inactivas)
5. **Top 10 procesos por RAM** — tabla ordenable con botón de detalle por PID

**Archivo:** [`dashboard.html`](./dashboard.html)

---

## 6. Proceso de Compilación del Kernel

```bash
# Paso 1: Descargar fuentes
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.69.tar.xz
tar -xf linux-6.12.69.tar.xz
cd linux-6.12.69

# Paso 2: Configurar (solo módulos activos — recomendado en VM)
make localmodconfig

# Paso 3: Deshabilitar firma de módulos
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS

# Paso 4: Compilar
make -j$(nproc)
# Tiempo estimado: 15-30 min con localmodconfig

# Paso 5: Instalar
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

En el menú de GRUB seleccionar `Advanced options → linux-6.12.69`. Verificar:

```bash
uname -r
# Salida esperada: 6.12.69
```

---

## 7. Compilación de Programas de Usuario

```bash
# Dependencia para el daemon
sudo apt install libcurl4-openssl-dev

# Compilar daemon
gcc monitor.c -o monitor_daemon -lcurl

# Compilar helper de consulta de PID
gcc query_process.c -o query_process
```

---

## 8. Ejecución del Sistema Completo

En la **máquina virtual** (tres terminales separadas):

```bash
# Terminal 1 – backend
python3 backend.py

# Terminal 2 – daemon (requiere kernel compilado con syscall 549)
sudo ./monitor_daemon

# Opcional: verificar syscall manualmente
./query_process 1
```

En la **máquina host**, abrir en el navegador:
```
http://<IP_VM>:8080/
```

Para encontrar la IP de la VM:
```bash
ip addr show | grep "inet " | grep -v "127.0.0.1"
```

> Si se usa VirtualBox con NAT, configurar **Port Forwarding**: Host Port 8080 → Guest Port 8080.

---

## 9. Pruebas y Validación

### 9.1 Validación de `sys_get_system_monitor`

```bash
# Trazar la syscall con strace
strace -e trace=549 ./monitor_daemon
```

Salida esperada del daemon (JSON enviado al backend):
```json
{
  "mem_total_kb": 4096000,
  "mem_used_kb": 1200000,
  "mem_free_kb": 1800000,
  "mem_cache_kb": 900000,
  "swap_total_kb": 2048000,
  "swap_used_kb": 0,
  "minor_faults": 384521,
  "major_faults": 12,
  "pages_active": 280000,
  "pages_inactive": 170000,
  "procesos_top": [
    {"pid": 1, "name": "systemd", "mem_percent": 0.80}
  ]
}
```

### 9.2 Validación de `sys_get_process_info` (reutilización P5)

```bash
./query_process 1        # systemd
./query_process $$       # shell actual
./query_process 2        # kthreadd
```

### 9.3 Validación del dashboard

- Abrir `http://localhost:8080/` en el navegador del host.
- Las 5 gráficas se actualizan automáticamente cada 3 segundos.
- Ingresar PID en el campo "Consultar proceso" y verificar que retorna datos.
- El botón "Detalle" en la tabla de procesos muestra información del PID correspondiente.

### 9.4 Prueba con PID inválido

```bash
./query_process 999999
# Salida JSON: {"error": "No such process"}
```

---

## 10. Decisiones de Diseño

**¿Por qué `mem_percent_x100` como entero?**
Evitar aritmética de punto flotante en el kernel, que está prohibida en código de kernel de Linux. El valor se convierte a `float` en el daemon antes de serializar a JSON.

**¿Por qué polling en lugar de WebSocket?**
El polling HTTP con intervalo de 3 segundos es suficiente para el propósito de monitoreo y elimina la necesidad de instalar bibliotecas adicionales. La latencia de 3 s es aceptable para métricas del sistema que cambian gradualmente.

**¿Por qué `query_process` como proceso separado?**
El backend es Python y no puede invocar syscalls personalizadas directamente. Delegar la llamada a un binario C compilado es la solución más limpia y reutiliza exactamente el mismo código que la syscall de Práctica 5.

**Manejo de errores en el kernel:**
- Se verifica que `user_info != NULL` antes de operar (`-EINVAL`).
- `get_mm_rss()` se llama solo cuando `task->mm != NULL`.
- `copy_to_user()` retorna `-EFAULT` si el puntero de usuario es inválido.
- La iteración de procesos ocurre bajo `rcu_read_lock()` para garantizar consistencia.

---

## 11. Observaciones y Limitaciones

- Los contadores de fallos de página (`minor_faults`, `major_faults`) son **acumulados desde el arranque del sistema**. El dashboard calcula el **delta por intervalo** para mostrar la tasa real.
- `mem_cache_kb` puede ser negativo si el sistema tiene poca memoria libre; el código lo limita a 0 en ese caso.
- El algoritmo top-K no produce una lista ordenada; el dashboard ordena por porcentaje en el lado del cliente.
- En sistemas con `CONFIG_VM_EVENT_COUNTERS` deshabilitado, los fallos de página retornarán 0. Esto es infrecuente en kernels de escritorio.

---

## Referencias

- Robert Love. *Linux Kernel Development*, 3rd Edition. Addison-Wesley, 2010.
- Daniel P. Bovet & Marco Cesati. *Understanding the Linux Kernel*, 3rd Edition. O'Reilly Media, 2005.
- The Linux Kernel documentation. <https://www.kernel.org/doc/html/latest/>
- Adding a New System Call. <https://www.kernel.org/doc/html/latest/process/adding-syscalls.html>
- Kernel memory management. <https://www.kernel.org/doc/html/latest/admin-guide/mm/index.html>
- libcurl documentation. <https://curl.se/libcurl/c/>
