# Simple Kernel Module en Linux

Este repositorio contiene un ejemplo básico de un módulo para el Kernel de Linux. A continuación se detalla el funcionamiento del código y las instrucciones para compilar, cargar, descargar y limpiar el módulo.

## Descripción del Código

### `main.c`
Este es el archivo fuente del módulo. Contiene las funciones esenciales para su funcionamiento:

*   **Librerías**: Se incluyen cabeceras necesarias como `<linux/init.h>`, `<linux/module.h>` y `<linux/kernel.h>`.
*   **Metadatos**: Se definen la licencia, autor, descripción y versión del módulo usando macros como `MODULE_LICENSE`.
*   **`simple_module_init`**: Función que se ejecuta al **cargar** el módulo en el kernel. Imprime el mensaje "Modulo cargado exitosamente" en el log del sistema usando `printk`.
*   **`simple_module_exit`**: Función que se ejecuta al **descargar** el módulo del kernel. Imprime el mensaje "Modulo descargado exitosamente".
*   **Macros de Registro**: `module_init` y `module_exit` registran las funciones anteriores como los puntos de entrada y salida del módulo.

### `Makefile`
Archivo de configuración para la compilación simplificada:
*   Define el objeto a compilar (`obj-m += main.o`).
*   **`all`**: Compila el módulo utilizando los headers del kernel actual (`uname -r`).
*   **`clean`**: Elimina los archivos generados durante la compilación.

## Instrucciones de Uso

Sigue estos pasos para probar el módulo:

### 1. Compilar el Módulo
Abre una terminal en el directorio del proyecto y ejecuta:
```bash
make all
```
Esto generará un archivo `main.ko` (Kernel Object), que es el módulo compilado.

### 2. Cargar el Módulo
Para insertar el módulo en el kernel (requiere permisos de superusuario):
```bash
sudo insmod main.ko
```

### 3. Ver Mensajes del Kernel
Para confirmar que el módulo imprimió el mensaje de carga:
```bash
sudo dmesg | tail
```
Deberías ver: `Modulo cargado exitosamente`

### 4. Verificar que el módulo está cargado
Para listar el módulo y confirmar que está activo:
```bash
lsmod | grep main
```
Debería aparecer una línea empezando con `main`.

### 5. Descargar el Módulo
Para remover el módulo del kernel:
```bash
sudo rmmod main
```

### 6. Verificar la descarga
Verifica nuevamente los mensajes del kernel para confirmar la salida:
```bash
sudo dmesg | tail
```
Deberías ver ahora: `Modulo descargado exitosamente`

### 7. Limpiar archivos compilados
Para borrar los archivos temporales y binarios generados:
```bash
make clean
```
