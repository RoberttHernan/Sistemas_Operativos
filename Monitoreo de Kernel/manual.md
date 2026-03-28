# Manual Técnico – Práctica 6
## Monitoreo de procesos en tiempo real mediante syscalls y visualización gráfica

**Curso:** Sistemas Operativos 2  
**Universidad:** Universidad de San Carlos de Guatemala – Facultad de Ingeniería  
**Kernel:** Linux 6.12.69 LTS  

---

## 1. Arquitectura del sistema

El sistema se compone de tres capas que se comunican entre sí:

- Maquina Virtual
- - KERNEL LINUX 6.12.69 (modificado)       
- - sys_get_system_monitor (syscall 549)  – Práctica 6
- syscall
- - monitor_daemon  (C)    – invoca syscalls   
- - query_process   (C)    – helper para /api/process
- HTTP POST /api/metrics   
- - backend.py  (Python)  – API HTTP en 0.0.0.0:8080
- MÁQUINA HOST                      
- -  dashboard.html (navegador)        
- - 5 gráficas con Chart.js         
- - consulta por PID   



---

## 2. Llamadas al sistema implementadas

### 2.1 `sys_get_process_info` (syscall 548) — Práctica 5

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

### 2.2 `sys_get_system_monitor` (syscall 549) — Práctica 6

Retorna métricas globales de memoria del sistema y la lista de los 10 procesos
con mayor consumo de RAM.

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

| Campo | Fuente en el kernel |
|---|---|
| `mem_total_kb` | `si_meminfo()` → `si.totalram * PAGE_SIZE / 1024` |
| `mem_free_kb` | `si.freeram * PAGE_SIZE / 1024` |
| `mem_cache_kb` | `global_node_page_state(NR_FILE_PAGES) − buffers − swap-cache` |
| `mem_used_kb` | `total − free − cache − buffers` |
| `swap_total_kb` | `si_swapinfo()` → `si.totalswap * PAGE_SIZE / 1024` |
| `swap_used_kb` | `(si.totalswap − si.freeswap) * PAGE_SIZE / 1024` |
| `minor_faults` | `all_vm_events(events)[PGFAULT] − [PGMAJFAULT]` |
| `major_faults` | `all_vm_events(events)[PGMAJFAULT]` |
| `pages_active` | `NR_ACTIVE_ANON + NR_ACTIVE_FILE` (node page state) |
| `pages_inactive` | `NR_INACTIVE_ANON + NR_INACTIVE_FILE` |
| `top_procs` | Iteración con `for_each_process()` bajo RCU lock, selección top-K |

**Algoritmo de selección top-K procesos:**  
Se mantiene un arreglo fijo de `MAX_TOP_PROCS` entradas. Al recorrer todos los
procesos, si el arreglo no está lleno se inserta directamente; si está lleno,
se busca el proceso con menor porcentaje y se reemplaza si el candidato actual
es mayor. Complejidad: O(N × K) donde N = número de procesos y K = 10.

---

## 3. Archivos modificados en el kernel

| Archivo | Cambio |
|---|---|
| `arch/x86/entry/syscalls/syscall_64.tbl` | Línea: `549 common get_system_monitor sys_get_system_monitor` |
| `include/linux/syscalls.h` | Prototipo: `asmlinkage long sys_get_system_monitor(void __user *info);` |
| `kernel/Makefile` | Agregar `obj-y += get_system_monitor/` |
| `kernel/get_system_monitor/get_system_monitor.c` | Archivo nuevo con la implementación |
| `kernel/get_system_monitor/Makefile` | `obj-y := get_system_monitor.o` |

---

## 4. Flujo de datos

```
Kernel (syscall 549)
  │  struct sys_monitor_info
  ▼
monitor_daemon.c (C, user-space)
  │  serialización manual a JSON
  │  HTTP POST cada 3 segundos
  ▼
backend.py (Python, puerto 8080)
  │  almacena en latest_metrics{}
  │  sirve GET /api/metrics
  ▼
dashboard.html (navegador, polling cada 3 s)
  │  Chart.js actualiza 4 gráficas + 1 tabla
  │
  ├─ Consulta PID → GET /api/process/<pid>
  │       ▼
  │  backend.py ejecuta ./query_process <pid>
  │       ▼
  │  query_process.c invoca syscall 548
  └────────────────────────────────────────
```

---

## 5. Compilación

### 5.1 Kernel

```bash
# Desde el directorio raíz del kernel:
make localmodconfig      # reutilizar configuración actual
make -j$(nproc)          # compilar (tarda ~15-30 min)
sudo make modules_install
sudo make install
sudo update-grub
sudo reboot
```

### 5.2 Programas de usuario

```bash
# Instalar dependencia
sudo apt install libcurl4-openssl-dev

# Compilar daemon
gcc monitor_daemon.c -o monitor_daemon -lcurl

# Compilar helper de consulta de PID
gcc query_process.c -o query_process
```

### 5.3 Backend

```bash
# Sin dependencias externas, sólo Python 3.6+
python3 backend.py
```

---

## 6. Ejecución del sistema completo

En la **máquina virtual** (tres terminales separadas):

```bash
# Terminal 1 – backend
python3 backend.py

# Terminal 2 – daemon (requiere kernel compilado)
sudo ./monitor_daemon

# Opcional: verificar syscall manualmente
sudo ./query_process 1
```

En la **máquina host**, abrir en el navegador:

```
http://<IP_VM>:8080/
```

Para conocer la IP de la VM:
```bash
ip addr show | grep "inet " | grep -v "127.0.0.1"
```

Si usas VirtualBox con NAT, configurar **Port Forwarding**:
- Host Port: 8080 → Guest Port: 8080

---

## 7. Pruebas realizadas

### 7.1 Validación de `sys_get_system_monitor`

```bash
# Verificar que la syscall retorna sin error
strace -e trace=549 ./monitor_daemon
```

Salida esperada del daemon:
```json
{"mem_total_kb":4096000,"mem_used_kb":1200000,"mem_free_kb":1800000,
 "mem_cache_kb":900000,"swap_total_kb":2048000,"swap_used_kb":0,
 "minor_faults":384521,"major_faults":12,"pages_active":280000,
 "pages_inactive":170000,
 "procesos_top":[{"pid":1,"name":"systemd","mem_percent":0.80}, ...]}
```

### 7.2 Validación de `sys_get_process_info` (reutilización P5)

```bash
sudo ./query_process 1       # systemd
sudo ./query_process $$      # shell actual
sudo ./query_process 2       # kthreadd
```

### 7.3 Validación del dashboard

- Abrir `http://localhost:8080/` en el navegador del host.
- Las 5 gráficas se actualizan automáticamente cada 3 segundos.
- Ingresar PID en el campo "Consultar proceso" y verificar que retorna datos.
- Clic en botón "Detalle" en la tabla de procesos muestra información del PID correspondiente.

---

## 8. Decisiones de diseño

**¿Por qué `mem_percent_x100` como entero?**  
Evitar aritmética de punto flotante en el kernel, que está prohibida en código
de kernel de Linux. El valor se convierte a `float` en el daemon antes de
serializar a JSON.

**¿Por qué polling en lugar de WebSocket?**  
El polling HTTP con intervalo de 3 segundos es suficiente para el propósito de
monitoreo y elimina la necesidad de instalar bibliotecas adicionales. La
latencia de 3 s es aceptable para métricas del sistema que cambian gradualmente.

**¿Por qué `query_process` como proceso separado?**  
El backend es Python y no puede invocar syscalls personalizadas directamente.
Delegar la llamada a un binario C compilado es la solución más limpia y reutiliza
exactamente el mismo código que la syscall de Práctica 5.

**Manejo de errores en el kernel:**  
- Se verifica que `user_info != NULL` antes de operar (`-EINVAL`).
- `get_mm_rss()` se llama solo cuando `task->mm != NULL`.
- `copy_to_user()` retorna `-EFAULT` si el puntero de usuario es inválido.
- La iteración de procesos ocurre bajo `rcu_read_lock()` para garantizar consistencia.

---

## 9. Observaciones y limitaciones

- Los contadores de fallos de página (`minor_faults`, `major_faults`) son
  **acumulados desde el arranque del sistema**. El dashboard calcula el
  **delta por intervalo** para mostrar la tasa real.
- `mem_cache_kb` puede ser negativo si el sistema tiene poca memoria libre;
  el código lo limita a 0 en ese caso.
- El algoritmo top-K no produce una lista ordenada; el dashboard ordena por
  porcentaje en el lado del cliente.
- En sistemas con `CONFIG_VM_EVENT_COUNTERS` deshabilitado, los fallos de
  página retornarán 0. Esto es infrecuente en kernels de escritorio.

---

