#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Receives TrophyNX's "Send logs" (debug screen in the app, Y) on your network, e.g. on a Raspberry Pi.

    python3 trophynx_logserver.py [--port 8421] [--dir ~/trophynx-logs]

On the Switch, put this machine's address in sdmc:/switch/TrophyNX/log_server.txt (one line, "192.168.1.20" or
"192.168.1.20:8421"). Each upload is saved to <dir>/<date-time>/ and copied to <dir>/latest/. The folder is also
served read-only over HTTP (http://<this machine>:8421/latest/live_log.txt), so a PC can read the logs directly.

Meant for a home network: no password, anyone on the network can send logs and read them. Only TrophyNX logs are
in there (no saves, no keys), but don't expose the port to the internet. Python 3.7+, standard library only.
"""
import argparse
import datetime
import functools
import http.server
import os
import re
import shutil
import sys

MAX_BODY = 64 * 1024 * 1024
SAFE_PART = re.compile(r'^[A-Za-z0-9._-]+$')


def safe_name(n):
    if not n or len(n) > 160 or n.startswith('/') or n.endswith('/'):
        return False
    return all(p not in ('', '.', '..') and SAFE_PART.match(p) for p in n.split('/'))


def decode(b):
    """Same format as include/logbundle.h: b"TNXLOGS1\\n" then per file b"<name>\\t<size>\\n" + data + b"\\n"."""
    if not b.startswith(b'TNXLOGS1\n'):
        raise ValueError('not a TrophyNX log bundle')
    p, out = 9, []
    while p < len(b):
        nl = b.index(b'\n', p)
        name, size = b[p:nl].decode('utf-8').split('\t')
        n = int(size)
        p = nl + 1
        if not safe_name(name) or p + n >= len(b) or b[p + n:p + n + 1] != b'\n':
            raise ValueError('bad entry ' + repr(name))
        out.append((name, b[p:p + n]))
        p += n + 1
    return out


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path.rstrip('/') != '/trophynx/logs':
            return self.send_error(404)
        n = int(self.headers.get('Content-Length') or 0)
        if n <= 0 or n > MAX_BODY:
            return self.send_error(413 if n > MAX_BODY else 411)
        body = b''
        while len(body) < n:
            chunk = self.rfile.read(n - len(body))
            if not chunk:
                break
            body += chunk
        try:
            files = decode(body)
        except Exception as e:   # noqa: BLE001 - any malformed upload is just refused
            return self.send_error(400, str(e))
        root = self.directory
        stamp = datetime.datetime.now().strftime('%Y-%m-%d_%H-%M-%S')
        dest = os.path.join(root, stamp)
        k = 1
        while os.path.exists(dest):
            k += 1
            dest = os.path.join(root, '%s_%d' % (stamp, k))
        for name, data in files:
            path = os.path.join(dest, *name.split('/'))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, 'wb') as f:
                f.write(data)
        latest = os.path.join(root, 'latest')
        shutil.rmtree(latest, ignore_errors=True)
        shutil.copytree(dest, latest)
        msg = '%d files, %d KB -> %s\n' % (len(files), len(body) >> 10, os.path.basename(dest))
        sys.stderr.write('[%s] %s from %s (TrophyNX %s)\n' % (
            stamp, msg.strip(), self.client_address[0], self.headers.get('X-TrophyNX', '?')))
        out = msg.encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/plain; charset=utf-8')
        self.send_header('Content-Length', str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()

    def guess_type(self, path):   # logs and json open as text in a browser
        return 'text/plain; charset=utf-8' if path.endswith(('.txt', '.json', '.log')) else super().guess_type(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--port', type=int, default=8421)
    ap.add_argument('--dir', default=os.path.expanduser('~/trophynx-logs'))
    ap.add_argument('--bind', default='0.0.0.0')
    a = ap.parse_args()
    os.makedirs(a.dir, exist_ok=True)
    handler = functools.partial(Handler, directory=a.dir)
    srv = http.server.ThreadingHTTPServer((a.bind, a.port), handler)
    sys.stderr.write('TrophyNX log receiver on port %d, saving to %s\n' % (a.port, a.dir))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == '__main__':
    main()
