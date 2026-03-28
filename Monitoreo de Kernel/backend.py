#!/usr/bin/env python3
"""
Práctica 6 – Sistemas Operativos 2
Archivo  : backend.py
Función  : Servidor HTTP ligero.
           - POST /api/metrics  → el daemon C envía JSON de métricas
           - GET  /api/metrics  → el frontend obtiene las últimas métricas
           - GET  /api/process/<pid> → consulta un PID con query_process
           - GET  /             → sirve dashboard.html

Requisitos: Python 3.6+ (sin dependencias externas)
Ejecutar  : python3 backend.py
"""

import json
import subprocess
import os
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse

# ── Configuración ────────────────────────────────────────────
HOST         = "0.0.0.0"   # escuchar en todas las interfaces (accesible desde host)
PORT         = 8080
QUERY_BIN    = "./query_process"   # ruta al binario compilado
DASHBOARD    = "./dashboard.html"  # ruta al HTML del dashboard

# ── Estado compartido ────────────────────────────────────────
latest_metrics = {}
metrics_lock   = threading.Lock()


# ── Handler HTTP ─────────────────────────────────────────────
class Handler(BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        """Silenciar logs por defecto; sólo mostrar errores."""
        if args and len(args) >= 2 and str(args[1]) not in ("200", "204"):
            super().log_message(fmt, *args)

    # ── Cabeceras comunes ────────────────────────────────────
    def _send_json(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path, mime="text/html"):
        try:
            with open(path, "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except FileNotFoundError:
            self.send_error(404, f"No encontrado: {path}")

    # ── OPTIONS (preflight CORS) ─────────────────────────────
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    # ── GET ──────────────────────────────────────────────────
    def do_GET(self):
        parsed = urlparse(self.path)
        path   = parsed.path.rstrip("/")

        if path == "" or path == "/":
            self._send_file(DASHBOARD)

        elif path == "/api/metrics":
            with metrics_lock:
                data = dict(latest_metrics)
            if not data:
                self._send_json({"error": "Sin datos aún"}, 503)
            else:
                self._send_json(data)

        elif path.startswith("/api/process/"):
            parts = path.split("/")
            pid   = parts[-1] if parts else ""
            self._handle_process_query(pid)

        else:
            self.send_error(404, "Ruta no encontrada")

    # ── POST ─────────────────────────────────────────────────
    def do_POST(self):
        if self.path.rstrip("/") != "/api/metrics":
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", 0))
        body   = self.rfile.read(length)

        try:
            data = json.loads(body)
        except json.JSONDecodeError as e:
            self._send_json({"error": f"JSON inválido: {e}"}, 400)
            return

        with metrics_lock:
            latest_metrics.clear()
            latest_metrics.update(data)

        print(f"[backend] Métricas actualizadas: "
              f"mem_used={data.get('mem_used_kb', '?')} KB")
        self._send_json({"status": "ok"})

    # ── Consulta de proceso individual ───────────────────────
    def _handle_process_query(self, pid):
        try:
            pid_int = int(pid)
        except ValueError:
            self._send_json({"error": "PID inválido"}, 400)
            return

        if not os.path.exists(QUERY_BIN):
            self._send_json({"error": f"Binario {QUERY_BIN} no encontrado"}, 500)
            return

        try:
            result = subprocess.run(
                [QUERY_BIN, str(pid_int)],
                capture_output=True,
                text=True,
                timeout=5
            )
            data = json.loads(result.stdout.strip())
            self._send_json(data)
        except subprocess.TimeoutExpired:
            self._send_json({"error": "Timeout al consultar proceso"}, 504)
        except json.JSONDecodeError:
            self._send_json({"error": "Respuesta inválida del binario",
                             "raw": result.stdout}, 500)
        except Exception as e:
            self._send_json({"error": str(e)}, 500)


# ── Main ──────────────────────────────────────────────────────
if __name__ == "__main__":
    server = HTTPServer((HOST, PORT), Handler)
    print(f"[backend] Servidor escuchando en http://{HOST}:{PORT}")
    print(f"[backend] Dashboard → http://localhost:{PORT}/")
    print(f"[backend] Métricas  → http://localhost:{PORT}/api/metrics")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[backend] Detenido.")
        server.server_close()
