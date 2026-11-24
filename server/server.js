// Smart Data Center OS Simulator - Lightweight HTTP Bridge
// Serves static frontend assets and forwards all /api/* calls to the compiled
// C++ simulation engine (bin/dc_sim.exe). Zero external npm packages required.
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const FRONTEND_DIR = path.join(ROOT, 'frontend');
const ENGINE_BIN = path.join(ROOT, 'bin', process.platform === 'win32' ? 'dc_sim.exe' : 'dc_sim');
const PORT = parseInt(process.env.PORT || '8080', 10);

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

function runEngine(payloadObj) {
  return new Promise((resolve, reject) => {
    if (!fs.existsSync(ENGINE_BIN)) {
      return reject(new Error(`C++ engine not found at ${ENGINE_BIN}. Run scripts/build.ps1 first.`));
    }
    const proc = spawn(ENGINE_BIN, [], { cwd: ROOT, windowsHide: true });
    let stdout = '';
    let stderr = '';
    const timer = setTimeout(() => {
      proc.kill();
      reject(new Error('C++ engine timed out (15s)'));
    }, 15000);

    proc.stdout.on('data', (d) => { stdout += d.toString('utf8'); });
    proc.stderr.on('data', (d) => { stderr += d.toString('utf8'); });
    proc.on('error', (err) => { clearTimeout(timer); reject(err); });
    proc.on('close', () => {
      clearTimeout(timer);
      try {
        const parsed = JSON.parse(stdout.trim());
        resolve(parsed);
      } catch (e) {
        reject(new Error(`Invalid JSON from C++ engine: ${e.message}. stderr=${stderr}`));
      }
    });

    proc.stdin.write(JSON.stringify(payloadObj));
    proc.stdin.end();
  });
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (chunk) => {
      data += chunk;
      if (data.length > 5 * 1024 * 1024) {
        reject(new Error('Request body too large'));
        req.destroy();
      }
    });
    req.on('end', () => resolve(data));
    req.on('error', reject);
  });
}

function sendJson(res, status, obj) {
  const body = JSON.stringify(obj);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Cache-Control': 'no-store',
  });
  res.end(body);
}

const ROUTES = {
  'GET /api/info': () => ({ action: 'INFO' }),
  'GET /api/samples': () => ({ action: 'SAMPLES' }),
  'POST /api/generate': (b) => ({ action: 'GENERATE', ...b }),
  'POST /api/simulate': (b) => ({ action: 'SIMULATE', ...b }),
  'POST /api/cpu': (b) => ({ action: 'CPU', ...b }),
  'POST /api/cpu/compare': (b) => ({ action: 'CPU_COMPARE', ...b }),
  'POST /api/memory': (b) => ({ action: 'MEMORY', ...b }),
  'POST /api/memory/compare': (b) => ({ action: 'MEMORY_COMPARE', ...b }),
  'POST /api/disk': (b) => ({ action: 'DISK', ...b }),
  'POST /api/disk/compare': (b) => ({ action: 'DISK_COMPARE', ...b }),
  'POST /api/sync': (b) => ({ action: 'SYNC', ...b }),
};

function serveStatic(reqPath, res) {
  let clean = reqPath.split('?')[0];
  if (clean === '/') clean = '/index.html';
  const full = path.normalize(path.join(FRONTEND_DIR, clean));
  if (!full.startsWith(FRONTEND_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }
  fs.readFile(full, (err, content) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end('Not found');
      return;
    }
    const ext = path.extname(full).toLowerCase();
    res.writeHead(200, { 'Content-Type': MIME[ext] || 'application/octet-stream' });
    res.end(content);
  });
}

const server = http.createServer(async (req, res) => {
  const urlPath = (req.url || '/').split('?')[0];
  const key = `${req.method} ${urlPath}`;

  if (urlPath.startsWith('/api/')) {
    const handler = ROUTES[key];
    if (!handler) {
      return sendJson(res, 404, { ok: false, error: `Unknown API route: ${key}` });
    }
    try {
      let body = {};
      if (req.method === 'POST') {
        const raw = await readBody(req);
        body = raw.trim() ? JSON.parse(raw) : {};
      }
      const result = await runEngine(handler(body));
      sendJson(res, result.ok ? 200 : 400, result);
    } catch (err) {
      sendJson(res, 500, { ok: false, error: err.message });
    }
    return;
  }

  serveStatic(req.url || '/', res);
});

const HOST = process.env.HOST || '0.0.0.0';

server.listen(PORT, HOST, () => {
  console.log(`Smart Data Center OS Simulator running at http://${HOST}:${PORT}`);
  console.log(`C++ Engine binary: ${ENGINE_BIN}`);
});
