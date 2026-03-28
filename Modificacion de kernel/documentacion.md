# Práctica 5 — Creación de una Nueva Llamada al Sistema (`sys_get_process_info`)

## Enunciado

> **Objetivo de la práctica:** Extender el kernel de Linux agregando una nueva llamada al sistema (syscall) personalizada llamada `sys_get_process_info`. La syscall debe permitir a cualquier proceso en espacio de usuario consultar información detallada de otro proceso del sistema a partir de su PID, incluyendo su nombre, tiempo de CPU acumulado y uso de memoria virtual. La syscall debe implementarse correctamente dentro del árbol del kernel, registrarse en la tabla de syscalls para la arquitectura x86_64, y verificarse con un programa de prueba en C.

**Tareas requeridas:**

1. Definir la estructura `process_info` con los campos: `pid`, `name`, `runtime_sec` y `mem_kb`.
2. Implementar `sys_get_process_info()` en un nuevo archivo dentro de `kernel/`.
3. Registrar la syscall en `arch/x86/entry/syscalls/syscall_64.tbl` con el número **548**.
4. Agregar el prototipo en `include/linux/syscalls.h`.
5. Integrar el nuevo subdirectorio al sistema de build del kernel (`kernel/Makefile`).
6. Compilar e instalar el kernel modificado en una máquina virtual.
7. Verificar el funcionamiento con un programa de usuario en C y con `dmesg`.

---

## Tabla de Contenidos

1. [Descripción General](#1-descripción-general)
2. [Requerimientos del Sistema](#2-requerimientos-del-sistema)
3. [Arquitectura de la Solución](#3-arquitectura-de-la-solución)
4. [Estructura de Datos](#4-estructura-de-datos)
5. [Implementación de la Syscall](#5-implementación-de-la-syscall)
6. [Archivos Modificados en el Kernel](#6-archivos-modificados-en-el-kernel)
7. [Programa de Usuario](#7-programa-de-usuario)
8. [Proceso de Compilación del Kernel](#8-proceso-de-compilación-del-kernel)
9. [Instalación y Arranque](#9-instalación-y-arranque)
10. [Pruebas y Validación](#10-pruebas-y-validación)
11. [Herramientas Utilizadas](#11-herramientas-utilizadas)
12. [Observaciones Técnicas](#12-observaciones-técnicas)

---

## 1. Descripción General

Esta práctica consiste en extender el kernel de Linux agregando una nueva llamada al sistema (syscall) personalizada llamada `sys_get_process_info`. Esta syscall permite a cualquier programa en espacio de usuario consultar información detallada de un proceso del sistema mediante su **PID (Process ID)**.

La información devuelta incluye:

- Nombre del proceso
- PID
- Tiempo de ejecución en CPU (en segundos)
- Uso de memoria virtual aproximado (en KB)

La syscall opera dentro del **espacio de kernel**, accede directamente a la estructura `task_struct` del proceso solicitado, y copia los datos de forma segura al espacio de usuario mediante `copy_to_user()`.

---

## 2. Requerimientos del Sistema

| Componente            | Especificación                                           |
|-----------------------|----------------------------------------------------------|
| Sistema Operativo     | Linux Mint 21+ u otra distribución basada en Debian      |
| Versión del Kernel    | 6.12.69 LTS (Long Term Support)                          |
| Arquitectura          | x86_64 (64 bits)                                         |
| Compilador            | GCC 11+                                                  |
| Herramientas de build | Make, Binutils                                           |
| Control de versiones  | Git                                                      |
| Espacio en disco      | Mínimo 30 GB libres para compilar el kernel              |
| RAM recomendada       | 8 GB o más (la compilación es intensiva)                 |
| Editor de texto       | VS Code, Vim o Nano                                      |

### Dependencias necesarias

```bash
sudo apt update
sudo apt install -y build-essential libncurses-dev bison flex \
    libssl-dev libelf-dev dwarves git bc wget curl
```

---

## 3. Arquitectura de la Solución

```
┌─────────────────────────────────────────┐
│           ESPACIO DE USUARIO            │
│                                         │
│   test_syscall.c                        │
│   └── syscall(548, pid, &info)          │
│                                         │
└──────────────────┬──────────────────────┘
                   │  instrucción syscall (int 0x80 / syscall)
┌──────────────────▼──────────────────────┐
│           ESPACIO DE KERNEL             │
│                                         │
│   sys_get_process_info(pid, *info)      │
│   ├── find_task_by_vpid(pid)            │
│   │     └── accede a task_struct        │
│   ├── get_task_comm()   → nombre        │
│   ├── task->utime + stime → tiempo CPU  │
│   ├── task->mm->total_vm → memoria      │
│   └── copy_to_user() → copia a usuario  │
│                                         │
└─────────────────────────────────────────┘
```

---

## 4. Estructura de Datos

La información del proceso se transfiere entre kernel y usuario mediante la siguiente estructura, definida tanto en el código del kernel como en el programa de usuario:

```c
struct process_info {
    pid_t pid;          /* Identificador del proceso (int) */
    char  name[16];     /* Nombre del proceso (TASK_COMM_LEN = 16) */
    long  runtime_sec;  /* Tiempo de CPU acumulado en segundos */
    long  mem_kb;       /* Memoria virtual total aproximada en KB */
};
```

### Campos y fuentes de datos en el kernel

| Campo          | Fuente en `task_struct`                      | Descripción                                                                   |
|----------------|----------------------------------------------|-------------------------------------------------------------------------------|
| `pid`          | `task->pid`                                  | PID del proceso                                                               |
| `name`         | `get_task_comm(buf, task)`                   | Nombre del ejecutable (máx. 15 chars + `\0`)                                  |
| `runtime_sec`  | `(task->utime + task->stime) / HZ`           | Suma de tiempo en modo usuario y kernel, convertido a segundos                |
| `mem_kb`       | `task->mm->total_vm << (PAGE_SHIFT - 10)`    | Páginas virtuales convertidas a KB                                            |

---

## 5. Implementación de la Syscall

**Archivo:** `kernel/get_process_info/get_process_info.c`

El código completo está disponible en [`kernel/get_process_info/get_process_info.c`](./kernel/get_process_info/get_process_info.c).

```c
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/jiffies.h>

struct process_info {
    pid_t pid;
    char  name[16];
    long  runtime_sec;
    long  mem_kb;
};

SYSCALL_DEFINE2(get_process_info,
                pid_t, pid,
                struct process_info __user *, info)
{
    struct task_struct  *task;
    struct process_info  kinfo;
    unsigned long        total_time;

    /* Registro en dmesg para depuración */
    printk(KERN_INFO "sys_get_process_info: consultando PID %d\n", pid);

    /* Buscar proceso bajo RCU lock para evitar condiciones de carrera */
    rcu_read_lock();
    task = find_task_by_vpid(pid);

    if (!task) {
        rcu_read_unlock();
        return -ESRCH;  /* No such process */
    }

    /* Rellenar estructura con datos del proceso */
    kinfo.pid = task->pid;
    get_task_comm(kinfo.name, task);

    total_time        = task->utime + task->stime;
    kinfo.runtime_sec = (long)(total_time / HZ);

    if (task->mm)
        kinfo.mem_kb = (long)(task->mm->total_vm << (PAGE_SHIFT - 10));
    else
        kinfo.mem_kb = 0;

    rcu_read_unlock();

    /* Transferencia segura al espacio de usuario */
    if (copy_to_user(info, &kinfo, sizeof(kinfo)))
        return -EFAULT;

    return 0;
}
```

### Decisiones de diseño

- **`rcu_read_lock()` / `rcu_read_unlock()`:** Se utiliza RCU (Read-Copy-Update) para acceder a `task_struct` de forma segura sin bloqueos pesados. Esto evita condiciones de carrera si el proceso termina mientras se está leyendo su información.
- **`find_task_by_vpid()`:** Busca el proceso por PID en el namespace de PID del proceso llamante, que es el comportamiento esperado desde espacio de usuario.
- **`copy_to_user()`:** Función del kernel que copia datos del espacio de kernel al espacio de usuario de forma segura, verificando que el puntero de destino sea válido y accesible.
- **`printk(KERN_INFO ...)`:** Registra cada invocación de la syscall en el log del kernel, visible con `dmesg`. Útil para depuración y auditoría.
- **`ESRCH` y `EFAULT`:** Códigos de error estándar de Linux. `ESRCH` = proceso no encontrado; `EFAULT` = puntero de usuario inválido.

---

## 6. Archivos Modificados en el Kernel

Se modificaron o crearon los siguientes archivos dentro del árbol del código fuente del kernel:

### 6.1 Nuevo archivo: `kernel/get_process_info/get_process_info.c`

Contiene la implementación completa de la syscall (ver sección 5).

### 6.2 Nuevo archivo: `kernel/get_process_info/Makefile`

Indica al sistema de build del kernel que compile el archivo:

```makefile
obj-y := get_process_info.o
```

### 6.3 Modificado: `kernel/Makefile`

Se agrega la nueva carpeta al build del subsistema kernel:

```makefile
# Línea agregada al bloque obj-y existente:
obj-y += get_process_info/
```

### 6.4 Modificado: `arch/x86/entry/syscalls/syscall_64.tbl`

Se registra la syscall en la tabla de syscalls para arquitectura x86_64:

```
548    common    get_process_info    sys_get_process_info
```

> El número **548** fue elegido como el siguiente disponible después del último entry en la tabla. Verificar con `tail -20 arch/x86/entry/syscalls/syscall_64.tbl` antes de asignar.

### 6.5 Modificado: `include/linux/syscalls.h`

Se agrega el prototipo de la función antes del `#endif` final:

```c
/* Syscall personalizada: información de proceso por PID */
struct process_info;
asmlinkage long sys_get_process_info(pid_t pid,
                                     struct process_info __user *info);
```

Los archivos modificados del kernel están disponibles en el directorio [`kernel/`](./kernel/).

---

## 7. Programa de Usuario

**Archivo:** [`test_syscall.c`](./test_syscall.c)

El programa de usuario invoca la syscall mediante la función `syscall()` de la glibc, pasando el número de syscall y los argumentos requeridos.

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

#define SYS_GET_PROCESS_INFO 548

struct process_info {
    int  pid;
    char name[16];
    long runtime_sec;
    long mem_kb;
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
    printf("║  PID          : %d\n",     info.pid);
    printf("║  Nombre       : %s\n",     info.name);
    printf("║  Tiempo CPU   : %ld seg\n", info.runtime_sec);
    printf("║  Memoria      : %ld KB\n",  info.mem_kb);
    printf("╚══════════════════════════════╝\n\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("=== Prueba 1: PID 1 (systemd/init) ===\n");
        consultar_pid(1);
        printf("=== Prueba 2: Proceso actual ===\n");
        consultar_pid(getpid());
        printf("=== Prueba 3: Proceso padre ===\n");
        consultar_pid(getppid());
    } else {
        consultar_pid(atoi(argv[1]));
    }
    return 0;
}
```

### Compilación del programa de usuario

```bash
gcc -o test_syscall test_syscall.c
```

### Uso

```bash
# Ejecutar 3 pruebas automáticas
./test_syscall

# Consultar un PID específico
./test_syscall 1
./test_syscall 1234
```

---

## 8. Proceso de Compilación del Kernel

### Paso 1 — Descargar el código fuente

```bash
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.69.tar.xz
tar -xf linux-6.12.69.tar.xz
cd linux-6.12.69
```

### Paso 2 — Configurar el kernel

```bash
# Opción A: Usar configuración del kernel actual (más seguro, más lento)
cp /boot/config-$(uname -r) .config
make olddefconfig

# Opción B: Solo módulos actualmente cargados (más rápido, recomendado en VM)
make localmodconfig
```

### Paso 3 — Deshabilitar firma de módulos

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
```

### Paso 4 — Compilar

```bash
# Compilar usando todos los núcleos disponibles
make -j$(nproc)
```

> **Tiempo estimado:** entre 20 minutos (localmodconfig) y 2 horas (olddefconfig), según el hardware.

### Paso 5 — Instalar

```bash
sudo make modules_install
sudo make install
sudo update-grub
```

---

## 9. Instalación y Arranque

Después de instalar, reiniciar el sistema:

```bash
sudo reboot
```

En el menú de GRUB seleccionar:
`Advanced options for Linux Mint → linux-6.12.69`

Verificar la versión del kernel activo:

```bash
uname -r
# Salida esperada: 6.12.69
```

---

## 10. Pruebas y Validación

### Prueba 1 — PID 1 (systemd/init)

```bash
./test_syscall 1
```

Salida esperada:

```
╔══════════════════════════════╗
║  PID          : 1
║  Nombre       : systemd
║  Tiempo CPU   : 3 seg
║  Memoria      : 51200 KB
╚══════════════════════════════╝
```

### Prueba 2 — PID del proceso actual

```bash
./test_syscall $$
```

### Prueba 3 — PID del proceso padre (shell)

```bash
./test_syscall $PPID
```

### Verificación en dmesg

Cada llamada a la syscall queda registrada en el log del kernel:

```bash
dmesg | grep get_process_info
```

Salida esperada:

```
[  123.456789] sys_get_process_info: consultando PID 1
[  123.457012] sys_get_process_info: consultando PID 4523
[  123.457234] sys_get_process_info: consultando PID 4521
```

### Prueba con PID inválido

```bash
./test_syscall 999999
# Salida: Error consultando PID 999999: No such process
```

---

## 11. Herramientas Utilizadas

| Herramienta    | Versión | Uso                                            |
|----------------|---------|------------------------------------------------|
| GCC            | 11+     | Compilación del kernel y programa de usuario   |
| Make           | 4.3+    | Sistema de build del kernel                    |
| Git            | 2.x     | Control de versiones                           |
| dmesg          | —       | Verificación de logs del kernel                |
| strace         | —       | Trazado de syscalls (depuración)               |
| gdb            | —       | Depuración de bajo nivel                       |
| Nano / VS Code | —       | Edición de archivos                            |

---

## 12. Observaciones Técnicas

### Seguridad en el acceso a `task_struct`

El acceso a `task_struct` se realiza dentro de una sección protegida por `rcu_read_lock()`. Esto garantiza que la estructura del proceso no sea liberada mientras se está leyendo, incluso si el proceso termina concurrentemente. Sin esta protección existiría una condición de carrera que podría causar accesos a memoria inválida (use-after-free) en el kernel.

### Conversión de tiempo de CPU

El tiempo de CPU se almacena en el kernel en **jiffies** (unidad interna del planificador). La constante `HZ` define cuántos jiffies hay por segundo (generalmente 250 o 1000 según la configuración). La conversión `total_time / HZ` produce segundos enteros; si se necesitara mayor precisión se podría usar `jiffies_to_msecs()`.

### Memoria virtual vs. memoria residente

El campo `total_vm` en `task->mm` representa el total de memoria **virtual** mapeada por el proceso, no la memoria física actualmente en uso (RSS). Para obtener la memoria residente real se usaría `get_mm_rss(task->mm)`, pero requiere incluir cabeceras adicionales. Para efectos de esta práctica, la memoria virtual es suficiente.

### Número de syscall

El número **548** asignado debe ser consistente entre la tabla del kernel (`syscall_64.tbl`) y la constante definida en el programa de usuario (`#define SYS_GET_PROCESS_INFO 548`). Si se usa un número diferente en alguno de los dos, la syscall no será encontrada y el programa retornará `ENOSYS`.

### Limitación de nombre del proceso

La función `get_task_comm()` copia el nombre del proceso con un máximo de `TASK_COMM_LEN` caracteres (16 bytes incluyendo el terminador `\0`). Nombres más largos son truncados por el kernel automáticamente.

---

## Referencias

- Robert Love. *Linux Kernel Development*, 3rd Edition. Addison-Wesley, 2010.
- Daniel P. Bovet & Marco Cesati. *Understanding the Linux Kernel*, 3rd Edition. O'Reilly Media, 2005.
- The Linux Kernel documentation. <https://www.kernel.org/doc/html/latest/>
- Adding a New System Call. <https://www.kernel.org/doc/html/latest/process/adding-syscalls.html>
- printk documentation. <https://www.kernel.org/doc/html/latest/core-api/printk-basics.html>
