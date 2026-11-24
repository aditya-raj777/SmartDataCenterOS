// DataCenterOS — Virtual Infrastructure Management & OS Simulation Platform
// HTTP Bridge & Application Management Server
// Serves static frontend assets, manages virtual servers & incidents,
// and forwards simulation requests to the compiled C++ engine (bin/dc_sim.exe).
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');
const { spawn } = require('child_process');
const crypto = require('crypto');

const ROOT = path.resolve(__dirname, '..');
const FRONTEND_DIR = path.join(ROOT, 'frontend');
const DATA_DIR = path.join(ROOT, 'data');
const STATE_FILE = path.join(DATA_DIR, 'state.json');
const ENGINE_BIN = path.join(ROOT, 'bin', process.platform === 'win32' ? 'dc_sim.exe' : 'dc_sim');
const PORT = parseInt(process.env.PORT || '8080', 10);
const HOST = process.env.HOST || '0.0.0.0';

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
};

// -----------------------------------------------------------------------------
// In-Memory Enterprise Application State & Persistence
// -----------------------------------------------------------------------------
const appState = {
  servers: [],
  incidents: [],
  events: [],
};

function generateId(prefix = 'id') {
  return `${prefix}-${Date.now().toString(36)}-${crypto.randomBytes(3).toString('hex')}`;
}

function initDefaultState() {
  if (appState.servers.length === 0) {
    appState.servers = [
      {
        id: 'srv-web-01',
        name: 'Production-Web-01',
        type: 'Application Gateway',
        status: 'running', // running, stopped, failed, provisioning, maintenance
        cpu: { cores: 8, utilization: 24.5 },
        ram: { total: 16384, used: 6420 },
        storage: { total: 500, used: 142 },
        processes: [1, 2],
        createdAt: Date.now() - 86400000 * 3,
        uptime: 259200,
        ipAddress: '10.0.1.14',
        region: 'us-east-1a'
      },
      {
        id: 'srv-db-primary',
        name: 'Database-Primary',
        type: 'Relational DB Cluster',
        status: 'running',
        cpu: { cores: 16, utilization: 48.2 },
        ram: { total: 32768, used: 21540 },
        storage: { total: 2000, used: 840 },
        processes: [3, 4],
        createdAt: Date.now() - 86400000 * 7,
        uptime: 604800,
        ipAddress: '10.0.2.22',
        region: 'us-east-1b'
      },
      {
        id: 'srv-ml-compute',
        name: 'ML-Inference-Node',
        type: 'GPU / Tensor Compute',
        status: 'stopped',
        cpu: { cores: 32, utilization: 0.0 },
        ram: { total: 65536, used: 0 },
        storage: { total: 4000, used: 620 },
        processes: [],
        createdAt: Date.now() - 86400000 * 1,
        uptime: 0,
        ipAddress: '10.0.3.50',
        region: 'us-east-1c'
      }
    ];
  }

  if (appState.incidents.length === 0) {
    appState.incidents = [
      {
        id: 'inc-001',
        title: 'High Disk I/O Queue Depth on Database-Primary',
        severity: 'warning', // critical, warning, info
        status: 'active',   // active, resolved
        server: 'Database-Primary',
        serverId: 'srv-db-primary',
        description: 'Sustained cylinder queue depth exceeded 12 operations during concurrent batch snapshot writes.',
        createdAt: Date.now() - 3600000 * 2,
        resolvedAt: null,
        resolution: null
      },
      {
        id: 'inc-002',
        title: 'Elevated Memory Fragmentation in Kernel Pool',
        severity: 'info',
        status: 'resolved',
        server: 'Production-Web-01',
        serverId: 'srv-web-01',
        description: 'Dynamic partition allocation resulted in 128KB external fragmentation hole. Compacted automatically.',
        createdAt: Date.now() - 3600000 * 14,
        resolvedAt: Date.now() - 3600000 * 12,
        resolution: 'First-fit allocation compaction cycle completed without process eviction.'
      }
    ];
  }

  if (appState.events.length === 0) {
    addSystemEvent('SYSTEM', 'DataCenterOS Enterprise Platform initialized', 'info', 'SYS-KERNEL');
    addSystemEvent('SERVER', 'Virtual Server Production-Web-01 online [10.0.1.14]', 'info', 'srv-web-01');
    addSystemEvent('SERVER', 'Virtual Server Database-Primary online [10.0.2.22]', 'info', 'srv-db-primary');
  }
}

function loadPersistedState() {
  try {
    if (fs.existsSync(STATE_FILE)) {
      const raw = fs.readFileSync(STATE_FILE, 'utf8');
      const data = JSON.parse(raw);
      if (Array.isArray(data.servers)) appState.servers = data.servers;
      if (Array.isArray(data.incidents)) appState.incidents = data.incidents;
      if (Array.isArray(data.events)) appState.events = data.events;
    }
  } catch (err) {
    console.warn(`[WARN] Could not load state from ${STATE_FILE}: ${err.message}`);
  }
  initDefaultState();
}

function persistState() {
  try {
    if (!fs.existsSync(DATA_DIR)) {
      fs.mkdirSync(DATA_DIR, { recursive: true });
    }
    fs.writeFileSync(STATE_FILE, JSON.stringify(appState, null, 2), 'utf8');
  } catch (err) {
    console.error(`[ERROR] Failed to persist state: ${err.message}`);
  }
}

function addSystemEvent(type, message, severity = 'info', source = 'PLATFORM') {
  const ev = {
    id: generateId('ev'),
    timestamp: Date.now(),
    type,
    source,
    message,
    severity
  };
  appState.events.unshift(ev);
  if (appState.events.length > 500) {
    appState.events.length = 500;
  }
  return ev;
}

// -----------------------------------------------------------------------------
// C++ Engine Process Bridge
// -----------------------------------------------------------------------------
function runEngine(payloadObj) {
  return new Promise((resolve, reject) => {
    if (!fs.existsSync(ENGINE_BIN)) {
      return reject(new Error(`C++ simulation engine not found at ${ENGINE_BIN}.`));
    }
    const proc = spawn(ENGINE_BIN, [], { cwd: ROOT, windowsHide: true });
    let stdout = '';
    let stderr = '';
    const timer = setTimeout(() => {
      proc.kill();
      reject(new Error('C++ simulation engine execution timed out (15s)'));
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

// -----------------------------------------------------------------------------
// HTTP Utilities
// -----------------------------------------------------------------------------
function readBody(req) {
  return new Promise((resolve, reject) => {
    let data = '';
    req.on('data', (chunk) => {
      data += chunk;
      if (data.length > 10 * 1024 * 1024) {
        reject(new Error('Request payload exceeds limit (10MB)'));
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
    'Cache-Control': 'no-store, no-cache, must-revalidate',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
  });
  res.end(body);
}

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
      res.end('Asset not found');
      return;
    }
    const ext = path.extname(full).toLowerCase();
    res.writeHead(200, {
      'Content-Type': MIME[ext] || 'application/octet-stream',
      'Cache-Control': ext === '.html' ? 'no-cache' : 'public, max-age=3600'
    });
    res.end(content);
  });
}

// -----------------------------------------------------------------------------
// HTTP Request Router
// -----------------------------------------------------------------------------
const server = http.createServer(async (req, res) => {
  // CORS Preflight
  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, PUT, DELETE, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type',
    });
    return res.end();
  }

  const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  const pathname = parsedUrl.pathname;
  const method = req.method;

  // Handle all API routes
  if (pathname.startsWith('/api/')) {
    try {
      let body = {};
      if (method === 'POST' || method === 'PUT') {
        const raw = await readBody(req);
        body = raw.trim() ? JSON.parse(raw) : {};
      }

      // ==========================================
      // 1. Core Simulation Engine Direct Endpoints
      // ==========================================
      if (method === 'GET' && pathname === '/api/info') {
        const r = await runEngine({ action: 'INFO' });
        return sendJson(res, 200, r);
      }
      if (method === 'GET' && pathname === '/api/samples') {
        const r = await runEngine({ action: 'SAMPLES' });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/generate') {
        const r = await runEngine({ action: 'GENERATE', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/simulate') {
        const r = await runEngine({ action: 'SIMULATE', ...body });
        addSystemEvent('SIMULATION', `Completed integrated simulation (${r.data?.summary?.makespan || 0} ticks, ${r.data?.summary?.completed || 0} jobs)`, 'info', 'SIM-ENGINE');
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/cpu') {
        const r = await runEngine({ action: 'CPU', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/cpu/compare') {
        const r = await runEngine({ action: 'CPU_COMPARE', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/memory') {
        const r = await runEngine({ action: 'MEMORY', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/memory/compare') {
        const r = await runEngine({ action: 'MEMORY_COMPARE', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/disk') {
        const r = await runEngine({ action: 'DISK', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/disk/compare') {
        const r = await runEngine({ action: 'DISK_COMPARE', ...body });
        return sendJson(res, 200, r);
      }
      if (method === 'POST' && pathname === '/api/sync') {
        const r = await runEngine({ action: 'SYNC', ...body });
        return sendJson(res, 200, r);
      }

      // ==========================================
      // 2. Deadlock Detection & Scenarios
      // ==========================================
      if (method === 'POST' && pathname === '/api/deadlock/detect') {
        const r = await runEngine({ action: 'DEADLOCK_DETECT', scenario: body.scenario || [] });
        if (r.data?.hasDeadlock) {
          addSystemEvent('DEADLOCK', `Deadlock detected: ${r.data.description}`, 'critical', 'DEADLOCK-DETECTOR');
        }
        return sendJson(res, 200, r);
      }
      if (method === 'GET' && pathname === '/api/deadlock/scenarios') {
        const r = await runEngine({ action: 'DEADLOCK_SCENARIOS' });
        return sendJson(res, 200, r);
      }

      // ==========================================
      // 3. Virtual Server Fleet Management
      // ==========================================
      if (method === 'GET' && pathname === '/api/servers') {
        return sendJson(res, 200, { ok: true, data: appState.servers });
      }

      if (method === 'POST' && pathname === '/api/servers') {
        const srv = {
          id: generateId('srv'),
          name: body.name || `Server-${appState.servers.length + 1}`,
          type: body.type || 'Standard Compute Node',
          status: 'running',
          cpu: { cores: parseInt(body.cpu?.cores || body.cores || 4, 10), utilization: 0 },
          ram: { total: parseInt(body.ram?.total || body.ram || 8192, 10), used: 0 },
          storage: { total: parseInt(body.storage?.total || body.storage || 500, 10), used: 0 },
          processes: [],
          createdAt: Date.now(),
          uptime: 0,
          ipAddress: `10.0.${Math.floor(Math.random() * 5 + 1)}.${Math.floor(Math.random() * 200 + 10)}`,
          region: body.region || 'us-east-1a'
        };
        appState.servers.push(srv);
        addSystemEvent('SERVER', `Provisioned virtual server ${srv.name} (${srv.cpu.cores} cores, ${srv.ram.total}MB RAM)`, 'info', srv.id);
        persistState();
        return sendJson(res, 201, { ok: true, data: srv });
      }

      // Single server actions: /api/servers/:id and /api/servers/:id/:action
      const serverMatch = pathname.match(/^\/api\/servers\/([^/]+)(?:\/([^/]+))?$/);
      if (serverMatch) {
        const serverId = serverMatch[1];
        const action = serverMatch[2];
        const srv = appState.servers.find((s) => s.id === serverId);

        if (!srv) {
          return sendJson(res, 404, { ok: false, error: `Virtual server ${serverId} not found` });
        }

        if (!action) {
          if (method === 'GET') {
            return sendJson(res, 200, { ok: true, data: srv });
          }
          if (method === 'PUT') {
            Object.assign(srv, body);
            persistState();
            return sendJson(res, 200, { ok: true, data: srv });
          }
          if (method === 'DELETE') {
            appState.servers = appState.servers.filter((s) => s.id !== serverId);
            addSystemEvent('SERVER', `Decommissioned virtual server ${srv.name}`, 'warning', serverId);
            persistState();
            return sendJson(res, 200, { ok: true, data: { deleted: true, id: serverId } });
          }
        } else if (method === 'POST') {
          if (action === 'start') {
            srv.status = 'running';
            addSystemEvent('SERVER', `Virtual server ${srv.name} started`, 'info', srv.id);
          } else if (action === 'stop') {
            srv.status = 'stopped';
            srv.cpu.utilization = 0;
            addSystemEvent('SERVER', `Virtual server ${srv.name} stopped`, 'warning', srv.id);
          } else if (action === 'restart') {
            srv.status = 'running';
            addSystemEvent('SERVER', `Virtual server ${srv.name} restarted`, 'info', srv.id);
          } else if (action === 'fail') {
            srv.status = 'failed';
            srv.cpu.utilization = 0;
            const inc = {
              id: generateId('inc'),
              title: `Hardware Fault / Node Outage on ${srv.name}`,
              severity: 'critical',
              status: 'active',
              server: srv.name,
              serverId: srv.id,
              description: 'Kernel panic or unrecoverable compute node crash simulated.',
              createdAt: Date.now(),
              resolvedAt: null,
              resolution: null
            };
            appState.incidents.unshift(inc);
            addSystemEvent('INCIDENT', `CRITICAL: Node outage on ${srv.name}`, 'critical', srv.id);
          } else if (action === 'recover') {
            srv.status = 'running';
            // Resolve any open incidents for this server
            appState.incidents.forEach((inc) => {
              if (inc.serverId === srv.id && inc.status === 'active') {
                inc.status = 'resolved';
                inc.resolvedAt = Date.now();
                inc.resolution = 'Node recovered and health checks verified.';
              }
            });
            addSystemEvent('SERVER', `Virtual server ${srv.name} recovered to healthy state`, 'info', srv.id);
          } else {
            return sendJson(res, 400, { ok: false, error: `Invalid server action '${action}'` });
          }
          persistState();
          return sendJson(res, 200, { ok: true, data: srv });
        }
      }

      // ==========================================
      // 4. Incident Management
      // ==========================================
      if (method === 'GET' && pathname === '/api/incidents') {
        const severity = parsedUrl.searchParams.get('severity');
        const status = parsedUrl.searchParams.get('status');
        let results = appState.incidents;
        if (severity && severity !== 'all') {
          results = results.filter((i) => i.severity === severity);
        }
        if (status && status !== 'all') {
          results = results.filter((i) => i.status === status);
        }
        return sendJson(res, 200, { ok: true, data: results });
      }

      if (method === 'POST' && pathname === '/api/incidents') {
        const inc = {
          id: generateId('inc'),
          title: body.title || 'Infrastructure Incident Alert',
          severity: body.severity || 'warning',
          status: 'active',
          server: body.server || 'DataCenter Cluster',
          serverId: body.serverId || null,
          description: body.description || 'System alert triggered by monitoring agent.',
          createdAt: Date.now(),
          resolvedAt: null,
          resolution: null
        };
        appState.incidents.unshift(inc);
        addSystemEvent('INCIDENT', `New incident logged: ${inc.title}`, inc.severity, inc.serverId || 'MONITOR');
        persistState();
        return sendJson(res, 201, { ok: true, data: inc });
      }

      const incMatch = pathname.match(/^\/api\/incidents\/([^/]+)\/resolve$/);
      if (incMatch && method === 'POST') {
        const incId = incMatch[1];
        const inc = appState.incidents.find((i) => i.id === incId);
        if (!inc) return sendJson(res, 404, { ok: false, error: 'Incident not found' });
        inc.status = 'resolved';
        inc.resolvedAt = Date.now();
        inc.resolution = body.resolution || 'Resolved by infrastructure operator.';
        addSystemEvent('INCIDENT', `Incident ${inc.title} resolved`, 'info', inc.id);
        persistState();
        return sendJson(res, 200, { ok: true, data: inc });
      }

      // ==========================================
      // 5. System Event Logs
      // ==========================================
      if (method === 'GET' && pathname === '/api/events') {
        const type = parsedUrl.searchParams.get('type');
        const severity = parsedUrl.searchParams.get('severity');
        const search = (parsedUrl.searchParams.get('search') || '').toLowerCase();
        const limit = parseInt(parsedUrl.searchParams.get('limit') || '100', 10);

        let filtered = appState.events;
        if (type && type !== 'all') {
          filtered = filtered.filter((e) => e.type.toLowerCase() === type.toLowerCase());
        }
        if (severity && severity !== 'all') {
          filtered = filtered.filter((e) => e.severity === severity);
        }
        if (search) {
          filtered = filtered.filter((e) =>
            e.message.toLowerCase().includes(search) ||
            e.source.toLowerCase().includes(search) ||
            e.type.toLowerCase().includes(search)
          );
        }
        return sendJson(res, 200, { ok: true, data: filtered.slice(0, limit) });
      }

      // ==========================================
      // 6. Platform Scenarios Lab
      // ==========================================
      if (method === 'GET' && pathname === '/api/scenarios') {
        const scenarios = [
          {
            id: 'scenario_cpu_spike',
            name: 'Extreme Compute Spike (CPU Saturation)',
            category: 'Performance',
            icon: '🔥',
            description: 'Simulates sudden arrival of high-burst compute jobs with FCFS vs SRTF scheduling comparison.',
            config: { cpuAlgorithm: 'SRTF', quantum: 2, memoryTotal: 1024, memoryStrategy: 'FIRST_FIT', diskAlgorithm: 'SCAN' }
          },
          {
            id: 'scenario_memory_leak',
            name: 'Memory Fragmentation & Exhaustion',
            category: 'Memory',
            icon: '💾',
            description: 'Tight 512KB address space with staggered multi-size allocations demonstrating external fragmentation.',
            config: { cpuAlgorithm: 'RR', quantum: 2, memoryTotal: 512, memoryStrategy: 'BEST_FIT', diskAlgorithm: 'FCFS' }
          },
          {
            id: 'scenario_disk_thrash',
            name: 'Disk I/O Bottleneck & Head Thrashing',
            category: 'Storage',
            icon: '💿',
            description: 'Scattered multi-cylinder I/O load across cylinders 14 to 183 to test SCAN and C-LOOK head travel efficiency.',
            config: { cpuAlgorithm: 'RR', quantum: 2, memoryTotal: 1024, memoryStrategy: 'FIRST_FIT', diskAlgorithm: 'CSCAN', diskHead: 53 }
          },
          {
            id: 'scenario_deadlock_chain',
            name: 'Multi-Process Deadlock Chain',
            category: 'Concurrency',
            icon: '🔒',
            description: 'Processes competing circular wait on DB_LOCK, LOG_FILE, and NET_PORT shared mutexes.',
            config: { cpuAlgorithm: 'RR', quantum: 2, memoryTotal: 1024, memoryStrategy: 'FIRST_FIT', diskAlgorithm: 'SCAN' }
          },
          {
            id: 'scenario_node_failover',
            name: 'Virtual Server Crash & Failover',
            category: 'Reliability',
            icon: '💥',
            description: 'High-availability failover test when primary compute server enters failed state.',
            config: { cpuAlgorithm: 'RR', quantum: 2, memoryTotal: 1024, memoryStrategy: 'FIRST_FIT', diskAlgorithm: 'SCAN' }
          },
          {
            id: 'scenario_balanced_prod',
            name: 'Balanced Enterprise Production Load',
            category: 'Production',
            icon: '⚖️',
            description: 'Realistic data center blend of web ingress, database write transactions, background ETL, and telemetry.',
            config: { cpuAlgorithm: 'RR', quantum: 2, memoryTotal: 1024, memoryStrategy: 'BEST_FIT', diskAlgorithm: 'SCAN' }
          }
        ];
        return sendJson(res, 200, { ok: true, data: scenarios });
      }

      // ==========================================
      // 7. System Status Aggregation
      // ==========================================
      if (method === 'GET' && pathname === '/api/status') {
        const totalServers = appState.servers.length;
        const activeServers = appState.servers.filter((s) => s.status === 'running').length;
        const failedServers = appState.servers.filter((s) => s.status === 'failed').length;
        const activeIncidents = appState.incidents.filter((i) => i.status === 'active');
        const criticalIncidents = activeIncidents.filter((i) => i.severity === 'critical').length;

        let health = 'healthy';
        if (criticalIncidents > 0 || failedServers > 0) {
          health = 'critical';
        } else if (activeIncidents.length > 0 || activeServers < totalServers) {
          health = 'degraded';
        }

        // Aggregate CPU, RAM, Storage
        let totalCores = 0, totalRam = 0, usedRam = 0, totalStorage = 0, usedStorage = 0;
        appState.servers.forEach((s) => {
          totalCores += s.cpu.cores;
          totalRam += s.ram.total;
          usedRam += s.ram.used;
          totalStorage += s.storage.total;
          usedStorage += s.storage.used;
        });

        return sendJson(res, 200, {
          ok: true,
          data: {
            health,
            servers: {
              total: totalServers,
              active: activeServers,
              offline: totalServers - activeServers,
              failed: failedServers
            },
            resources: {
              totalCores,
              ramTotalMB: totalRam,
              ramUsedMB: usedRam,
              ramUtilPercent: totalRam > 0 ? Math.round((usedRam / totalRam) * 100) : 0,
              storageTotalGB: totalStorage,
              storageUsedGB: usedStorage,
              storageUtilPercent: totalStorage > 0 ? Math.round((usedStorage / totalStorage) * 100) : 0
            },
            incidents: {
              active: activeIncidents.length,
              critical: criticalIncidents,
              total: appState.incidents.length
            },
            platform: {
              engine: 'C++14 Native Simulation Core',
              name: 'DataCenterOS',
              version: '2.0.0',
              uptime: process.uptime()
            }
          }
        });
      }

      // If no route matched
      return sendJson(res, 404, { ok: false, error: `Endpoint not found: ${method} ${pathname}` });
    } catch (err) {
      console.error(`[API ERROR] ${method} ${pathname}: ${err.message}`);
      return sendJson(res, 500, { ok: false, error: err.message });
    }
  }

  // Static Assets Fallback
  serveStatic(pathname, res);
});

// Periodic state persistence every 30 seconds
setInterval(persistState, 30000);

// Initialize & Launch
loadPersistedState();

server.listen(PORT, HOST, () => {
  console.log(`========================================================================`);
  console.log(`DataCenterOS — Virtual Infrastructure Management & Simulation Platform`);
  console.log(`Dashboard Web Interface: http://${HOST}:${PORT}`);
  console.log(`C++ Simulation Engine:    ${ENGINE_BIN}`);
  console.log(`State Persistence:        ${STATE_FILE}`);
  console.log(`========================================================================`);
});
