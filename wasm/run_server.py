from http import server
from socketserver import ThreadingMixIn
import os
import sys

port = 8000
if len(sys.argv) >= 2:
  port = int(sys.argv[1])

# Serve the directory holding this script, not the current working directory.
# The old behaviour only worked when launched from inside web/, which made the
# server silently 404 everything (or die) depending on where it was started.
WEB_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "web")


class Handler(server.SimpleHTTPRequestHandler):
  def __init__(self, *args, **kwargs):
    super().__init__(*args, directory=WEB_DIR, **kwargs)

  def end_headers(self):
    self.send_header("Cross-Origin-Opener-Policy", "same-origin")
    self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
    # Never let the browser reuse a stale script.js or asset chunk between
    # reloads; a cached script.js keeps an old data_version and silently
    # re-uses the previous IndexedDB assets.
    self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
    self.send_header("Pragma", "no-cache")
    self.send_header("Expires", "0")

    server.SimpleHTTPRequestHandler.end_headers(self)

  def log_message(self, fmt, *args):
    # Keep the 20MB asset chunks from flooding the log.
    if not str(args[1] if len(args) > 1 else "").endswith((".00", ".01", ".02",
                                                            ".03", ".04", ".05",
                                                            ".06", ".07", ".08",
                                                            ".09", ".10", ".11",
                                                            ".12")):
      super().log_message(fmt, *args)


class Server(ThreadingMixIn, server.HTTPServer):
  daemon_threads = True
  allow_reuse_address = True


if __name__ == '__main__':
  with Server(("", port), Handler) as httpd:
    print(f"Serving {WEB_DIR} on http://localhost:{port}", flush=True)
    httpd.serve_forever()