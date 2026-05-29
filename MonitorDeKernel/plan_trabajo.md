# Plan de Trabajo — Proyecto Único
## Sistema Integral de Monitoreo, Análisis y Respuesta de Seguridad a Nivel Kernel
**Sistemas Operativos 2 — USAC FIUSAC**
**Fecha límite:** 23/04/2026

---

## Resumen de componentes

| Componente | Puntos | Prioridad |
|---|---|---|
| Syscalls (Kernel) | 30 |  Alta |
| Daemon en C | 25 |  Alta |
| Dashboard Web | 10 | Media |
| PAM + Roles | 10 |  Media |
| Funcionalidad adicional | 10 |  Baja |
| Manual Técnico + Usuario | 10 | Baja |

---

## Fase 1 — Kernel: Nuevas Syscalls
> Base: kernel Linux 6.12.69 LTS con `sys_get_process_info` (syscall 548) ya implementada

### 1.1 `sys_get_system_monitor()` — Métricas del sistema
- [x] Struct `system_monitor_info` con campos:
  - `memoria_usada`, `memoria_libre`, `memoria_cache`, `swap_usada`
  - `fallos_menores`, `fallos_mayores`
  - `paginas_activas`, `paginas_inactivas`
  - Array `top_processes[10]` (nombre, PID, % memoria)
- [x] Implementar usando `si_meminfo()`, `global_node_page_state()`, iteración de `task_struct`
- [x] Registrar en `syscall_64.tbl` (número 549)
- [x] Agregar prototipo en `include/linux/syscalls.h`
- [x] Agregar al `kernel/Makefile`

### 1.2 `sys_file_analize(path, result)` — Análisis de archivos
- [x] Struct `file_info` con: `tamaño`, `timestamp_modificacion`, `sha256_hash[65]`
- [x] Obtener metadata con `vfs_stat()`
- [x] Calcular SHA-256 usando la API crypto del kernel (`crypto_shash`)
- [x] Registrar en `syscall_64.tbl` (número 550)

### 1.3 `sys_scan_processes(buf, max_count)` — Escaneo de procesos
- [x] Struct `process_scan_entry` con: PID, nombre, `mem_kb`, `cpu_time`
- [x] Iterar con `for_each_process()`, recolectar datos crudos
- [x] **No clasificar severidad** — solo devolver datos
- [x] Registrar en `syscall_64.tbl` (número 551)

### 1.4 `sys_quarantine_file(path)` — Marcar archivo en cuarentena
- [x] Lista interna en kernel (arreglo estático, máx. 64 entradas)
- [x] Verificar existencia del archivo con `kern_path()`
- [x] Evitar duplicados
- [x] Almacenar: ruta + timestamp de cuarentena
- [x] **No eliminar ni mover el archivo físicamente**
- [x] Registrar en `syscall_64.tbl` (número 552)

### 1.5 `sys_restore_file(path)` — Restaurar archivo de cuarentena
- [x] Buscar ruta en la lista interna
- [x] Si existe, eliminarla de la lista
- [x] Retornar error controlado si no está en cuarentena
- [x] Registrar en `syscall_64.tbl` (número 553)

### 1.6 `sys_get_quarantine_list(buf, max_count)` — Listar cuarentena
- [x ] Copiar lista interna al espacio de usuario con `copy_to_user()`
- [x ] Registrar en `syscall_64.tbl` (número 554)

### 1.7 `sys_simulate_panic(msg)` — Simulación de fallo crítico
- [x ] Usar `printk(KERN_EMERG "[SECURITY] Simulated kernel panic: %s", msg)`
- [x ] **No llamar a `panic()` real**
- [x ] Registrar en `syscall_64.tbl` (número 555)

### 1.8 Compilación del kernel
- [x ] `make localmodconfig`
- [x ] `make -j$(nproc)`
- [x ] `make modules_install && make install`
- [x ] Verificar syscalls con programa de prueba en C

---

## Fase 2 — Daemon en C (Programa Intermedio)
> Ejecutable en segundo plano, 2 hilos mínimo, comunicación HTTP/WebSocket con dashboard

### 2.1 Estructura general
- [x ] `daemon.c` principal con `fork()` + `setsid()` para ejecutarse en background
- [x ] Manejo de señales (`SIGTERM`, `SIGINT`) para cierre limpio
- [x ] Logging a archivo (`/var/log/security_daemon.log`)

### 2.2 Thread 1 — Monitoreo
- [x ] Invocar `sys_get_system_monitor()` cada N segundos (configurable)
- [x ] Invocar `sys_scan_processes()` periódicamente
- [x ] Invocar `sys_get_process_info(pid)` cuando el dashboard lo solicite
- [x ] Evaluar condiciones de alerta:
  - Memoria usada > umbral → alerta MEDIUM
  - Page faults elevados → alerta MEDIUM
  - Proceso con alto consumo → alerta MEDIUM/HIGH
  - Condición crítica persistente → invocar `sys_simulate_panic()`

### 2.3 Thread 2 — Escaneo de archivos
- [x ] Monitorear directorio `/home/user/monitor/`
- [x ] Para cada archivo: invocar `sys_file_analize()` → obtener SHA-256
- [x ] Comparar hash contra `hash_blacklist.json` (leer con cJSON o parser propio)
- [x ] Detectar archivos nuevos y modificados (comparar con estado previo)
- [x ] Generar alertas según coincidencia:
  - Archivo nuevo → LOW
  - Archivo modificado → LOW/MEDIUM
  - Hash en blacklist (HIGH) → invocar `sys_simulate_panic()` + `sys_quarantine_file()`

### 2.4 Sistema de alertas
- [x ] Struct `alert_t` con: tipo, descripción, severidad (LOW/MEDIUM/HIGH), timestamp
- [x ] Cola de alertas thread-safe (mutex + array circular o lista enlazada)
- [x ] Función `generate_alert()` centralizada

### 2.5 Blacklist (`hash_blacklist.json`)
- [x ] Mínimo 8 firmas definidas con: hash, nombre, severidad, descripción
- [x ] Ejemplo de entradas HIGH: `Simulated.Ransomware.Encryptor`, `Simulated.Backdoor.Listener`
- [x ] Parser para leer el JSON al iniciar el daemon

### 2.6 Autenticación PAM
- [x ] Función `authenticate_pam(username, password)` usando `libpam`
- [x ] Verificar pertenencia a grupos `admin_user` o `common_user`
- [x ] Retornar rol: `ROLE_ADMIN`, `ROLE_USER`, `ROLE_DENIED`
- [x ] Si pertenece a ambos grupos → ROLE_ADMIN

### 2.7 Servidor HTTP embebido
- [x ] Servidor HTTP simple con sockets (o microhttpd/libmicrohttpd)
- [x ] Endpoints REST:
  - `POST /login` — autenticación PAM, devuelve token + rol
  - `GET /metrics` — métricas del sistema
  - `GET /alerts` — lista de alertas
  - `GET /files` — archivos analizados y su estado
  - `GET /quarantine` — lista de archivos en cuarentena
  - `POST /quarantine` — agregar archivo a cuarentena (solo admin)
  - `DELETE /quarantine` — restaurar archivo (solo admin)
  - `GET /process/:pid` — info de proceso específico (solo admin)
  - `POST /scan/start` — activar escaneo continuo (solo admin)
  - `POST /scan/stop` — desactivar escaneo (solo admin)
- [x ] Validación de rol en cada endpoint protegido (lógica en C, no en frontend)
- [x ] Respuestas en formato JSON

---

## Fase 3 — Dashboard Web (Frontend)
> HTML + CSS + JavaScript. Sin frameworks obligatorio (opcional).

### 3.1 Sistema de Login
- [x ] Pantalla de login con campos usuario/contraseña
- [x ] `POST /login` al daemon → guardar token y rol en sessionStorage
- [x ] Redirigir según rol o mostrar error si acceso denegado

### 3.2 Vista principal (métricas)
- [x ] Reutilizar gráficas de Práctica 6:
  - Uso de memoria (usado/libre/cache/swap)
  - Page faults (menores y mayores)
  - Páginas activas/inactivas
- [x ] Tabla top 10 procesos por memoria
- [x ] Actualización periódica (polling cada 5s o WebSocket)

### 3.3 Panel de alertas
- [x ] Lista de alertas ordenadas por más reciente
- [x ] Cada alerta: tipo, descripción, severidad, fecha/hora
- [x ] Colores: LOW → azul, MEDIUM → amarillo, HIGH → rojo

### 3.4 Sección de archivos analizados
- [x ] Tabla con: nombre, estado (limpio/modificado/sospechoso), hash parcial, timestamp

### 3.5 Sección de amenazas detectadas
- [x ] Cuando hash coincide: nombre firma, archivo afectado, severidad, descripción

### 3.6 Control de escaneo (solo admin)
- [x ] Botón "Activar escaneo" / "Desactivar escaneo"
- [x ] Solo visible/funcional para rol admin

### 3.7 Búsqueda por PID (solo admin)
- [x ] Input para ingresar PID
- [x ] Mostrar resultado de `sys_get_process_info`

---

## Fase 4 — Documentación

### 4.1 Manual Técnico (`manual_tecnico.md`)
- [x ] Descripción de cada syscall (parámetros, structs, lógica interna)
- [x ] Archivos del kernel modificados
- [x ] Arquitectura del daemon (hilos, flujo de datos)
- [x ] Manejo de errores y concurrencia
- [x ] Cómo compilar y desplegar

### 4.2 Manual de Usuario (`manual_usuario.md`)
- [x ] Instrucciones para iniciar el daemon
- [x ] Cómo acceder al dashboard
- [x ] Guía de uso para administrador
- [x ] Guía de uso para usuario común
- [x ] Capturas de pantalla (opcional)

---

## Fase 5 — Funcionalidad Adicional (10 pts)
> Propuesta a implementar durante el desarrollo

**Candidatos:**
- [x ] Exportar reporte de alertas a JSON/CSV desde el dashboard
- [x ] Notificaciones visuales en tiempo real (badge en la pestaña del navegador)
- [x ] Modo oscuro / claro en el dashboard
- [x ] Historial de alertas persistente (archivo de log estructurado)
- [x ] *(Definir con base en avance del proyecto)*

---

## Estructura del repositorio

```
ProyectoUnico/
├── Kernel/
│   ├── arch/x86/entry/syscalls/syscall_64.tbl
│   ├── include/linux/syscalls.h
│   ├── kernel/Makefile
│   └── kernel/
│       ├── get_process_info/       ← reutilizada de Práctica 5
│       ├── get_system_monitor/
│       ├── file_analize/
│       ├── scan_processes/
│       ├── quarantine/             ← quarantine_file, restore_file, get_quarantine_list
│       └── simulate_panic/
├── ProgramaIntermedio/
│   ├── daemon.c
│   ├── pam_auth.c / pam_auth.h
│   ├── http_server.c / http_server.h
│   ├── alerts.c / alerts.h
│   ├── blacklist.c / blacklist.h
│   ├── hash_blacklist.json
│   ├── Makefile
│   └── daemon (ejecutable compilado)
├── Dashboard/
│   ├── index.html
│   ├── login.html
│   ├── css/
│   ├── js/
│   └── assets/
├── manual_tecnico.md
└── manual_usuario.md
```

---

## Orden de trabajo recomendado

1. **Syscalls del kernel** → compilar y verificar individualmente con programas de prueba
2. **Parser blacklist + lógica SHA-256** en userspace (sin daemon aún)
3. **Daemon básico**: hilos + endpoints HTTP + autenticación PAM
4. **Integración**: conectar daemon con todas las syscalls
5. **Dashboard**: primero métricas, luego alertas, luego controles de admin
6. **Documentación**: redactar en paralelo al desarrollo
7. **Funcionalidad adicional**: última semana

---

## Notas clave

- **Severidad se calcula en el daemon, NUNCA en el kernel**
- **Control de permisos en el daemon, NUNCA en el frontend**
- Las syscalls de cuarentena son **simuladas** — no modificar archivos realmente
- `sys_simulate_panic` usa `printk`, no `panic()` real
- Reutilizar `sys_get_process_info` (syscall 548) de Práctica 5 sin modificarla