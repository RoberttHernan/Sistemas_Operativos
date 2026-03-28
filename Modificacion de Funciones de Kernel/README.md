# Práctica 4 — Modificación de Funciones del Kernel de Linux

## Enunciado

> **Objetivo de la práctica:** Modificar directamente el código fuente del kernel de Linux para agregar un contador global que registre el número de veces que la syscall `sys_getpid()` es invocada, reportando el resultado a través del subsistema de logging del kernel (`printk`). El ejercicio requiere compilar e instalar el kernel modificado en una máquina virtual y verificar su funcionamiento mediante las herramientas del sistema.

**Tareas requeridas:**

1. Descargar el código fuente del kernel de Linux versión 6.x desde [kernel.org](https://kernel.org).
2. Localizar la implementación de `sys_getpid()` en el archivo `kernel/sys.c`.
3. Agregar una variable `static long` global que almacene el conteo de invocaciones.
4. Modificar `sys_getpid()` para incrementar el contador y registrar el resultado con `printk()`.
5. Configurar el kernel con `make olddefconfig` y `make localmodconfig`.
6. Compilar el kernel modificado e instalarlo en la VM.
7. Arrancar con el nuevo kernel y verificar las entradas en `dmesg`.

---

## 1. Introducción

El kernel de Linux constituye el núcleo del sistema operativo y es responsable de administrar los recursos de hardware, la gestión de memoria, la planificación de procesos y la comunicación entre el espacio de usuario y el hardware. Una de las interfaces más importantes entre ambos espacios son las llamadas al sistema (syscalls), mediante las cuales los procesos solicitan servicios al sistema operativo.

En esta práctica se realizó una modificación directa al código fuente del kernel de Linux, específicamente sobre la función `sys_getpid()`, con el objetivo de agregar un contador global que registre el número de invocaciones de dicha función y reporte el resultado mediante `printk`. Esto permite observar de forma concreta cómo los cambios en el código fuente del kernel afectan el comportamiento del sistema operativo en ejecución.

---

## 2. Objetivos

- Localizar e identificar la función `sys_getpid()` dentro del código fuente del kernel de Linux.
- Agregar una variable global en el archivo `kernel/sys.c` para almacenar el número de invocaciones.
- Modificar `sys_getpid()` para incrementar el contador en cada llamada y registrar su valor mediante `printk()`.
- Compilar e instalar el kernel modificado en una máquina virtual con Linux Mint.
- Verificar el correcto funcionamiento del kernel modificado mediante `dmesg`.

---

## 3. Entorno de Desarrollo

| Componente         | Detalle                          |
|--------------------|----------------------------------|
| Sistema operativo  | Linux Mint (basado en Debian)    |
| Virtualización     | VirtualBox                       |
| Versión del kernel | linux-6.12.69                    |
| Lenguaje           | C                                |
| Editor             | nano / VS Code                   |
| Control de versión | git                              |

---

## 4. Procedimiento

### 4.1 Descarga del código fuente del kernel

Se descargó el código fuente del kernel de Linux en su versión 6.12.69 desde el repositorio oficial kernel.org y se extrajo en el directorio `~/Downloads/`.

```bash
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.69.tar.xz
tar -xvf linux-6.12.69.tar.xz
cd linux-6.12.69
```

### 4.2 Localización de la función sys_getpid()

Se utilizó el comando `grep` para localizar la definición de la función dentro del código fuente:

```bash
grep -rn "SYSCALL_DEFINE0(getpid" --include="*.c"
```

La función se encontró en el archivo `kernel/sys.c`. Su implementación original es la siguiente:

```c
SYSCALL_DEFINE0(getpid)
{
    return task_tgid_vnr(current);
}
```

### 4.3 Modificación de la función sys_getpid()

Se editó el archivo `kernel/sys.c` con el editor nano:

```bash
nano kernel/sys.c
```

Se agregó una variable global estática antes de la definición de la función y se modificó el cuerpo de la misma para incrementar el contador y registrar su valor en el log del sistema:

```c
/* PRACTICA 4 - Contador de llamadas a sys_getpid */
static long getpid_call_count = 0;

SYSCALL_DEFINE0(getpid)
{
    getpid_call_count++;
    printk(KERN_INFO "[PRACTICA4] sys_getpid() invocada #%ld - PID actual: %d\n",
           getpid_call_count, task_tgid_vnr(current));

    return task_tgid_vnr(current);
}
```

La variable `getpid_call_count` es de tipo `static long`, lo que garantiza que persiste en memoria durante toda la vida del kernel y no se reinicia entre llamadas.

### 4.4 Configuración del kernel

Se copió la configuración del kernel activo del sistema como base de configuración:

```bash
sudo cp /boot/config-$(uname -r) .config
```

Se actualizaron los valores por defecto para todas las opciones nuevas del kernel 6.12.69:

```bash
make olddefconfig
```

Este paso generó advertencias menores relacionadas con módulos no soportados (`NET_9P_USBG`, `ANDROID_BINDER_IPC`, `ANDROID_BINDERFS`), las cuales son normales al trasladar una configuración entre versiones de kernel. La configuración se escribió correctamente al archivo `.config`.

A continuación se deshabilitaron los certificados de firma de módulos para evitar errores de compilación relacionados con claves de confianza:

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
```

Para reducir el tiempo de compilación, se utilizó `make localmodconfig`, que ajusta la configuración para incluir únicamente los módulos que están cargados actualmente en el sistema, eliminando la compilación de módulos innecesarios:

```bash
make localmodconfig
```

### 4.5 Compilación del kernel

Se inició el proceso de compilación utilizando todos los núcleos de procesamiento disponibles en la máquina virtual:

```bash
make -j$(nproc) 2>&1 | tee compilacion.log
```

Durante la compilación se observaron los archivos objeto siendo generados correctamente (`CC fs/backing-file.o`, `CC fs/mbcache.o`, entre otros). El proceso culminó en la generación de la imagen del kernel y los módulos correspondientes.

### 4.6 Instalación del kernel modificado

Se instalaron los módulos del kernel compilado:

```bash
sudo make modules_install
```

Se instaló la imagen del kernel en el directorio de arranque del sistema:

```bash
sudo make install
```

Se actualizó el gestor de arranque GRUB para incluir la nueva entrada del kernel:

```bash
sudo update-grub
```

### 4.7 Arranque con el kernel modificado

Se reinició la máquina virtual:

```bash
sudo reboot
```

En el menú de GRUB se seleccionó la opción "Advanced options" y se eligió el kernel recién compilado. Tras el arranque se verificó la versión activa del kernel:

```bash
uname -r
```

### 4.8 Verificación del funcionamiento

Se creó un programa de prueba en C que invoca `getpid()` repetidamente para generar entradas en el log del kernel:

```c
// test_getpid.c
#include <stdio.h>
#include <unistd.h>

int main() {
    int i;
    for (i = 0; i < 10; i++) {
        printf("PID: %d\n", getpid());
    }
    return 0;
}
```

Se compiló y ejecutó:

```bash
gcc -o main main.c
./main
```

Se verificaron las entradas generadas en el log del sistema:

```bash
dmesg | grep "PRACTICA4"
```

La salida esperada en `dmesg` tiene la siguiente forma:

```
[12345.678901] [PRACTICA4] sys_getpid() invocada #1 - PID actual: 1234
[12345.679102] [PRACTICA4] sys_getpid() invocada #2 - PID actual: 1234
[12345.679304] [PRACTICA4] sys_getpid() invocada #3 - PID actual: 1234
...
[12345.681500] [PRACTICA4] sys_getpid() invocada #10 - PID actual: 1234
```

---

## 5. Código Insertado

A continuación se presenta el bloque de código que fue insertado en `kernel/sys.c`. El archivo completo modificado está disponible en [`sys.c`](./sys.c).

```c
/* ================================================================
 * PRACTICA 4 - Sistemas Operativos
 * Modificacion: Contador global de invocaciones a sys_getpid()
 * Archivo: kernel/sys.c
 * ================================================================ */

static long getpid_call_count = 0;

SYSCALL_DEFINE0(getpid)
{
    getpid_call_count++;
    printk(KERN_INFO "[PRACTICA4] sys_getpid() invocada #%ld - PID actual: %d\n",
           getpid_call_count, task_tgid_vnr(current));

    return task_tgid_vnr(current);
}
```

---

## 6. Herramientas Utilizadas

| Herramienta           | Uso en la práctica                                                    |
|-----------------------|-----------------------------------------------------------------------|
| `gcc`                 | Compilación del programa de prueba en espacio de usuario             |
| `make`                | Compilación e instalación del kernel modificado                      |
| `make olddefconfig`   | Actualización de la configuración del kernel con valores por defecto |
| `make localmodconfig` | Reducción de módulos compilados al conjunto activo en el sistema     |
| `scripts/config`      | Modificación de parámetros individuales del archivo `.config`        |
| `dmesg`               | Visualización de mensajes del ring buffer del kernel                 |
| `printk()`            | Función del kernel para escritura de mensajes en el log del sistema  |
| `uname -r`            | Verificación de la versión del kernel en ejecución                   |
| `nano`                | Edición del archivo `kernel/sys.c`                                   |
| `grep`                | Búsqueda de la definición de funciones en el código fuente           |

---

## 7. Análisis de Resultados

### 7.1 Comportamiento del contador global

La variable `getpid_call_count` se declaró como `static long`, lo que implica que reside en el segmento de datos estáticos del kernel y mantiene su valor entre sucesivas llamadas. Al ser una variable global del kernel, su valor persiste desde el arranque del sistema hasta el apagado, acumulando el conteo total de invocaciones independientemente del proceso que realice la llamada.

### 7.2 Impacto en el rendimiento

La modificación introduce una operación de incremento entero y una llamada a `printk()` en cada invocación de `sys_getpid()`. El incremento del contador es una operación de costo O(1) y no representa una degradación significativa del rendimiento. Sin embargo, `printk()` escribe en el ring buffer del kernel y puede introducir latencia observable en sistemas con alta frecuencia de llamadas a `getpid()`, dado que `sys_getpid()` es una de las syscalls más invocadas por los procesos del sistema.

En un entorno de producción, sería recomendable proteger el acceso al contador con un mecanismo de sincronización como `atomic_long_t` para garantizar la consistencia en sistemas multiprocesador (SMP).

### 7.3 Visibilidad mediante dmesg

La función `printk()` con el nivel `KERN_INFO` escribe el mensaje en el ring buffer del kernel, el cual puede ser consultado en cualquier momento mediante el comando `dmesg`. Esto permite observar directamente la frecuencia de invocación de la syscall y el PID del proceso que la invocó, constituyendo un mecanismo efectivo de depuración y observación del comportamiento interno del kernel.

### 7.4 Integridad funcional

La modificación preserva el comportamiento original de `sys_getpid()` ya que el valor de retorno (`task_tgid_vnr(current)`) no fue alterado. La función sigue retornando correctamente el PID del proceso en ejecución, tal como lo esperan todos los procesos del sistema que la invocan.

---

## 8. Flujo de Creación de Procesos en el Kernel de Linux

Cuando un proceso en espacio de usuario invoca `fork()` o `clone()`, el kernel ejecuta la función `do_fork()` (o `kernel_clone()` en versiones recientes), la cual asigna una nueva estructura `task_struct` para el proceso hijo. Esta estructura contiene toda la información del proceso, incluyendo su PID, que es asignado mediante `alloc_pid()` del namespace de PIDs correspondiente.

La función `sys_getpid()` accede al campo `tgid` (Thread Group ID) de la estructura `task_struct` del proceso actual a través del puntero `current`, que en Linux es una macro que retorna un puntero a la `task_struct` del proceso en ejecución en la CPU actual. La función `task_tgid_vnr()` traduce el TGID al espacio de nombres de PID visible desde el proceso, lo que permite el correcto funcionamiento en entornos con namespaces (como contenedores Docker).

---

## 9. Conclusiones

- Se logró modificar exitosamente el código fuente del kernel de Linux en el archivo `kernel/sys.c`, agregando un contador global de invocaciones a `sys_getpid()` con registro en el log del sistema.

- El uso de `make localmodconfig` redujo significativamente el tiempo de compilación al limitar los módulos compilados al conjunto activo en el sistema, en comparación con una compilación completa.

- La deshabilitación de `SYSTEM_TRUSTED_KEYS` y `SYSTEM_REVOCATION_KEYS` fue necesaria para evitar errores de compilación relacionados con la firma de módulos del kernel, un paso crítico en entornos de desarrollo y prueba.

- La herramienta `printk()` demostró ser un mecanismo efectivo para la depuración y observación del comportamiento del kernel desde el espacio de kernel, siendo el equivalente de `printf()` en el espacio de usuario.

- La modificación realizada no altera la funcionalidad original de la syscall, lo que demuestra que es posible extender el comportamiento del kernel de forma no destructiva, preservando la compatibilidad con el espacio de usuario.

- Este ejercicio proporciona una comprensión práctica del ciclo completo de desarrollo del kernel: modificación del código fuente, configuración, compilación, instalación y verificación, que constituye la base para tareas más complejas como la implementación de nuevas llamadas al sistema o la modificación del planificador de procesos.

---

## 10. Referencias

- Robert Love. *Linux Kernel Development*, 3rd Edition. Addison-Wesley, 2010.
- Daniel P. Bovet & Marco Cesati. *Understanding the Linux Kernel*, 3rd Edition. O'Reilly Media, 2005.
- The Linux Kernel documentation. <https://www.kernel.org/doc/html/latest/>
- Kernel Newbies — Kernel Hacking 101. <https://kernelnewbies.org/KernelHacking>
- printk documentation. <https://www.kernel.org/doc/html/latest/core-api/printk-basics.html>
