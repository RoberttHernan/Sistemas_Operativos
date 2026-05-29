# MonitorDeKernel

Resumen
- MonitorDeKernel es una suite académica para monitorizar y asegurar un sistema Linux a nivel de kernel y espacio de usuario.
- Componentes principales:
  - Módulos/Syscalls kernel: implementaciones para obtener información de procesos, análisis de archivos, cuarentena y simulación de pánico (ubicados en `Kernel/`).
  - Daemon (ProgramaIntermedio): servicio HTTP (daemon) que consulta syscalls personalizados, monitorea un directorio, escanea archivos contra una blacklist y expone endpoints JSON (`/login`, `/metrics`, `/alerts`, `/files`, `/quarantine`, `/process/:pid`, `/threats`, `/scan/*`).
  - Dashboard: interfaz web en `Dashboard/Index.html` que consume el daemon y muestra métricas, alertas y gestión de cuarentena.
  - Docs/ y Test/: documentación, imágenes y pruebas de syscalls.

Estructura relevante
- `Kernel/` — código de kernel (syscalls, Makefiles). Requiere entorno de compilación del kernel para construir e integrar.
- `ProgramaIntermedio/` — daemon C, dependencias: `libmicrohttpd`, `libcjson`, PAM. Incluye `daemon.c`, `Makefile`, ejemplo de `monitor_dir/` con binarios de prueba y `hash_blacklist.json`.
- `Dashboard/` — frontend estático (HTML/JS/CSS) que se conecta al daemon.
- `Test/` — pruebas de syscalls y binarios de prueba.

Borrar/Verificar credenciales
- He escaneado el contenido por patrones comunes (`apikey`, `password`, `secret`, `private key`, `credentials`, etc.). No se encontraron credenciales embebidas o secretos en texto plano dentro del proyecto.
- Recomendación: antes de subir al portafolio, revisa cualquier archivo que puedas haber añadido fuera del repositorio (p.ej. `.env`, archivos en `monitor_dir/` o archivos exportados) y evita subir binarios que puedan contener datos sensibles.

Construcción y ejecución (resumen)
1) Dependencias del sistema (Ubuntu/Debian):

```bash
sudo apt update
sudo apt install build-essential libmicrohttpd-dev libcjson-dev libpam0g-dev
```

2) Compilar el daemon:

```bash
cd MonitorDeKernel/ProgramaIntermedio
make
# Ejecutar (necesita permisos para syscalls y acceso al directorio):
./daemon
```

3) Compilar módulos/syscalls del kernel (requiere fuentes/headers del kernel apropiados):

```bash
# Ejemplo genérico — adaptarlo a tu árbol de fuentes del kernel
cd MonitorDeKernel/Kernel/kernel
make
# Seguir las instrucciones dentro de los Makefiles para instalar o probar
```

4) Abrir Dashboard:
- Editar la constante `BASE` en `Dashboard/Index.html` si el daemon corre en otra IP/puerto.
- Abrir `Dashboard/Index.html` en el navegador.

Notas de seguridad y limpieza
- No se encontraron credenciales hardcodeadas; no fue necesario eliminar nada.
- El daemon genera tokens de sesión en memoria (no JWT) y los guarda en un arreglo in-memory; para producción usar JWT o almacenamiento seguro y HTTPS.
- Evita subir archivos que contengan datos reales de usuarios o claves privadas.

Cómo ayudar en el portafolio
- Puedo limpiar/eliminar binarios de prueba (`monitor_dir/*.bin`) si prefieres subir solo código fuente.
- Puedo generar un README en inglés, un archivo `LICENSE`, y un `Makefile` raíz para construir pasos automatizados.

Contacto
- Si quieres, renombro la carpeta a otro nombre diferente o ajusto el README según tu preferencia.
