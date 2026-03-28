# Sistemas Operativos 2 — Portafolio de Prácticas

Colección de prácticas de programación a nivel de kernel de Linux. Cada práctica construye sobre la anterior, progresando desde la configuración del entorno hasta la construcción de un sistema completo de monitoreo del kernel.

**Entorno:** Linux Mint · VirtualBox · Kernel 6.12.69 LTS · GCC · C / Python 3

---

## Índice de Prácticas

| # | Carpeta | Tema |
|---|---------|------|
| 2 | [`Modulos basicos de Kernel`](#práctica-2--módulos-básicos-de-kernel) | Primer módulo de kernel con `insmod`/`rmmod` |
| 3 | [`Verificacion de herramientes de compilacion`](#práctica-3--configuración-y-compilación-del-kernel) | Compilación e instalación de kernel personalizado |
| 4 | [`Modificacion de Funciones de Kernel`](#práctica-4--modificación-de-funciones-del-kernel) | Modificar `sys_getpid()` con contador global |
| 5 | [`Modificacion de kernel`](#práctica-5--nueva-syscall-sys_get_process_info) | Nueva syscall `sys_get_process_info` (548) |
| 6 | [`Monitoreo de Kernel`](#práctica-6--monitoreo-de-procesos-en-tiempo-real) | Sistema completo de monitoreo: syscall + daemon + dashboard |

---

## Práctica 2 — Módulos Básicos de Kernel

**Carpeta:** [`Modulos basicos de Kernel/`](./Modulos%20basicos%20de%20Kernel/)

**Problema:** Comprender cómo el kernel de Linux puede extenderse en tiempo de ejecución sin necesidad de recompilar ni reiniciar el sistema.

**Solución:** Se implementó un módulo de kernel mínimo en C con las funciones `module_init` y `module_exit`, que emiten mensajes al log del sistema via `printk`. El módulo se carga y descarga en caliente con `insmod` / `rmmod` y los mensajes se verifican con `dmesg`.

**Conceptos clave:** `MODULE_LICENSE`, `printk`, `insmod`, `rmmod`, `Makefile` con `obj-m`.

```bash
make all
sudo insmod main.ko
dmesg | tail          # → "Modulo cargado exitosamente"
sudo rmmod main
```

---

## Práctica 3 — Configuración y Compilación del Kernel

**Carpeta:** [`Verificacion de herramientes de compilacion/`](./Verificacion%20de%20herramientes%20de%20compilacion/)

**Problema:** Preparar un entorno de desarrollo completo capaz de compilar el kernel de Linux desde su código fuente e instalar un kernel personalizado e identificable.

**Solución:** Se instalaron todas las dependencias de compilación (`gcc`, `make`, `bison`, `flex`, `libssl-dev`, `libelf-dev`, `gawk`), se descargó el código fuente del kernel 6.12.69, se configuró con `make localmodconfig` y se personalizó el campo `EXTRAVERSION` del `Makefile` principal para que `uname -r` muestre un identificador propio (`6.12.69-CustomKernel`). Se resolvió un error de compilación por `gawk` faltante.

**Conceptos clave:** `make localmodconfig`, `EXTRAVERSION`, `make modules_install`, `update-grub`, snapshots de VirtualBox.

```bash
make localmodconfig
# Editar EXTRAVERSION = -CustomKernel en Makefile
make -j$(nproc)
sudo make modules_install && sudo make install
sudo update-grub && sudo reboot
uname -r    # → 6.12.69-CustomKernel
```

---

## Práctica 4 — Modificación de Funciones del Kernel

**Carpeta:** [`Modificacion de Funciones de Kernel/`](./Modificacion%20de%20Funciones%20de%20Kernel/)

**Problema:** Instrumentar una syscall existente del kernel para observar su comportamiento en tiempo de ejecución, sin alterar su funcionalidad original.

**Solución:** Se localizó `sys_getpid()` en `kernel/sys.c` mediante `grep` y se añadió una variable global estática `getpid_call_count`. La función fue modificada para incrementar el contador en cada invocación y registrar el resultado con `printk(KERN_INFO ...)`. Tras compilar e instalar el kernel modificado, se verificó con un programa de prueba en C que invoca `getpid()` en bucle y `dmesg` muestra el conteo acumulado.

**Conceptos clave:** `SYSCALL_DEFINE0`, `static long`, `printk`, `task_tgid_vnr`, `dmesg`.

```c
static long getpid_call_count = 0;

SYSCALL_DEFINE0(getpid) {
    getpid_call_count++;
    printk(KERN_INFO "[PRACTICA4] sys_getpid() invocada #%ld - PID: %d\n",
           getpid_call_count, task_tgid_vnr(current));
    return task_tgid_vnr(current);
}
```

```bash
dmesg | grep PRACTICA4
# → [12345.678] [PRACTICA4] sys_getpid() invocada #1 - PID: 1234
```

---

## Práctica 5 — Nueva Syscall: `sys_get_process_info`

**Carpeta:** [`Modificacion de kernel/`](./Modificacion%20de%20kernel/)

**Problema:** Crear desde cero una nueva llamada al sistema que exponga información de un proceso arbitrario al espacio de usuario a partir de su PID, accediendo directamente a las estructuras internas del kernel.

**Solución:** Se implementó `sys_get_process_info` (número **548**) en un nuevo subdirectorio `kernel/get_process_info/`. La syscall localiza el `task_struct` del proceso bajo RCU lock, extrae nombre, tiempo de CPU en jiffies y memoria virtual, y copia los datos al espacio de usuario con `copy_to_user()`. Se registró en `syscall_64.tbl` y `syscalls.h`, se integró al build del kernel y se verificó con un programa de usuario en C.

**Conceptos clave:** `SYSCALL_DEFINE2`, `find_task_by_vpid`, `rcu_read_lock`, `copy_to_user`, `get_task_comm`, `syscall_64.tbl`.

```c
SYSCALL_DEFINE2(get_process_info, pid_t, pid,
                struct process_info __user *, info) {
    rcu_read_lock();
    task = find_task_by_vpid(pid);
    // ... rellenar struct ...
    rcu_read_unlock();
    return copy_to_user(info, &kinfo, sizeof(kinfo)) ? -EFAULT : 0;
}
```

```bash
gcc -o test_syscall test_syscall.c
./test_syscall 1       # → PID: 1 | Nombre: systemd | CPU: 3s | Mem: 51200 KB
```

---

## Práctica 6 — Monitoreo de Procesos en Tiempo Real

**Carpeta:** [`Monitoreo de Kernel/`](./Monitoreo%20de%20Kernel/)

**Problema:** Construir un sistema end-to-end que exponga métricas globales del kernel (memoria RAM, swap, fallos de página, top-10 procesos por RAM) y las visualice en un dashboard web actualizado en tiempo real, todo desglosado en tres capas independientes.

**Solución:** Se implementó `sys_get_system_monitor` (número **549**) que itera todos los procesos con `for_each_process()` bajo RCU lock y recopila métricas de memoria via `si_meminfo()` y contadores de páginas. Un daemon en C (`monitor.c`) invoca la syscall cada 3 segundos, serializa a JSON y lo envía vía HTTP POST con libcurl a un servidor Python (`backend.py`). El dashboard HTML con Chart.js hace polling y renderiza 5 gráficas + tabla de top procesos con consulta individual de PID (reutilizando la syscall 548 de la Práctica 5).

**Arquitectura:**
```
Kernel (syscall 549) → monitor_daemon (C) → backend.py (Python :8080) → dashboard.html (Chart.js)
                                                     ↑
                                        query_process (C, syscall 548) ← /api/process/<pid>
```

**Conceptos clave:** `si_meminfo`, `for_each_process`, `NR_FILE_PAGES`, `all_vm_events`, libcurl, HTTP polling, Chart.js.

```bash
python3 backend.py          # Terminal 1
sudo ./monitor_daemon       # Terminal 2
# Abrir http://<IP_VM>:8080/ en el navegador host
```

---

## Tecnologías Transversales

| Herramienta | Uso |
|-------------|-----|
| `make localmodconfig` | Reducir tiempo de compilación del kernel a ~15-30 min |
| `scripts/config --disable SYSTEM_TRUSTED_KEYS` | Evitar errores de firma en entornos de desarrollo |
| `dmesg` | Verificar mensajes del kernel en tiempo de ejecución |
| `printk` | Logging desde el espacio de kernel |
| `rcu_read_lock` | Acceso seguro a estructuras del kernel sin race conditions |
| `copy_to_user` | Transferencia segura kernel → espacio de usuario |
| VirtualBox Snapshots | Respaldo antes de instalar kernels modificados |

---

## Referencias

- Robert Love. *Linux Kernel Development*, 3rd Edition. Addison-Wesley, 2010.
- Daniel P. Bovet & Marco Cesati. *Understanding the Linux Kernel*, 3rd Edition. O'Reilly Media, 2005.
- The Linux Kernel documentation — <https://www.kernel.org/doc/html/latest/>
- Adding a New System Call — <https://www.kernel.org/doc/html/latest/process/adding-syscalls.html>
- Kernel Newbies — <https://kernelnewbies.org/KernelHacking>
