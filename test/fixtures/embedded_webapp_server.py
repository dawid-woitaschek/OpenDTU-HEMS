"""Serve production assets with WebApi_webapp.cpp's embedded-file routes.

Unknown paths return index.html, including unembedded Vite /assets files.
Device APIs must be intercepted by the browser fixtures.
"""
import argparse
import hashlib
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', type=int, default=5174)
parser.add_argument('--directory', type=Path, default=Path('webapp_dist'))
args = parser.parse_args()
routes = {
    '/': 'index.html.gz',
    '/index.html': 'index.html.gz',
    '/js/app.js': 'js/app.js.gz',
    '/favicon.ico': 'favicon.ico',
    '/favicon.png': 'favicon.png',
    '/site.webmanifest': 'site.webmanifest',
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        filename = routes.get(urlparse(self.path).path, 'index.html.gz')
        data = (args.directory / filename).read_bytes()
        etag = '"' + hashlib.md5(data).hexdigest() + '"'
        cached = self.headers.get('If-None-Match') == etag
        self.send_response(304 if cached else 200)
        self.send_header('ETag', etag)
        self.send_header('Cache-Control', 'public, must-revalidate')
        self.send_header('Content-Length', '0' if cached else str(len(data)))
        if not cached:
            self.send_header('Content-Type', mimetypes.guess_type(filename.removesuffix('.gz'))[0])
            if filename.endswith('.gz'):
                self.send_header('Content-Encoding', 'gzip')
        self.end_headers()
        if not cached:
            self.wfile.write(data)

    def log_message(self, *_):
        pass


ThreadingHTTPServer(('127.0.0.1', args.port), Handler).serve_forever()
