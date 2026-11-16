import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class DemoHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/healthz":
            self._send(200, b"ok\n", "text/plain; charset=utf-8")
            return

        if self.path != "/":
            self._send(404, b"not found\n", "text/plain; charset=utf-8")
            return

        response = {
            "app": "nero-demo-web",
            "environment": os.environ.get("APP_ENV", "unknown"),
            "log_level": os.environ.get("LOG_LEVEL", "unknown"),
        }
        body = json.dumps(response).encode("utf-8")
        self._send(200, body, "application/json")

    def _send(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


if __name__ == "__main__":
    ThreadingHTTPServer(("0.0.0.0", 8080), DemoHandler).serve_forever()
