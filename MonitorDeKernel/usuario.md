# Manual de Usuario
## KernelGuard — Sistema de Monitoreo y Seguridad
**Sistemas Operativos 2 — USAC FIUSAC**

---

## Tabla de Contenidos

1. [Requisitos](#1-requisitos)
2. [Iniciar el Sistema](#2-iniciar-el-sistema)
3. [Acceder al Dashboard](#3-acceder-al-dashboard)
4. [Inicio de Sesión](#4-inicio-de-sesión)
5. [Guía para Usuario Común](#5-guía-para-usuario-común)
6. [Guía para Administrador](#6-guía-para-administrador)
7. [Solución de Problemas](#7-solución-de-problemas)

---

## 1. Requisitos

- Kernel Linux 6.12.69 compilado e instalado
- Daemon compilado en `ProgramaIntermedio/`
- Navegador web (Chromium o Firefox)
- Usuario perteneciente al grupo `admin_user` o `common_user`

---

## 2. Iniciar el Sistema

Abre una terminal y ejecuta:

```bash
cd /home/lubuntu/Documents/SO2/ProyectoUnico/ProgramaIntermedio
sudo ./daemon
```

Debes ver:
```
[daemon] Blacklist cargada: 8 firmas
[daemon] Corriendo en http://0.0.0.0:8080
[daemon] Directorio monitoreado: ./monitor_dir
```

Deja esta terminal abierta mientras uses el sistema.

> **Screenshot sugerido:** Terminal con el daemon corriendo.

---

## 3. Acceder al Dashboard

Abre una nueva terminal y ejecuta:

```bash
cd /home/lubuntu/Documents/SO2/ProyectoUnico/Dashboard
python3 -m http.server 9090
```

Luego abre Chromium con:

```bash
chromium-browser --disable-web-security --user-data-dir=/tmp/chrome-test http://localhost:9090 &
```

> **Screenshot sugerido:** Pantalla de login del dashboard.

---

## 4. Inicio de Sesión

Ingresa tu nombre de usuario y contraseña del sistema operativo. El sistema verificará que pertenezcas a uno de los grupos autorizados.

| Grupo | Rol asignado |
|---|---|
| `admin_user` | Administrador |
| `common_user` | Usuario |
| Ambos grupos | Administrador |
| Ninguno | Acceso denegado |

Si las credenciales son incorrectas o no perteneces a ningún grupo, verás un mensaje de error.

> **Screenshot sugerido:** Mensaje de error de login vs. login exitoso.

---

## 5. Guía para Usuario Común

Como usuario común tienes acceso a 4 secciones del dashboard.

### Métricas

Muestra el estado actual del sistema en tiempo real, actualizado cada 5 segundos.

**Tarjetas superiores:**
- **Memoria Usada** — RAM consumida por aplicaciones en MB
- **Memoria Libre** — RAM disponible en MB
- **Swap Usada** — Memoria de intercambio en uso en MB
- **Páginas Activas** — Número de páginas de memoria en uso activo

**Gráficas:**
- **Desglose de Memoria** — Proporción de memoria usada, libre y en caché
- **Estado de Páginas** — Páginas activas vs. inactivas
- **RAM vs Swap** — Evolución histórica del uso de memoria
- **Fallos de Página** — Frecuencia de minor y major page faults

**Tabla de Procesos** — Los 10 procesos que más memoria consumen, con barra visual de uso.

> **Screenshot sugerido:** Vista completa de la sección Métricas.

### Alertas

Lista de eventos de seguridad generados por el sistema, ordenados del más reciente al más antiguo.

Cada alerta muestra:
- **Severidad** con código de color:
  - 🟢 **LOW** — Informativo (archivo nuevo detectado)
  - 🟡 **MEDIUM** — Advertencia (alto consumo de memoria, archivo modificado)
  - 🔴 **HIGH** — Crítico (hash malicioso, consumo extremo de recursos)
- Tipo de evento (memoria / proceso / archivo / sistema)
- Descripción detallada
- Fecha y hora del evento

> **Screenshot sugerido:** Panel de alertas con ejemplos de los tres niveles.

### Archivos

Lista de archivos monitoreados en el directorio de escaneo.

| Campo | Descripción |
|---|---|
| Ruta | Ubicación completa del archivo |
| Estado | LIMPIO / MODIFICADO / SOSPECHOSO |
| Hash | Primeros 16 caracteres del SHA-256 |
| Última Modificación | Fecha y hora |

> **Screenshot sugerido:** Tabla de archivos con diferentes estados.

### Amenazas

Muestra tarjetas detalladas cuando el hash de un archivo coincide con la base de datos de amenazas conocidas.

Cada tarjeta incluye:
- Nombre de la amenaza (firma)
- Archivo afectado
- Severidad
- Descripción de la amenaza
- Timestamp de detección

> **Screenshot sugerido:** Tarjeta de amenaza detectada.

---

## 6. Guía para Administrador

El administrador tiene acceso a todo lo anterior más el tab **Admin**.

### Tab Admin

#### Control de Escaneo

Permite activar o detener el escaneo continuo de archivos:

- **▶ Activar Escaneo** — El sistema revisa el directorio monitoreado cada 10 segundos
- **■ Detener Escaneo** — El escaneo se pausa hasta ser reactivado

> **Screenshot sugerido:** Botones de control de escaneo con el indicador de estado.

#### Consulta de Proceso por PID

Ingresa el PID de cualquier proceso activo para ver su información detallada:

1. Escribe el PID en el campo de texto
2. Presiona **Consultar** o la tecla Enter
3. El sistema mostrará: PID, nombre del proceso, tiempo de CPU y memoria usada

> **Screenshot sugerido:** Campo PID con resultado de consulta.

#### Archivos en Cuarentena

Lista de archivos marcados automáticamente como peligrosos por el sistema.

- Los archivos en cuarentena **no son eliminados ni movidos**, solo marcados como no confiables
- Para restaurar un archivo a estado seguro, presiona el botón **Restaurar**

> **Screenshot sugerido:** Lista de cuarentena con botón de restaurar.

### Agregar archivos al directorio monitoreado

Para probar el escaneo, copia archivos al directorio monitoreado:

```bash
cp /ruta/al/archivo \
   /home/lubuntu/Documents/SO2/ProyectoUnico/ProgramaIntermedio/monitor_dir/
```

El sistema los detectará en el siguiente ciclo de escaneo (~10 segundos).

---

## 7. Solución de Problemas

| Problema | Solución |
|---|---|
| "No se pudo conectar al daemon" | Verifica que el daemon esté corriendo con `ps aux \| grep daemon` |
| "Credenciales inválidas" | Verifica que tu usuario pertenezca a `admin_user` o `common_user` con `groups tu_usuario` |
| Dashboard no carga | Asegúrate de usar el flag `--disable-web-security` en Chromium |
| No aparecen archivos en la sección Archivos | Copia archivos al directorio `monitor_dir/` y espera ~10 segundos |
| Amenazas no aparecen | El hash del archivo debe coincidir con la blacklist. Prueba con `touch monitor_dir/test.bin` |
| Daemon termina con error de puerto | Ejecuta `sudo pkill -f "./daemon"` y vuelve a iniciarlo |