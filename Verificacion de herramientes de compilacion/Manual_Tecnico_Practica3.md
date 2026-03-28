# Manual Técnico - Práctica 3
## Instalación y Verificación de Herramientas de Compilación

**Universidad San Carlos de Guatemala**  
**Facultad de Ingeniería - Ingeniería en Ciencias y Sistemas**  
**Sistemas Operativos 2**  

---

## Enunciado de la Práctica

El objetivo de esta práctica es crear un entorno de desarrollo completo en un sistema Linux para compilar el kernel desde el código fuente. Los estudiantes deben:

1. **Preparar el entorno de desarrollo** instalando todas las herramientas y librerías necesarias para compilar un kernel Linux
2. **Descargar el código fuente** del kernel Linux de una versión específica
3. **Configurar el kernel** seleccionando los módulos y características a compilar
4. **Compilar el kernel** a partir del código fuente
5. **Instalar el kernel personalizado** en el sistema
6. **Personalizar la versión del kernel** de manera que sea identificable en el sistema
7. **Verificar la instalación** confirmando que el kernel personalizado está en uso

Esta práctica proporciona experiencia práctica en la gestión de kernels Linux y comprensión profunda de cómo el sistema operativo se compila y instala.

---

## Índice

1. [Descripción General](#descripción-general)
2. [Requerimientos del Sistema](#requerimientos-del-sistema)
3. [Configuración de la Máquina Virtual](#configuración-de-la-máquina-virtual)
4. [Instalación de Herramientas de Compilación](#instalación-de-herramientas-de-compilación)
5. [Descarga del Kernel Linux](#descarga-del-kernel-linux)
6. [Configuración del Kernel](#configuración-del-kernel)
7. [Modificación del EXTRAVERSION](#modificación-del-extraversion)
8. [Compilación del Kernel](#compilación-del-kernel)
9. [Instalación del Kernel](#instalación-del-kernel)
10. [Verificación del Kernel Instalado](#verificación-del-kernel-instalado)
11. [Observaciones Técnicas](#observaciones-técnicas)

---

## Descripción General

Esta práctica tiene como objetivo preparar un entorno de desarrollo funcional en GNU/Linux para la compilación del kernel Linux. Se instalan y verifican las herramientas GCC, make, GDB y las librerías de desarrollo necesarias, culminando con la compilación e instalación de un kernel personalizado visible mediante el comando `uname -r`.

---

## Requerimientos del Sistema

| Componente | Mínimo Recomendado |
|---|---|
| Sistema Operativo | Linux Mint (basado en Debian) |
| RAM | 4 GB |
| Núcleos CPU | 4 |
| Espacio en disco | 50 GB |
| Virtualización | VirtualBox |

---

## Configuración de la Máquina Virtual

Se utilizó VirtualBox para crear una máquina virtual con Linux Mint. Antes de instalar el kernel compilado se tomó un **snapshot** como respaldo:

```
VirtualBox → Máquina → Tomar Instantánea → "Antes de instalar kernel"
```

Esto permite revertir la VM en caso de que el sistema no arranque correctamente tras instalar el kernel personalizado.

---

## Instalación de Herramientas de Compilación

Se actualizaron los repositorios e instalaron todas las dependencias necesarias con un solo comando:

```bash
sudo apt update
sudo apt install -y gcc make gdb build-essential libncurses-dev bison flex libssl-dev libelf-dev gawk
```

### Herramientas instaladas:

| Herramienta | Descripción |
|---|---|
| `gcc` | Compilador de C de GNU |
| `make` | Gestor de construcción de proyectos |
| `gdb` | Depurador para programas en C |
| `build-essential` | Meta-paquete con herramientas esenciales de compilación |
| `libncurses-dev` | Librería para la interfaz de configuración del kernel |
| `bison` / `flex` | Generadores de parsers necesarios para el kernel |
| `libssl-dev` | Librería SSL para firma de módulos |
| `libelf-dev` | Librería para manejo de archivos ELF |
| `gawk` | Procesador de texto GNU necesario para el build |

### Verificación de versiones instaladas:

```bash
gcc --version
make --version
gdb --version
```

---

## Descarga del Kernel Linux

Se descargó el código fuente del kernel Linux versión **6.12.69** desde el sitio oficial [https://kernel.org](https://kernel.org):

```bash
cd ~/Descargas
tar xvf linux-6.12.69.tar.xz
cd linux-6.12.69
```

---

## Configuración del Kernel

Se utilizó `localmodconfig` para generar una configuración basada en los módulos actualmente cargados en el sistema, lo que reduce significativamente el tiempo de compilación:

```bash
make localmodconfig
```

> **Nota:** Este comando puede mostrar warnings sobre módulos de MIDI (`SND_SEQ_MIDI_EVENT`, `SND_RAWMIDI`). Estos son normales en una VM y no afectan el proceso.

### Deshabilitar verificación de firmas de módulos

Para evitar errores durante la compilación relacionados con llaves de firma, se deshabilitaron las verificaciones de certificados:

```bash
scripts/config --disable SYSTEM_TRUSTED_KEYS
scripts/config --disable SYSTEM_REVOCATION_KEYS
scripts/config --set-str SYSTEM_TRUSTED_KEYS ""
scripts/config --set-str SYSTEM_REVOCATION_KEYS ""
```

---

## Modificación del EXTRAVERSION

Se modificó el archivo `Makefile` principal del kernel para personalizar la versión con el nombre y carné del estudiante. Esta modificación hace que el valor aparezca al ejecutar `uname -r`:

```bash
nano Makefile
```

Se localizó la línea al inicio del archivo:

```makefile
EXTRAVERSION =
```

Y se cambió a:

```makefile
EXTRAVERSION = -CustomKernel
```

Esto hace que la versión completa del kernel sea:
```
6.12.69-CustomKernel
```

---

## Compilación del Kernel

Se compiló el kernel utilizando todos los núcleos disponibles del procesador para optimizar el tiempo:

```bash
make -j$(nproc)
```

El flag `-j$(nproc)` indica a make que use tantos hilos paralelos como núcleos tenga disponibles el CPU.

### Error encontrado y solución

Durante la compilación se presentó el siguiente error:

```
/bin/sh: 1: gawk: not found
make[2]: *** [scripts/Makefile.vmlinux:51: modules.builtin.ranges] Error 127
```

**Causa:** Faltaba el paquete `gawk` instalado en el sistema.

**Solución:**
```bash
sudo apt install -y gawk
make -j$(nproc)
```

### Verificación de la imagen compilada:

```bash
ls arch/x86/boot/bzImage
```

Si el archivo existe, la compilación fue exitosa.

---

## Instalación del Kernel

Una vez compilado correctamente el kernel, se procedió a instalar:

**1. Instalar los módulos del kernel:**
```bash
sudo make modules_install
```


**2. Instalar el kernel en /boot:**
```bash
sudo make install
```

**3. Actualizar el gestor de arranque GRUB:**
```bash
sudo update-grub
```

**4. Configurar GRUB para mostrar el menú al arrancar:**
```bash
sudo nano /etc/default/grub
```

Se modificaron las líneas:
```
GRUB_TIMEOUT_STYLE=menu
GRUB_TIMEOUT=10
```

```bash
sudo update-grub
```

**5. Reiniciar el sistema:**
```bash
sudo reboot
```

Al reiniciar, se presionó `Shift` para acceder al menú GRUB y se seleccionó:
```
Advanced options for Linux Mint →
    Linux 6.12.69-CustomKernel
```

---

## Verificación del Kernel Instalado

Se verificó que los archivos del kernel se instalaron correctamente en `/boot`:

```bash
ls /boot/vmlinuz*
```

Output obtenido:
```
/boot/vmlinuz
/boot/vmlinuz-6.12.69-CustomKernel
/boot/vmlinuz-6.14.0-37-generic
/boot/vmlinuz.old
```

Después de reiniciar en el nuevo kernel, se ejecutó el comando final de verificación:

```bash
uname -r
```

**Output esperado:**
```
6.12.69-CustomKernel
```

> ✅ Este resultado confirma que el kernel personalizado fue compilado e instalado exitosamente.

---

## Observaciones Técnicas

- **Tiempo de compilación:** El proceso de compilación con `make -j$(nproc)` tardó aproximadamente entre 15 minutos.

- **Uso de `localmodconfig`:** Esta opción es ideal para entornos de práctica ya que solo compila los módulos que el sistema está usando actualmente, reduciendo el tiempo de compilación comparado con `make defconfig` o `make menuconfig`.

- **Snapshots en VirtualBox:** Se recomienda siempre tomar un snapshot antes de instalar un kernel personalizado. Si el sistema no arranca, se puede restaurar el snapshot fácilmente sin perder el trabajo realizado.

- **`gawk` como dependencia:** Aunque `gawk` no se menciona explícitamente en las dependencias típicas, es requerido por el kernel 6.12 para generar `modules.builtin.ranges` durante la compilación.

- **EXTRAVERSION vs `start_kernel`:** Se eligió modificar `EXTRAVERSION` en el Makefile ya que es el método correcto para personalizar la salida de `uname -r`. Modificar `start_kernel` en `init/main.c` solo afectaría los mensajes del log de arranque (`dmesg`), no el resultado de `uname -r`.

---

*Manual Técnico generado para la Práctica 3 - Sistemas Operativos 2*  
*Fecha: Febrero 2026*
