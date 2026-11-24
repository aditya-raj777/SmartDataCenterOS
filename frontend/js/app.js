// DataCenterOS — Virtual Infrastructure Management & OS Simulation Platform
// Master Frontend Controller: All 12 Enterprise Modules & Native C++ Simulation Engine
'use strict';

(() => {
  // ----------------------------------------------------------------- Application State
  const state = {
    // Simulation & Workload State
    jobs: [],
    samples: [],
    filterState: 'ALL',
    simResult: null,
    tick: 0,
    playing: false,
    timer: null,

    // Standalone Labs State
    cpuResult: null,
    walkIdx: 0,
    memResult: null,
    memStep: 0,
    diskResult: null,
    diskAnimStep: 0,
    diskTimer: null,
    cmpMode: 'cpu',
    cmpLoaded: false,

    // Enterprise Fleet State
    servers: [],
    incidents: [],
    events: [],
    scenarios: [],
    platformStatus: null,

    // Deadlock Module State
    deadlockNodes: [
      { pid: 1, name: 'tx-processor-1', holding: ['DB_LOCK'], waiting: ['LOG_FILE'] },
      { pid: 2, name: 'audit-logger-2', holding: ['LOG_FILE'], waiting: ['DB_LOCK'] }
    ],
    deadlockResult: null,

    // Chart instances cache
    charts: {},
  };

  const PALETTE = [
    '#38bdf8', '#34d399', '#a78bfa', '#fbbf24', '#fb7185',
    '#2dd4bf', '#818cf8', '#f97316', '#4ade80', '#e879f9', '#60a5fa', '#facc15'
  ];
  const colorForPid = (pid) => pid < 0 ? '#1e293b' : PALETTE[(pid - 1) % PALETTE.length];

  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  function toast(msg, isError = false) {
    const el = $('toast');
    if (!el) return;
    el.textContent = msg;
    el.className = 'toast' + (isError ? ' error' : '');
    el.style.display = 'block';
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { el.style.display = 'none'; }, 3800);
  }

  async function api(url, body = null) {
    const opts = body !== null
      ? { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }
      : { method: 'GET' };
    const res = await fetch(url, opts);
    const json = await res.json();
    if (!json.ok) throw new Error(json.error || `HTTP ${res.status}`);
    return json.data;
  }

  // ----------------------------------------------------------------- Tooltip System
  const tip = $('tooltip');
  function attachTip(el, html) {
    if (!el || !tip) return;
    el.addEventListener('mouseenter', (e) => {
      tip.innerHTML = html;
      tip.style.display = 'block';
      moveTip(e);
    });
    el.addEventListener('mousemove', moveTip);
    el.addEventListener('mouseleave', () => { tip.style.display = 'none'; });
  }
  function moveTip(e) {
    if (!tip) return;
    tip.style.left = Math.min(window.innerWidth - 280, e.clientX + 14) + 'px';
    tip.style.top = Math.min(window.innerHeight - 130, e.clientY + 14) + 'px';
  }

  function downloadFile(name, content, type) {
    const blob = new Blob([content], { type });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  // ----------------------------------------------------------------- Navigation
  document.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.nav-item').forEach((b) => b.classList.remove('active'));
      document.querySelectorAll('.view').forEach((v) => v.classList.remove('active'));
      btn.classList.add('active');
      const v = btn.dataset.view;
      const targetView = $('view-' + v);
      if (targetView) targetView.classList.add('active');

      if (v === 'servers') loadServers();
      if (v === 'monitoring') loadIncidents();
      if (v === 'logs') loadLogs();
      if (v === 'scenarios') loadScenarios();
      if (v === 'deadlock') renderDeadlockUI();
      if (v === 'compare' && !state.cmpLoaded) runComparison();
      if (v === 'cpu' && !state.cpuResult && state.jobs.length) runCpuLab();
      if (v === 'memory' && !state.memResult && state.jobs.length) runMemoryLab();
      if (v === 'disk' && !state.diskResult) runDiskLab();
    });
  });

  // ----------------------------------------------------------------- Gantt Chart Renderer
  function renderGantt(container, slices, currentTick = null) {
    if (!container) return;
    container.innerHTML = '';
    if (!slices || !slices.length) {
      container.innerHTML = '<div style="color:var(--text-muted);padding:12px;">No CPU execution intervals recorded yet.</div>';
      return;
    }
    const maxEnd = slices[slices.length - 1].end || 1;
    const track = document.createElement('div');
    track.className = 'gantt-track';
    const scale = document.createElement('div');
    scale.className = 'gantt-scale';

    slices.forEach((s) => {
      const dur = Math.max(1, s.end - s.start);
      const pct = (dur / maxEnd) * 100;
      const blk = document.createElement('div');
      blk.className = 'gantt-block' + (s.pid < 0 ? ' idle' : '');
      if (currentTick !== null && currentTick >= s.start && currentTick < s.end) blk.classList.add('current-tick');
      blk.style.flex = `${dur} 0 ${Math.max(22, pct * 6)}px`;
      if (s.pid >= 0) blk.style.background = colorForPid(s.pid);
      blk.innerHTML = `<span>${esc(s.name)}</span><span style="font-size:9px;opacity:.75;">${s.start}–${s.end}</span>`;
      attachTip(blk, `<b>${esc(s.name)}</b> ${s.pid >= 0 ? `(PID ${s.pid})` : ''}<br>Start: <b>${s.start}</b> · End: <b>${s.end}</b><br>Duration: <b>${dur}</b> tick(s)`);
      track.appendChild(blk);

      const tk = document.createElement('div');
      tk.className = 'gantt-tick';
      tk.style.flex = `${dur} 0 ${Math.max(22, pct * 6)}px`;
      tk.textContent = s.start;
      scale.appendChild(tk);
    });
    const endTk = document.createElement('div');
    endTk.textContent = maxEnd;
    scale.appendChild(endTk);
    container.appendChild(track);
    container.appendChild(scale);
  }

  // ----------------------------------------------------------------- Memory Map Renderer
  function renderMemBar(container, mem) {
    if (!container) return;
    container.innerHTML = '';
    if (!mem || !mem.blocks || !mem.blocks.length) {
      container.innerHTML = '<div style="color:var(--text-muted);padding:12px;">No memory allocation data.</div>';
      return;
    }
    const total = mem.total || 1;
    mem.blocks.forEach((b) => {
      const seg = document.createElement('div');
      const isFree = b.pid < 0;
      seg.className = 'mem-seg ' + (isFree ? 'free' : 'alloc');
      seg.style.width = ((b.size / total) * 100) + '%';
      if (!isFree) seg.style.background = colorForPid(b.pid);
      seg.innerHTML = `<span>${isFree ? 'FREE' : 'P' + b.pid}</span><span style="font-size:9px;opacity:.8;">${b.size}K</span>`;
      attachTip(seg, `<b>${isFree ? 'Free Memory Hole' : 'PID ' + b.pid}</b><br>Physical Range: ${b.start} – ${b.start + b.size} KB<br>Block Size: ${b.size} KB<br>Requested: ${b.used} KB`);
      container.appendChild(seg);
    });
  }

  // ===========================================================================
  // MODULE 1: INFRASTRUCTURE OVERVIEW & INTEGRATED ENGINE
  // ===========================================================================
  function getSimConfig() {
    return {
      cpuAlgorithm: $('cfg-cpu').value,
      quantum: parseInt($('cfg-quantum').value, 10) || 2,
      memoryTotal: parseInt($('cfg-mem-total').value, 10) || 1024,
      memoryStrategy: $('cfg-mem-strategy').value,
      diskAlgorithm: $('cfg-disk-algo').value,
      diskCylinders: parseInt($('disk-cyls')?.value || '200', 10) || 200,
      diskHead: parseInt($('disk-head')?.value || '53', 10) || 53,
      diskDirection: $('disk-dir')?.value || 'UP',
      includeTimeline: true,
    };
  }

  async function runIntegratedSim(autoPlay = true) {
    if (!state.jobs.length) {
      toast('Add at least one process or load a workload preset first.', true);
      return;
    }
    stopPlayback();
    try {
      const res = await api('/api/simulate', { config: getSimConfig(), processes: state.jobs });
      state.simResult = res;
      state.tick = 0;
      const maxTick = Math.max(0, (res.timeline?.length || 1) - 1);
      $('sim-scrubber').max = maxTick;
      $('sim-scrubber').value = 0;
      renderOverviewChart(res);
      renderTick(0);
      if (autoPlay && maxTick > 0) startPlayback();
      else toast(`Simulation finished (${res.summary.makespan} ticks, ${res.summary.completed}/${res.summary.processCount} completed).`);
    } catch (e) {
      toast(e.message, true);
    }
  }

  function startPlayback() {
    if (!state.simResult || !state.simResult.timeline.length) return;
    if (state.tick >= state.simResult.timeline.length - 1) state.tick = 0;
    state.playing = true;
    $('btn-sim-pause').disabled = false;
    $('btn-sim-pause').textContent = '⏸ Pause';
    const stepMs = parseInt($('sim-speed').value, 10) || 450;
    clearInterval(state.timer);
    state.timer = setInterval(() => {
      if (state.tick < state.simResult.timeline.length - 1) {
        state.tick++;
        renderTick(state.tick);
      } else {
        stopPlayback();
        toast('Simulation playback completed.');
      }
    }, stepMs);
  }

  function stopPlayback() {
    state.playing = false;
    clearInterval(state.timer);
    state.timer = null;
    $('btn-sim-pause').textContent = '▶ Resume';
  }

  function renderTick(idx) {
    const res = state.simResult;
    if (!res || !res.timeline || !res.timeline.length) return;
    idx = Math.max(0, Math.min(res.timeline.length - 1, idx));
    state.tick = idx;
    const snap = res.timeline[idx];
    const maxT = res.timeline[res.timeline.length - 1].time;

    $('sim-clock').textContent = `t = ${snap.time} / ${maxT}`;
    $('sim-scrubber').value = idx;

    // KPI Values
    $('kpi-running').textContent = snap.counts.running;
    $('kpi-running-name').textContent = snap.runningPid >= 0 ? `${snap.runningName} (PID ${snap.runningPid})` : 'CPU Idle';
    $('kpi-ready').textContent = snap.counts.ready;
    $('kpi-waiting').textContent = snap.counts.waiting;
    $('kpi-completed').textContent = snap.counts.terminated;
    const failedSoFar = snap.processes.filter((p) => p.failed).length;
    $('kpi-failed-sub').textContent = `${failedSoFar} failed`;
    $('kpi-cpu-util').textContent = `${snap.cpuUtilization}%`;
    $('kpi-ctx-sub').textContent = `${snap.contextSwitches} context switches`;
    $('kpi-mem-util').textContent = `${snap.memory.utilization}%`;
    $('kpi-mem-sub').textContent = `${snap.memory.reserved} / ${snap.memory.total} KB`;
    $('kpi-disk-move').textContent = snap.disk.totalMovement;
    $('kpi-disk-sub').textContent = `Head @ ${snap.disk.head} (${snap.disk.direction})`;
    $('kpi-conflicts').textContent = snap.resourceConflicts;

    // Gantt up to current tick
    $('overview-gantt-meta').textContent = `Policy: ${res.config.cpuAlgorithm} · Avg Wait: ${res.summary.avgWaiting} · Avg TAT: ${res.summary.avgTurnaround} · Throughput: ${res.summary.throughput}`;
    renderGantt($('overview-gantt'), res.gantt, snap.time);

    // Memory bar
    $('overview-mem-meta').textContent = `${snap.memory.reserved}/${snap.memory.total} KB (${snap.memory.utilization}%) · Ext Frag: ${snap.memory.externalFragmentation} KB · Failures: ${snap.memory.failures}`;
    renderMemBar($('overview-mem-bar'), snap.memory);
    $('overview-mem-legend').textContent = `Free blocks: ${snap.memory.freeBlocks} · Largest free hole: ${snap.memory.largestFree} KB · Strategy: ${snap.memory.strategy}`;

    // Shared resources
    const resBox = $('overview-resources');
    if (resBox) {
      resBox.innerHTML = '';
      snap.resources.forEach((r) => {
        const card = document.createElement('div');
        card.className = 'res-card' + (r.locked ? ' locked' : '');
        card.innerHTML = `
          <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:5px;">
            <b>${esc(r.name)}</b>
            <span class="badge ${r.locked ? 'badge-WAITING' : 'badge-RUNNING'}">${r.kind.toUpperCase()} (${r.holders.length}/${r.capacity})</span>
          </div>
          <div style="font-size:11px;color:var(--text-secondary);font-family:var(--font-mono);">
            Holders: ${r.holders.length ? r.holders.map((p) => 'P' + p).join(', ') : 'None'}<br>
            Waiters: ${r.waiters.length ? r.waiters.map((p) => 'P' + p).join(', ') : 'None'}<br>
            Acquisitions: ${r.acquisitions} · Conflicts: ${r.conflicts}
          </div>`;
        resBox.appendChild(card);
      });
    }

    // Live Event Log
    const evs = res.events.filter((e) => e.time <= snap.time);
    $('overview-event-count').textContent = `${evs.length} / ${res.events.length} events`;
    const logEl = $('overview-events');
    if (logEl) {
      logEl.innerHTML = evs.slice(-60).reverse().map((e) => `
        <div class="event-item">
          <span class="event-t">t=${e.time}</span>
          <span class="event-type ev-${esc(e.type)}">${esc(e.type)}</span>
          <span>${esc(e.message)}</span>
        </div>`).join('');
    }

    renderProcessTable();
  }

  function renderOverviewChart(res) {
    const canvas = $('chart-overview-telemetry');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (state.charts.overview) state.charts.overview.destroy();
    const h = res.metricsHistory || [];
    $('overview-perf-summary').textContent = `Peak RAM: ${res.summary.peakMemoryUtilization}% · Disk Utilization: ${res.summary.diskUtilization}%`;
    state.charts.overview = new Chart(ctx, {
      type: 'line',
      data: {
        labels: h.map((x) => 't=' + x.time),
        datasets: [
          { label: 'CPU Utilization %', data: h.map((x) => x.cpuUtilization), borderColor: '#38bdf8', backgroundColor: 'rgba(56,189,248,0.12)', fill: true, tension: 0.2, pointRadius: 1 },
          { label: 'Memory Utilization %', data: h.map((x) => x.memoryUtilization), borderColor: '#34d399', backgroundColor: 'rgba(52,211,153,0.10)', fill: true, tension: 0.2, pointRadius: 1 },
        ]
      },
      options: {
        responsive: true, maintainAspectRatio: false,
        plugins: { legend: { labels: { color: '#cbd5e1', font: { size: 11 } } } },
        scales: {
          x: { ticks: { color: '#64748b', maxTicksLimit: 12 }, grid: { color: 'rgba(148,163,184,0.08)' } },
          y: { min: 0, max: 100, ticks: { color: '#64748b' }, grid: { color: 'rgba(148,163,184,0.08)' } }
        }
      }
    });
  }

  // ===========================================================================
  // MODULE 2: VIRTUAL SERVER FLEET MANAGEMENT
  // ===========================================================================
  async function loadServers() {
    try {
      const data = await api('/api/servers');
      state.servers = data;
      renderServerGrid();
      updateServerSelects();
      updateFleetStatusKPIs();
    } catch (e) {
      toast('Failed to load servers: ' + e.message, true);
    }
  }

  function updateFleetStatusKPIs() {
    const total = state.servers.length;
    const active = state.servers.filter((s) => s.status === 'running').length;
    const offline = total - active;
    if ($('kpi-srv-total')) $('kpi-srv-total').textContent = total;
    if ($('kpi-srv-sub')) $('kpi-srv-sub').textContent = `${active} Active · ${offline} Offline`;
    if ($('nav-count-servers')) $('nav-count-servers').textContent = total;
  }

  function renderServerGrid() {
    const grid = $('server-grid');
    if (!grid) return;
    if (!state.servers.length) {
      grid.innerHTML = '<div style="color:var(--text-muted);padding:24px;text-align:center;">No virtual servers provisioned yet.</div>';
      return;
    }

    grid.innerHTML = state.servers.map((s) => {
      const isRunning = s.status === 'running';
      const isFailed = s.status === 'failed';
      const ramPct = s.ram.total > 0 ? Math.round((s.ram.used / s.ram.total) * 100) : 0;
      const storagePct = s.storage.total > 0 ? Math.round((s.storage.used / s.storage.total) * 100) : 0;
      const statusBadge = isFailed ? 'badge-FAILED' : (isRunning ? 'badge-RUNNING' : 'badge-WAITING');

      return `
        <div class="server-card">
          <div class="server-header">
            <div>
              <div class="server-title">${esc(s.name)}</div>
              <div class="server-sub">${esc(s.type)} · ${esc(s.ipAddress)} (${esc(s.region)})</div>
            </div>
            <span class="badge ${statusBadge}">${esc(s.status.toUpperCase())}</span>
          </div>

          <div class="server-metric-row">
            <div class="metric-mini"><span>CPU Utilization (${s.cpu.cores} Cores)</span><b>${s.cpu.utilization}%</b></div>
            <div class="progress-bar-bg"><div class="progress-bar-fill" style="width:${s.cpu.utilization}%;background:var(--cyan);"></div></div>

            <div class="metric-mini" style="margin-top:6px;"><span>RAM (${s.ram.used}/${s.ram.total} MB)</span><b>${ramPct}%</b></div>
            <div class="progress-bar-bg"><div class="progress-bar-fill" style="width:${ramPct}%;background:var(--emerald);"></div></div>

            <div class="metric-mini" style="margin-top:6px;"><span>Storage (${s.storage.used}/${s.storage.total} GB)</span><b>${storagePct}%</b></div>
            <div class="progress-bar-bg"><div class="progress-bar-fill" style="width:${storagePct}%;background:var(--amber);"></div></div>
          </div>

          <div class="server-actions-row">
            ${isRunning
              ? `<button class="btn btn-sm" data-srv-action="stop" data-id="${s.id}">⏹ Stop</button>
                 <button class="btn btn-sm" data-srv-action="restart" data-id="${s.id}">↻ Restart</button>
                 <button class="btn btn-sm btn-danger" data-srv-action="fail" data-id="${s.id}">💥 Crash Node</button>`
              : (isFailed
                  ? `<button class="btn btn-sm btn-success" data-srv-action="recover" data-id="${s.id}">✓ Recover &amp; Start</button>`
                  : `<button class="btn btn-sm btn-success" data-srv-action="start" data-id="${s.id}">▶ Start</button>`)
            }
            <button class="btn btn-sm btn-danger" data-srv-action="delete" data-id="${s.id}">🗑 Delete</button>
          </div>
        </div>
      `;
    }).join('');

    // Attach server action listeners
    grid.querySelectorAll('[data-srv-action]').forEach((b) => {
      b.addEventListener('click', async () => {
        const id = b.dataset.id;
        const action = b.dataset.srvAction;
        try {
          if (action === 'delete') {
            await api(`/api/servers/${id}`, { _method: 'DELETE' }); // Note: DELETE
            toast(`Decommissioned server ${id}`);
          } else {
            await api(`/api/servers/${id}/${action}`, {});
            toast(`Server action '${action}' applied successfully.`);
          }
          await loadServers();
          await fetchPlatformStatus();
        } catch (e) {
          toast(e.message, true);
        }
      });
    });
  }

  function updateServerSelects() {
    const jobSel = $('job-server-select');
    const incSel = $('inc-server-select');
    if (jobSel) {
      jobSel.innerHTML = '<option value="">Auto-Assign</option>' +
        state.servers.map((s) => `<option value="${s.id}">${esc(s.name)}</option>`).join('');
    }
    if (incSel) {
      incSel.innerHTML = state.servers.map((s) => `<option value="${s.id}">${esc(s.name)}</option>`).join('');
    }
  }

  // Server Form Toggle & Submission
  $('btn-toggle-server-form')?.addEventListener('click', () => {
    const p = $('server-form-panel');
    p.style.display = p.style.display === 'none' ? 'block' : 'none';
  });
  $('btn-cancel-server-form')?.addEventListener('click', () => {
    $('server-form-panel').style.display = 'none';
  });
  $('btn-refresh-servers')?.addEventListener('click', loadServers);

  $('server-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      await api('/api/servers', {
        name: $('srv-name').value.trim(),
        type: $('srv-type').value,
        cores: parseInt($('srv-cores').value, 10),
        ram: parseInt($('srv-ram').value, 10),
        storage: parseInt($('srv-storage').value, 10),
        region: $('srv-region').value
      });
      toast('Provisioned new virtual server instance.');
      $('server-form').reset();
      $('server-form-panel').style.display = 'none';
      await loadServers();
      await fetchPlatformStatus();
    } catch (err) {
      toast(err.message, true);
    }
  });

  // ===========================================================================
  // MODULE 3: PROCESS CONTROL CENTER
  // ===========================================================================
  function renderProcessTable() {
    const tbody = $('process-tbody');
    if (!tbody) return;
    if ($('nav-count-processes')) $('nav-count-processes').textContent = state.jobs.length;
    const snap = state.simResult?.timeline?.[state.tick];
    const snapMap = {};
    if (snap) snap.processes.forEach((p) => { snapMap[p.pid] = p; });
    const finalMap = {};
    if (state.simResult) state.simResult.processes.forEach((p) => { finalMap[p.pid] = p; });

    const rows = state.jobs.filter((j) => {
      if (state.filterState === 'ALL') return true;
      const st = snapMap[j.pid]?.state || 'NEW';
      return st === state.filterState;
    });

    tbody.innerHTML = rows.map((j) => {
      const live = snapMap[j.pid];
      const fin = finalMap[j.pid];
      const st = live ? (live.failed ? 'FAILED' : live.state) : 'NEW';
      const sub = live?.waitReason ? ` (${live.waitReason})` : '';
      const rem = live ? live.remainingCpu : j.cpuBurst;
      const srvName = j.serverId ? (state.servers.find((s) => s.id === j.serverId)?.name || 'Assigned') : 'Cluster';

      return `<tr>
        <td><b>${j.pid}</b></td>
        <td class="sans" style="font-weight:600;color:${colorForPid(j.pid)}">${esc(j.name)}</td>
        <td style="font-size:11px;color:var(--text-secondary);">${esc(srvName)}</td>
        <td>${j.arrival}</td>
        <td>${j.cpuBurst}</td>
        <td>${j.priority}</td>
        <td>${j.memory} KB</td>
        <td style="font-size:10.5px;color:var(--text-secondary);">${esc(j.burstSpec || '—')}</td>
        <td><span class="badge badge-${st}">${st}${esc(sub)}</span></td>
        <td>${rem}</td>
        <td>${fin ? fin.waiting : '—'}</td>
        <td>${fin && fin.turnaround >= 0 ? fin.turnaround : '—'}</td>
        <td>${fin && fin.response >= 0 ? fin.response : '—'}</td>
        <td>
          <button class="btn btn-sm" data-edit="${j.pid}">Edit</button>
          <button class="btn btn-sm btn-danger" data-del="${j.pid}">Del</button>
        </td>
      </tr>`;
    }).join('') || '<tr><td colspan="14" style="text-align:center;color:var(--text-muted);padding:18px;">No processes match current state filter.</td></tr>';

    tbody.querySelectorAll('[data-edit]').forEach((b) => b.addEventListener('click', () => editJob(parseInt(b.dataset.edit, 10))));
    tbody.querySelectorAll('[data-del]').forEach((b) => b.addEventListener('click', () => {
      const pid = parseInt(b.dataset.del, 10);
      state.jobs = state.jobs.filter((x) => x.pid !== pid);
      state.simResult = null;
      renderProcessTable();
      toast(`Deleted PID ${pid}`);
    }));
  }

  function editJob(pid) {
    const j = state.jobs.find((x) => x.pid === pid);
    if (!j) return;
    $('job-edit-pid').value = j.pid;
    $('job-name').value = j.name;
    $('job-arrival').value = j.arrival;
    $('job-cpu').value = j.cpuBurst;
    $('job-priority').value = j.priority;
    $('job-memory').value = j.memory;
    if ($('job-server-select')) $('job-server-select').value = j.serverId || '';
    $('job-io').value = (j.ioRequests || []).join(', ');
    $('job-res').value = (j.resources || []).join(', ');
    $('job-spec').value = j.burstSpec || '';
    $('job-form-title').textContent = `Edit Process PID ${j.pid}`;
    $('job-submit-btn').textContent = '✓ Save Changes';
    $('job-cancel-edit').style.display = 'inline-flex';
  }

  function resetJobForm() {
    $('job-edit-pid').value = '';
    $('job-form').reset();
    $('job-arrival').value = 0; $('job-cpu').value = 5; $('job-priority').value = 2; $('job-memory').value = 128;
    $('job-form-title').textContent = 'Create Process Task';
    $('job-submit-btn').textContent = '+ Add Process';
    $('job-cancel-edit').style.display = 'none';
  }

  $('job-cancel-edit')?.addEventListener('click', resetJobForm);
  $('job-form')?.addEventListener('submit', (e) => {
    e.preventDefault();
    const editPid = parseInt($('job-edit-pid').value, 10);
    const nextPid = state.jobs.reduce((m, x) => Math.max(m, x.pid), 0) + 1;
    const ioList = $('job-io').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => !isNaN(n));
    const resList = $('job-res').value.split(/[\s,;]+/).map((x) => x.trim()).filter(Boolean);
    const spec = $('job-spec').value.trim();

    const job = {
      pid: editPid || nextPid,
      name: $('job-name').value.trim() || `worker-${editPid || nextPid}`,
      serverId: $('job-server-select')?.value || null,
      arrival: parseInt($('job-arrival').value, 10) || 0,
      cpuBurst: parseInt($('job-cpu').value, 10) || 1,
      priority: parseInt($('job-priority').value, 10) || 1,
      memory: parseInt($('job-memory').value, 10) || 0,
      ioRequests: ioList,
      resources: resList,
      burstSpec: spec,
    };
    if (editPid) {
      const idx = state.jobs.findIndex((x) => x.pid === editPid);
      if (idx >= 0) state.jobs[idx] = job;
      toast(`Updated Process PID ${editPid}`);
    } else {
      state.jobs.push(job);
      toast(`Added ${job.name} (PID ${job.pid})`);
    }
    state.simResult = null;
    resetJobForm();
    renderProcessTable();
  });

  $('state-filters')?.querySelectorAll('.pill').forEach((p) => {
    p.addEventListener('click', () => {
      $('state-filters').querySelectorAll('.pill').forEach((x) => x.classList.remove('active'));
      p.classList.add('active');
      state.filterState = p.dataset.state;
      renderProcessTable();
    });
  });

  $('btn-clear-jobs')?.addEventListener('click', () => {
    state.jobs = [];
    state.simResult = null;
    renderProcessTable();
    toast('Cleared all jobs.');
  });

  $('btn-batch-add')?.addEventListener('click', async () => {
    try {
      const gen = await api('/api/generate', { profile: 'mixed', count: 5, seed: Math.floor(Math.random() * 10000) });
      let nextPid = state.jobs.reduce((m, x) => Math.max(m, x.pid), 0) + 1;
      gen.forEach((g) => { g.pid = nextPid++; state.jobs.push(g); });
      state.simResult = null;
      renderProcessTable();
      toast('Appended 5 generated workload jobs.');
    } catch (e) { toast(e.message, true); }
  });

  $('btn-csv-export')?.addEventListener('click', () => {
    const header = 'pid,name,arrival,cpuBurst,priority,memory,burstSpec';
    const lines = state.jobs.map((j) => `${j.pid},"${j.name}",${j.arrival},${j.cpuBurst},${j.priority},${j.memory},"${j.burstSpec || ''}"`);
    downloadFile('workload.csv', [header, ...lines].join('\n'), 'text/csv');
  });

  $('csv-import-input')?.addEventListener('change', (e) => {
    const file = e.target.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const lines = reader.result.split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
      const parsed = [];
      for (let i = 1; i < lines.length; i++) {
        const cols = lines[i].split(',').map((s) => s.replace(/^"|"$/g, '').trim());
        if (cols.length < 6) continue;
        parsed.push({
          pid: parseInt(cols[0], 10) || i,
          name: cols[1] || `P${i}`,
          arrival: parseInt(cols[2], 10) || 0,
          cpuBurst: Math.max(1, parseInt(cols[3], 10) || 1),
          priority: parseInt(cols[4], 10) || 1,
          memory: parseInt(cols[5], 10) || 64,
          burstSpec: cols[6] || '',
        });
      }
      if (parsed.length) {
        state.jobs = parsed;
        state.simResult = null;
        renderProcessTable();
        toast(`Imported ${parsed.length} processes from CSV.`);
      } else toast('No valid rows found in CSV.', true);
    };
    reader.readAsText(file);
    e.target.value = '';
  });

  // ===========================================================================
  // MODULE 4: CPU SCHEDULING LAB
  // ===========================================================================
  async function runCpuLab() {
    if (!state.jobs.length) return toast('Add processes first.', true);
    try {
      const res = await api('/api/cpu', {
        processes: state.jobs,
        algorithm: $('cpu-algo').value,
        quantum: parseInt($('cpu-quantum').value, 10) || 2,
        priorityOrder: $('cpu-prio-order').value,
        includeSteps: true,
      });
      state.cpuResult = res;
      state.walkIdx = 0;
      const s = res.summary;
      $('cpu-kpis').innerHTML = `
        <div class="kpi-card cyan"><div class="kpi-label">Avg Waiting</div><div class="kpi-value">${s.avgWaiting}</div></div>
        <div class="kpi-card emerald"><div class="kpi-label">Avg Turnaround</div><div class="kpi-value">${s.avgTurnaround}</div></div>
        <div class="kpi-card amber"><div class="kpi-label">Avg Response</div><div class="kpi-value">${s.avgResponse}</div></div>
        <div class="kpi-card violet"><div class="kpi-label">CPU Utilization</div><div class="kpi-value">${s.cpuUtilization}%</div><div class="kpi-sub">Busy ${s.busy} / Idle ${s.idle}</div></div>
        <div class="kpi-card"><div class="kpi-label">Throughput</div><div class="kpi-value">${s.throughput}</div><div class="kpi-sub">jobs / tick</div></div>
        <div class="kpi-card rose"><div class="kpi-label">Context Switches</div><div class="kpi-value">${s.contextSwitches}</div></div>`;
      renderGantt($('cpu-gantt'), res.gantt);
      $('cpu-tbody').innerHTML = res.processes.map((p) => `<tr>
        <td>${p.pid}</td><td class="sans">${esc(p.name)}</td><td>${p.arrival}</td><td>${p.burst}</td><td>${p.priority}</td>
        <td>${p.start}</td><td>${p.completion}</td><td>${p.waiting}</td><td>${p.turnaround}</td><td>${p.response}</td>
      </tr>`).join('');
      renderWalkthrough();
    } catch (e) { toast(e.message, true); }
  }

  function renderWalkthrough() {
    const res = state.cpuResult;
    if (!res || !res.steps || !res.steps.length) return;
    const st = res.steps[state.walkIdx];
    $('walk-counter').textContent = `Step ${state.walkIdx + 1} / ${res.steps.length} (t=${st.time})`;
    $('cpu-walkthrough-body').innerHTML = `
      <div style="margin-bottom:10px;display:flex;gap:14px;flex-wrap:wrap;font-size:12px;">
        <span>Running: <b style="color:var(--emerald)">${st.runningPid >= 0 ? esc(st.runningName) + ' (rem=' + st.runningRemaining + ')' : 'IDLE'}</b></span>
        ${res.quantum ? `<span>Quantum: <b style="color:var(--amber)">${res.quantum}</b></span>` : ''}
        <span>Context Switches: <b>${st.contextSwitches}</b></span>
      </div>
      <div style="font-size:10.5px;color:var(--text-secondary);text-transform:uppercase;margin-bottom:4px;font-weight:700;">Ready Queue (FIFO order)</div>
      <div class="queue-chips">${st.readyQueue.length ? st.readyQueue.map((q) => `<span class="q-chip">${esc(q.name)} (rem:${q.remaining}, p:${q.priority})</span>`).join('') : '<span style="color:var(--text-muted);font-size:11.5px;">Empty</span>'}</div>
      <div style="font-size:10.5px;color:var(--text-secondary);text-transform:uppercase;margin:12px 0 4px;font-weight:700;">Decision Events at t=${st.time}</div>
      <ul style="padding-left:18px;font-size:12px;color:var(--text-primary);">${st.events.map((e) => `<li>${esc(e)}</li>`).join('')}</ul>`;
  }

  $('btn-cpu-run')?.addEventListener('click', runCpuLab);
  $('btn-walk-prev')?.addEventListener('click', () => { if (state.walkIdx > 0) { state.walkIdx--; renderWalkthrough(); } });
  $('btn-walk-next')?.addEventListener('click', () => { if (state.cpuResult && state.walkIdx < state.cpuResult.steps.length - 1) { state.walkIdx++; renderWalkthrough(); } });

  // ===========================================================================
  // MODULE 5: MEMORY MANAGEMENT LAB
  // ===========================================================================
  $('mem-mode')?.addEventListener('change', () => {
    $('mem-parts-field').style.display = $('mem-mode').value === 'FIXED' ? 'flex' : 'none';
  });

  async function runMemoryLab() {
    if (!state.jobs.length) return toast('Add processes first.', true);
    try {
      const memJobs = state.jobs.map((j) => ({ pid: j.pid, name: j.name, size: Math.max(1, j.memory || 64), arrival: j.arrival, duration: j.cpuBurst }));
      const parts = $('mem-partitions').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => n > 0);
      const res = await api('/api/memory', {
        total: parseInt($('mem-total').value, 10) || 512,
        mode: $('mem-mode').value,
        partitions: parts,
        strategy: $('mem-strategy').value,
        jobs: memJobs,
      });
      state.memResult = res;
      state.memStep = Math.min(1, res.steps.length - 1);
      const s = res.summary;
      $('mem-kpis').innerHTML = `
        <div class="kpi-card cyan"><div class="kpi-label">Total Memory</div><div class="kpi-value">${s.totalMemory} KB</div></div>
        <div class="kpi-card emerald"><div class="kpi-label">Peak Utilization</div><div class="kpi-value">${s.peakUtilization}%</div><div class="kpi-sub">Avg: ${s.avgUtilization}%</div></div>
        <div class="kpi-card"><div class="kpi-label">Allocated / Jobs</div><div class="kpi-value">${s.allocated}/${s.jobs}</div><div class="kpi-sub">${s.rejected} rejected</div></div>
        <div class="kpi-card rose"><div class="kpi-label">Alloc Failures</div><div class="kpi-value">${s.allocationFailures}</div><div class="kpi-sub">${s.externalFragmentationFailures} ext-frag fails</div></div>
        <div class="kpi-card amber"><div class="kpi-label">Peak Ext Frag</div><div class="kpi-value">${s.peakExternalFragmentation} KB</div><div class="kpi-sub">Int Frag: ${s.internalFragmentation} KB</div></div>`;
      renderMemStep();
    } catch (e) { toast(e.message, true); }
  }

  function renderMemStep() {
    const res = state.memResult;
    if (!res || !res.steps.length) return;
    const st = res.steps[state.memStep];
    $('mem-step-badge').textContent = `Step ${state.memStep + 1} / ${res.steps.length} (t=${st.time})`;
    $('mem-step-msg').textContent = `[${st.action}] ${st.message}`;
    $('mem-step-msg').style.color = st.ok ? 'var(--cyan)' : 'var(--rose)';
    renderMemBar($('mem-lab-bar'), st.memory);
    $('mem-blocks-table').innerHTML = `<table><thead><tr><th>Start Addr</th><th>End Addr</th><th>Block Size</th><th>Status</th><th>Process</th><th>Requested</th></tr></thead><tbody>${
      st.memory.blocks.map((b) => `<tr>
        <td>${b.start}</td><td>${b.start + b.size}</td><td>${b.size} KB</td>
        <td><span class="badge ${b.pid < 0 ? 'badge-NEW' : 'badge-RUNNING'}">${b.pid < 0 ? 'FREE' : 'ALLOCATED'}</span></td>
        <td>${b.pid < 0 ? '—' : 'PID ' + b.pid}</td><td>${b.used} KB</td>
      </tr>`).join('')
    }</tbody></table>`;
  }

  $('btn-mem-run')?.addEventListener('click', runMemoryLab);
  $('btn-mem-prev')?.addEventListener('click', () => { if (state.memStep > 0) { state.memStep--; renderMemStep(); } });
  $('btn-mem-next')?.addEventListener('click', () => { if (state.memResult && state.memStep < state.memResult.steps.length - 1) { state.memStep++; renderMemStep(); } });

  // ===========================================================================
  // MODULE 6: STORAGE & DISK I/O LAB
  // ===========================================================================
  $('btn-disk-from-jobs')?.addEventListener('click', () => {
    const reqs = [];
    state.jobs.forEach((j) => (j.ioRequests || []).forEach((c) => reqs.push(c)));
    if (!reqs.length) return toast('Current workload contains no cylinder I/O requests.', true);
    $('disk-reqs').value = reqs.join(', ');
    runDiskLab();
  });

  async function runDiskLab() {
    try {
      const reqs = $('disk-reqs').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => !isNaN(n));
      const res = await api('/api/disk', {
        cylinders: parseInt($('disk-cyls').value, 10) || 200,
        head: parseInt($('disk-head').value, 10) || 53,
        direction: $('disk-dir').value,
        algorithm: $('disk-algo').value,
        requests: reqs,
      });
      state.diskResult = res;
      $('disk-kpis').innerHTML = `
        <div class="kpi-card cyan"><div class="kpi-label">Algorithm</div><div class="kpi-value" style="font-size:18px;">${esc(res.algorithm)}</div><div class="kpi-sub">${esc(res.algorithmName)}</div></div>
        <div class="kpi-card amber"><div class="kpi-label">Total Movement</div><div class="kpi-value">${res.totalMovement}</div><div class="kpi-sub">cylinders</div></div>
        <div class="kpi-card emerald"><div class="kpi-label">Average Seek</div><div class="kpi-value">${res.averageSeek}</div><div class="kpi-sub">per I/O op</div></div>
        <div class="kpi-card"><div class="kpi-label">Requests Served</div><div class="kpi-value">${res.requestCount}</div><div class="kpi-sub">Final head @ ${res.finalHead}</div></div>`;
      $('disk-order-strip').textContent = `Seek Sequence: ${res.initialHead} → ${res.path.slice(1).join(' → ')}`;
      playDiskAnim();
    } catch (e) { toast(e.message, true); }
  }

  function playDiskAnim() {
    clearInterval(state.diskTimer);
    if (!state.diskResult) return;
    state.diskAnimStep = 0;
    drawDiskCanvas(0);
    state.diskTimer = setInterval(() => {
      if (state.diskAnimStep < state.diskResult.path.length - 1) {
        state.diskAnimStep++;
        drawDiskCanvas(state.diskAnimStep);
      } else clearInterval(state.diskTimer);
    }, 360);
  }

  function drawDiskCanvas(step) {
    const res = state.diskResult;
    if (!res) return;
    const canvas = $('disk-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);
    const padL = 44, padR = 30, padT = 34, padB = 24;
    const maxC = Math.max(1, res.cylinders - 1);
    const xOf = (c) => padL + (c / maxC) * (W - padL - padR);
    const pts = res.path;
    const yOf = (i) => padT + (i / Math.max(1, pts.length - 1)) * (H - padT - padB);

    // Cylinder axis ticks
    ctx.strokeStyle = 'rgba(148,163,184,0.22)';
    ctx.fillStyle = '#94a3b8';
    ctx.font = '11px monospace';
    ctx.beginPath(); ctx.moveTo(padL, padT - 10); ctx.lineTo(W - padR, padT - 10); ctx.stroke();
    for (let k = 0; k <= 5; k++) {
      const c = Math.round((k / 5) * maxC);
      const x = xOf(c);
      ctx.beginPath(); ctx.moveTo(x, padT - 14); ctx.lineTo(x, padT - 6); ctx.stroke();
      ctx.fillText(String(c), x - 10, padT - 18);
    }

    // Path lines up to `step`
    ctx.lineWidth = 2.4;
    for (let i = 0; i < step && i < pts.length - 1; i++) {
      ctx.strokeStyle = '#38bdf8';
      ctx.beginPath();
      ctx.moveTo(xOf(pts[i]), yOf(i));
      ctx.lineTo(xOf(pts[i + 1]), yOf(i + 1));
      ctx.stroke();
    }

    // Nodes
    for (let i = 0; i <= step && i < pts.length; i++) {
      const x = xOf(pts[i]), y = yOf(i);
      const isCur = i === step;
      ctx.fillStyle = i === 0 ? '#34d399' : (isCur ? '#fbbf24' : '#818cf8');
      ctx.beginPath(); ctx.arc(x, y, isCur ? 7 : 4.5, 0, Math.PI * 2); ctx.fill();
      ctx.fillStyle = '#e2e8f0';
      ctx.fillText(String(pts[i]), x + 8, y + 4);
    }
    $('disk-anim-label').textContent = `Step ${step}/${pts.length - 1} · Head @ ${pts[step]}`;
  }

  $('btn-disk-run')?.addEventListener('click', runDiskLab);
  $('btn-disk-play')?.addEventListener('click', playDiskAnim);
  $('btn-disk-step')?.addEventListener('click', () => {
    clearInterval(state.diskTimer);
    if (!state.diskResult) return;
    state.diskAnimStep = (state.diskAnimStep + 1) % state.diskResult.path.length;
    drawDiskCanvas(state.diskAnimStep);
  });

  // ===========================================================================
  // MODULE 7: SYNCHRONIZATION & MULTITHREADING LAB
  // ===========================================================================
  function syncPayload(modeOverride = null) {
    return {
      mode: modeOverride || $('sync-mode').value,
      threads: parseInt($('sync-threads').value, 10) || 4,
      iterations: parseInt($('sync-iters').value, 10) || 1500,
      permits: parseInt($('sync-permits').value, 10) || 2,
      bufferSize: parseInt($('sync-buf').value, 10) || 4,
      producers: Math.max(1, Math.floor((parseInt($('sync-threads').value, 10) || 4) / 2)),
      consumers: Math.max(1, Math.ceil((parseInt($('sync-threads').value, 10) || 4) / 2)),
      traceLimit: 250,
    };
  }

  function renderSyncCard(r) {
    const ok = r.correct;
    return `<div class="panel" style="border-color:${ok ? 'rgba(52,211,153,0.45)' : 'rgba(251,113,133,0.55)'};">
      <div class="panel-head">
        <div class="panel-title">${esc(r.mode.toUpperCase())} Execution Result</div>
        <span class="badge ${ok ? 'badge-RUNNING' : 'badge-FAILED'}">${ok ? '✓ CONSISTENCY VERIFIED' : '✗ RACE CONDITION DETECTED'}</span>
      </div>
      <div class="kpi-grid" style="margin-bottom:10px;">
        <div class="kpi-card"><div class="kpi-label">Expected Value</div><div class="kpi-value">${r.expected}</div></div>
        <div class="kpi-card ${ok ? 'emerald' : 'rose'}"><div class="kpi-label">Actual Value</div><div class="kpi-value">${r.actual}</div></div>
        <div class="kpi-card ${r.lostUpdates > 0 ? 'rose' : 'emerald'}"><div class="kpi-label">Lost Updates</div><div class="kpi-value">${r.lostUpdates ?? 0}</div></div>
        <div class="kpi-card cyan"><div class="kpi-label">Elapsed Time</div><div class="kpi-value">${r.elapsedMs} ms</div><div class="kpi-sub">${r.threads} native OS threads</div></div>
        ${r.maxConcurrentInCriticalSection !== undefined ? `<div class="kpi-card violet"><div class="kpi-label">Max In CS</div><div class="kpi-value">${r.maxConcurrentInCriticalSection}</div><div class="kpi-sub">${r.contentions} contentions</div></div>` : ''}
        ${r.maxConcurrent !== undefined ? `<div class="kpi-card violet"><div class="kpi-label">Max Active</div><div class="kpi-value">${r.maxConcurrent}/${r.permits}</div><div class="kpi-sub">${r.blockedWaits} blocked</div></div>` : ''}
      </div>
      <p style="font-size:12.5px;color:var(--text-secondary);">${esc(r.explanation)}</p>
      <div style="font-size:11px;color:var(--text-muted);margin-top:6px;">Kernel Backend: ${esc(r.backend)}</div>
    </div>`;
  }

  function renderSyncTrace(r) {
    $('sync-trace-meta').textContent = `Showing ${r.trace.length} of ${r.traceTotalEvents} captured thread interleaving events`;
    $('sync-trace-log').innerHTML = r.trace.map((e) => `
      <div class="event-item">
        <span class="event-t">${e.tUs}µs</span>
        <span style="min-width:60px;color:${colorForPid(e.thread + 1)};font-weight:700;">Thread ${e.thread}</span>
        <span style="min-width:95px;color:var(--amber);font-weight:700;">${esc(e.action)}</span>
        <span>val=${e.value}${e.active ? ` · active=${e.active}` : ''}</span>
      </div>`).join('');
  }

  $('btn-sync-run')?.addEventListener('click', async () => {
    try {
      const r = await api('/api/sync', syncPayload());
      $('sync-results-area').innerHTML = renderSyncCard(r);
      renderSyncTrace(r);
    } catch (e) { toast(e.message, true); }
  });

  $('btn-sync-compare')?.addEventListener('click', async () => {
    try {
      const [race, mtx] = await Promise.all([
        api('/api/sync', syncPayload('race')),
        api('/api/sync', syncPayload('mutex')),
      ]);
      $('sync-results-area').innerHTML = `<div class="grid-2">${renderSyncCard(race)}${renderSyncCard(mtx)}</div>`;
      renderSyncTrace(race);
    } catch (e) { toast(e.message, true); }
  });

  // ===========================================================================
  // MODULE 8: DEADLOCK DETECTION & RAG MANAGEMENT
  // ===========================================================================
  async function loadDeadlockPresets() {
    try {
      const presets = await api('/api/deadlock/scenarios');
      const sel = $('deadlock-scenario-select');
      if (!sel) return;
      sel.innerHTML = '<option value="">Load Deadlock Scenario...</option>' +
        presets.map((s) => `<option value="${s.id}">${esc(s.name)}</option>`).join('');
      state.deadlockPresets = presets;
    } catch (e) {
      console.warn('Could not load deadlock presets: ' + e.message);
    }
  }

  $('btn-load-deadlock-preset')?.addEventListener('click', () => {
    const sel = $('deadlock-scenario-select');
    const p = state.deadlockPresets?.find((s) => s.id === sel.value);
    if (!p) return toast('Select a deadlock scenario preset first.', true);
    state.deadlockNodes = JSON.parse(JSON.stringify(p.nodes));
    renderDeadlockUI();
    toast(`Loaded preset: ${p.name}`);
    runDeadlockDetection();
  });

  function renderDeadlockUI() {
    const tbody = $('deadlock-nodes-tbody');
    if (!tbody) return;
    tbody.innerHTML = state.deadlockNodes.map((n, idx) => `
      <tr>
        <td><b>${n.pid}</b></td>
        <td><span style="color:${colorForPid(n.pid)};font-weight:600;">${esc(n.name)}</span></td>
        <td>${n.holding && n.holding.length ? n.holding.map((r) => `<span class="badge badge-RUNNING">${esc(r)}</span>`).join(' ') : '—'}</td>
        <td>${n.waiting && n.waiting.length ? n.waiting.map((r) => `<span class="badge badge-WAITING">${esc(r)}</span>`).join(' ') : '—'}</td>
        <td><button class="btn btn-sm btn-danger" data-del-dl="${idx}">Remove</button></td>
      </tr>
    `).join('') || '<tr><td colspan="5" style="text-align:center;color:var(--text-muted);padding:14px;">No process nodes defined.</td></tr>';

    tbody.querySelectorAll('[data-del-dl]').forEach((b) => {
      b.addEventListener('click', () => {
        const idx = parseInt(b.dataset.delDl, 10);
        state.deadlockNodes.splice(idx, 1);
        renderDeadlockUI();
        runDeadlockDetection();
      });
    });

    drawRAGCanvas(state.deadlockResult);
  }

  $('deadlock-add-form')?.addEventListener('submit', (e) => {
    e.preventDefault();
    const pid = parseInt($('dl-pid').value, 10);
    const name = $('dl-name').value.trim() || `worker-${pid}`;
    const h = $('dl-holding').value.split(/[\s,;]+/).map((s) => s.trim()).filter(Boolean);
    const w = $('dl-waiting').value.split(/[\s,;]+/).map((s) => s.trim()).filter(Boolean);

    state.deadlockNodes.push({ pid, name, holding: h, waiting: w });
    renderDeadlockUI();
    $('deadlock-add-form').reset();
    $('dl-pid').value = pid + 1;
    runDeadlockDetection();
  });

  $('btn-clear-deadlock-nodes')?.addEventListener('click', () => {
    state.deadlockNodes = [];
    state.deadlockResult = null;
    $('deadlock-status-banner').style.display = 'none';
    $('deadlock-recovery-panel').style.display = 'none';
    renderDeadlockUI();
  });

  $('btn-deadlock-detect')?.addEventListener('click', runDeadlockDetection);

  async function runDeadlockDetection() {
    if (!state.deadlockNodes.length) {
      toast('Add at least one process node to detect deadlock.', true);
      return;
    }
    try {
      const res = await api('/api/deadlock/detect', { scenario: state.deadlockNodes });
      state.deadlockResult = res;

      const banner = $('deadlock-status-banner');
      const dot = $('deadlock-status-dot');
      const title = $('deadlock-status-title');
      const desc = $('deadlock-status-desc');
      const badge = $('deadlock-status-badge');
      const recovery = $('deadlock-recovery-panel');

      banner.style.display = 'flex';
      if (res.hasDeadlock) {
        banner.className = 'health-bar critical';
        dot.className = 'status-dot critical';
        title.textContent = 'CRITICAL DEADLOCK DETECTED (Circular Wait)';
        desc.textContent = res.description;
        badge.className = 'badge badge-FAILED';
        badge.textContent = 'DEADLOCK CONFIRMED';

        // Populate recovery buttons
        recovery.style.display = 'block';
        const actionsDiv = $('deadlock-recovery-actions');
        actionsDiv.innerHTML = res.cycle.map((pid) => `
          <button class="btn btn-danger btn-sm" data-victim-pid="${pid}">
            ⚡ Terminate Victim Process P${pid}
          </button>
        `).join('') + `
          <button class="btn btn-success btn-sm" id="btn-force-release-all">
            🔓 Force Preempt All Locks
          </button>
        `;

        actionsDiv.querySelectorAll('[data-victim-pid]').forEach((b) => {
          b.addEventListener('click', () => {
            const vPid = parseInt(b.dataset.victimPid, 10);
            state.deadlockNodes = state.deadlockNodes.filter((n) => n.pid !== vPid);
            toast(`Terminated process P${vPid} to break circular wait.`);
            renderDeadlockUI();
            runDeadlockDetection();
          });
        });

        $('btn-force-release-all')?.addEventListener('click', () => {
          state.deadlockNodes.forEach((n) => { n.holding = []; });
          toast('Force-released all held locks.');
          renderDeadlockUI();
          runDeadlockDetection();
        });

      } else {
        banner.className = 'health-bar healthy';
        dot.className = 'status-dot healthy';
        title.textContent = 'Resource Allocation Safe (DAG Acyclic)';
        desc.textContent = res.description;
        badge.className = 'badge badge-RUNNING';
        badge.textContent = 'NO DEADLOCK';
        recovery.style.display = 'none';
      }

      drawRAGCanvas(res);
    } catch (e) {
      toast(e.message, true);
    }
  }

  function drawRAGCanvas(res) {
    const canvas = $('rag-canvas');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const W = canvas.width, H = canvas.height;
    ctx.clearRect(0, 0, W, H);

    if (!state.deadlockNodes.length) {
      ctx.fillStyle = '#64748b';
      ctx.font = '12px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText('Resource Allocation Graph will render here', W / 2, H / 2);
      return;
    }

    const procs = state.deadlockNodes;
    const resSet = new Set();
    procs.forEach((p) => {
      (p.holding || []).forEach((r) => resSet.add(r));
      (p.waiting || []).forEach((r) => resSet.add(r));
    });
    const resources = Array.from(resSet);

    // Position process nodes on left circle, resources on right circle
    const pos = {};
    const cy = H / 2;

    procs.forEach((p, i) => {
      const angle = ((i + 0.5) / Math.max(1, procs.length)) * Math.PI - Math.PI / 2;
      pos['P' + p.pid] = {
        x: W * 0.28 + Math.cos(angle) * 70,
        y: cy + Math.sin(angle) * (H * 0.36),
        label: p.name || `P${p.pid}`,
        isProc: true,
        pid: p.pid
      };
    });

    resources.forEach((r, i) => {
      const angle = ((i + 0.5) / Math.max(1, resources.length)) * Math.PI - Math.PI / 2;
      pos[r] = {
        x: W * 0.72 - Math.cos(angle) * 70,
        y: cy + Math.sin(angle) * (H * 0.36),
        label: r,
        isProc: false
      };
    });

    // Draw Edges
    const edges = res?.ragEdges || [];
    edges.forEach((e) => {
      const from = pos[e.from];
      const to = pos[e.to];
      if (!from || !to) return;

      const isAssignment = e.type === 'assignment'; // Resource -> Process (Holding)
      ctx.strokeStyle = isAssignment ? '#34d399' : '#fb7185';
      ctx.lineWidth = isAssignment ? 2 : 1.8;
      if (!isAssignment) ctx.setLineDash([4, 3]);
      else ctx.setLineDash([]);

      ctx.beginPath();
      ctx.moveTo(from.x, from.y);
      ctx.lineTo(to.x, to.y);
      ctx.stroke();
      ctx.setLineDash([]);

      // Draw arrow head
      const angle = Math.atan2(to.y - from.y, to.x - from.x);
      const headlen = 7;
      ctx.fillStyle = ctx.strokeStyle;
      ctx.beginPath();
      ctx.moveTo(to.x, to.y);
      ctx.lineTo(to.x - headlen * Math.cos(angle - Math.PI / 6), to.y - headlen * Math.sin(angle - Math.PI / 6));
      ctx.lineTo(to.x - headlen * Math.cos(angle + Math.PI / 6), to.y - headlen * Math.sin(angle + Math.PI / 6));
      ctx.fill();
    });

    // Draw Nodes
    Object.values(pos).forEach((n) => {
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';

      if (n.isProc) {
        // Circle for Process
        const inCycle = res?.cycle?.includes(n.pid);
        ctx.fillStyle = inCycle ? 'rgba(251, 113, 133, 0.3)' : 'rgba(56, 189, 248, 0.25)';
        ctx.strokeStyle = inCycle ? '#fb7185' : '#38bdf8';
        ctx.lineWidth = inCycle ? 3 : 2;
        ctx.beginPath();
        ctx.arc(n.x, n.y, 20, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#f8fafc';
        ctx.font = 'bold 10px monospace';
        ctx.fillText(`P${n.pid}`, n.x, n.y);
      } else {
        // Square for Resource
        ctx.fillStyle = 'rgba(251, 191, 36, 0.22)';
        ctx.strokeStyle = '#fbbf24';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.rect(n.x - 22, n.y - 14, 44, 28);
        ctx.fill();
        ctx.stroke();

        ctx.fillStyle = '#f8fafc';
        ctx.font = 'bold 9.5px sans-serif';
        ctx.fillText(n.label.slice(0, 7), n.x, n.y);
      }
    });
  }

  // ===========================================================================
  // MODULE 9: SCENARIO LAB
  // ===========================================================================
  async function loadScenarios() {
    try {
      const scenarios = await api('/api/scenarios');
      state.scenarios = scenarios;
      const grid = $('scenario-grid');
      if (!grid) return;
      grid.innerHTML = scenarios.map((s) => `
        <div class="scenario-card" data-scenario-id="${s.id}">
          <div style="display:flex;justify-content:space-between;align-items:center;">
            <div class="scenario-icon">${s.icon}</div>
            <span class="badge badge-NEW">${esc(s.category)}</span>
          </div>
          <div class="scenario-name">${esc(s.name)}</div>
          <div class="scenario-desc">${esc(s.description)}</div>
          <button class="btn btn-primary btn-sm" style="margin-top:auto;" data-run-scenario="${s.id}">
            ▶ Execute Scenario
          </button>
        </div>
      `).join('');

      grid.querySelectorAll('[data-run-scenario]').forEach((b) => {
        b.addEventListener('click', (e) => {
          e.stopPropagation();
          runScenario(b.dataset.runScenario);
        });
      });
    } catch (e) {
      toast('Failed to load scenarios: ' + e.message, true);
    }
  }

  async function runScenario(scenarioId) {
    const s = state.scenarios.find((x) => x.id === scenarioId);
    if (!s) return;
    toast(`Deploying scenario: ${s.name}...`);
    try {
      // Configure engine options from scenario config
      if (s.config) {
        if (s.config.cpuAlgorithm) $('cfg-cpu').value = s.config.cpuAlgorithm;
        if (s.config.quantum) $('cfg-quantum').value = s.config.quantum;
        if (s.config.memoryTotal) $('cfg-mem-total').value = s.config.memoryTotal;
        if (s.config.memoryStrategy) $('cfg-mem-strategy').value = s.config.memoryStrategy;
        if (s.config.diskAlgorithm) $('cfg-disk-algo').value = s.config.diskAlgorithm;
      }
      await runIntegratedSim(true);
      $('scenario-run-panel').style.display = 'block';
      $('scenario-run-title').textContent = `${s.icon} ${s.name} — Execution Complete`;
      $('scenario-run-sub').textContent = `Makespan: ${state.simResult.summary.makespan} ticks · CPU: ${state.simResult.summary.cpuUtilization}% · Peak RAM: ${state.simResult.summary.peakMemoryUtilization}%`;
      renderGantt($('scenario-run-gantt'), state.simResult.gantt);
    } catch (e) {
      toast(e.message, true);
    }
  }

  // ===========================================================================
  // MODULE 10: INCIDENTS & MONITORING
  // ===========================================================================
  async function loadIncidents() {
    try {
      const sev = $('inc-filter-sev')?.value || 'all';
      const status = $('inc-filter-status')?.value || 'all';
      const incidents = await api(`/api/incidents?severity=${sev}&status=${status}`);
      state.incidents = incidents;
      renderIncidentFeed();
      updateIncidentKPIs();
    } catch (e) {
      toast('Failed to load incidents: ' + e.message, true);
    }
  }

  function updateIncidentKPIs() {
    const all = state.incidents;
    const critical = all.filter((i) => i.severity === 'critical' && i.status === 'active').length;
    const warning = all.filter((i) => i.severity === 'warning' && i.status === 'active').length;
    const info = all.filter((i) => i.severity === 'info' && i.status === 'active').length;
    const resolved = all.filter((i) => i.status === 'resolved').length;

    if ($('inc-kpi-critical')) $('inc-kpi-critical').textContent = critical;
    if ($('inc-kpi-warning')) $('inc-kpi-warning').textContent = warning;
    if ($('inc-kpi-info')) $('inc-kpi-info').textContent = info;
    if ($('inc-kpi-resolved')) $('inc-kpi-resolved').textContent = resolved;
    if ($('kpi-incidents-active')) $('kpi-incidents-active').textContent = critical + warning;
    if ($('nav-count-incidents')) $('nav-count-incidents').textContent = critical + warning;
  }

  function renderIncidentFeed() {
    const list = $('incident-list');
    if (!list) return;
    if (!state.incidents.length) {
      list.innerHTML = '<div style="color:var(--text-muted);padding:24px;text-align:center;">No incidents matching current criteria.</div>';
      return;
    }

    list.innerHTML = state.incidents.map((inc) => `
      <div class="incident-card ${inc.severity} ${inc.status === 'resolved' ? 'resolved' : ''}">
        <div style="flex:1;">
          <div style="display:flex;align-items:center;gap:8px;margin-bottom:4px;">
            <span class="badge ${inc.severity === 'critical' ? 'badge-FAILED' : (inc.severity === 'warning' ? 'badge-WAITING' : 'badge-READY')}">
              ${esc(inc.severity.toUpperCase())}
            </span>
            <b style="font-size:13px;color:#fff;">${esc(inc.title)}</b>
            <span style="font-size:11px;color:var(--text-muted);">${new Date(inc.createdAt).toLocaleTimeString()}</span>
          </div>
          <div style="font-size:12px;color:var(--text-secondary);">${esc(inc.description)}</div>
          <div style="font-size:11px;color:var(--text-muted);margin-top:4px;">Node Target: <b>${esc(inc.server)}</b></div>
          ${inc.resolution ? `<div style="font-size:11px;color:var(--emerald);margin-top:3px;">Resolution: ${esc(inc.resolution)}</div>` : ''}
        </div>
        ${inc.status === 'active'
          ? `<button class="btn btn-sm btn-success" data-resolve-id="${inc.id}">✓ Resolve</button>`
          : `<span class="badge badge-RUNNING">RESOLVED</span>`
        }
      </div>
    `).join('');

    list.querySelectorAll('[data-resolve-id]').forEach((b) => {
      b.addEventListener('click', async () => {
        try {
          await api(`/api/incidents/${b.dataset.resolveId}/resolve`, { resolution: 'Operator acknowledged and marked mitigated.' });
          toast('Incident resolved.');
          await loadIncidents();
          await fetchPlatformStatus();
        } catch (e) { toast(e.message, true); }
      });
    });
  }

  $('btn-toggle-incident-form')?.addEventListener('click', () => {
    const p = $('incident-form-panel');
    p.style.display = p.style.display === 'none' ? 'block' : 'none';
  });
  $('btn-cancel-incident-form')?.addEventListener('click', () => {
    $('incident-form-panel').style.display = 'none';
  });
  $('btn-refresh-incidents')?.addEventListener('click', loadIncidents);
  $('inc-filter-status')?.addEventListener('change', loadIncidents);
  $('inc-filter-sev')?.addEventListener('change', loadIncidents);

  $('incident-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    try {
      const srvSel = $('inc-server-select');
      await api('/api/incidents', {
        title: $('inc-title').value.trim(),
        severity: $('inc-severity').value,
        server: srvSel.options[srvSel.selectedIndex]?.text || 'Data Center',
        serverId: srvSel.value,
        description: $('inc-desc').value.trim()
      });
      toast('Logged incident successfully.');
      $('incident-form').reset();
      $('incident-form-panel').style.display = 'none';
      await loadIncidents();
      await fetchPlatformStatus();
    } catch (err) { toast(err.message, true); }
  });

  // ===========================================================================
  // MODULE 11: BENCHMARKING ANALYTICS
  // ===========================================================================
  document.querySelectorAll('[data-cmp]').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('[data-cmp]').forEach((x) => x.classList.remove('active'));
      b.classList.add('active');
      state.cmpMode = b.dataset.cmp;
      runComparison();
    });
  });
  $('btn-compare-run')?.addEventListener('click', runComparison);

  async function runComparison() {
    state.cmpLoaded = true;
    try {
      if (state.cmpMode === 'cpu') {
        if (!state.jobs.length) return toast('Add processes first.', true);
        const res = await api('/api/cpu/compare', { processes: state.jobs, quantum: parseInt($('cfg-quantum').value, 10) || 2, priorityOrder: 'LOWER' });
        state.lastCmpData = res;
        renderCompareTable(res, 'algorithm', [
          ['algorithmName', 'Algorithm'], ['avgWaiting', 'Avg Wait'], ['avgTurnaround', 'Avg TAT'],
          ['avgResponse', 'Avg Resp'], ['cpuUtilization', 'CPU Util %'], ['throughput', 'Throughput'], ['contextSwitches', 'Ctx Switches']
        ]);
        renderCompareCharts(res.rows.map((r) => r.algorithm),
          'Average Waiting & Turnaround Time (lower is better)',
          [{ label: 'Avg Wait', data: res.rows.map((r) => r.avgWaiting), backgroundColor: '#38bdf8' },
           { label: 'Avg Turnaround', data: res.rows.map((r) => r.avgTurnaround), backgroundColor: '#818cf8' }],
          'Average Response Time & Context Switches',
          [{ label: 'Avg Response', data: res.rows.map((r) => r.avgResponse), backgroundColor: '#34d399' },
           { label: 'Context Switches', data: res.rows.map((r) => r.contextSwitches), backgroundColor: '#fbbf24' }]);
        $('cmp-analysis-box').innerHTML = `<b>Empirical Analysis:</b> SRTF and SJF mathematically minimize average wait times under static batches; Round Robin provides the lowest initial response variance suitable for interactive multi-tenant containers.`;
      } else if (state.cmpMode === 'memory') {
        if (!state.jobs.length) return toast('Add processes first.', true);
        const memJobs = state.jobs.map((j) => ({ pid: j.pid, name: j.name, size: Math.max(1, j.memory || 64), arrival: j.arrival, duration: j.cpuBurst }));
        const parts = [100, 200, 150, 250, 300];
        const res = await api('/api/memory/compare', { total: parseInt($('cfg-mem-total').value, 10) || 512, mode: 'DYNAMIC', partitions: parts, jobs: memJobs });
        state.lastCmpData = res;
        renderCompareTable(res, 'strategy', [
          ['strategyName', 'Strategy'], ['allocated', 'Allocated'], ['allocationFailures', 'Failures'],
          ['avgUtilization', 'Avg Util %'], ['peakExternalFragmentation', 'Peak Ext Frag'], ['avgMemoryWait', 'Avg Mem Wait']
        ]);
        renderCompareCharts(res.rows.map((r) => r.strategyName),
          'Average Memory Utilization % (higher is better)',
          [{ label: 'Avg Util %', data: res.rows.map((r) => r.avgUtilization), backgroundColor: '#34d399' }],
          'Allocation Failures & Peak External Fragmentation (KB)',
          [{ label: 'Failures', data: res.rows.map((r) => r.allocationFailures), backgroundColor: '#fb7185' },
           { label: 'Peak Ext Frag (KB)', data: res.rows.map((r) => r.peakExternalFragmentation), backgroundColor: '#fbbf24' }]);
        $('cmp-analysis-box').innerHTML = `<b>Empirical Analysis:</b> First Fit incurs lowest search overhead; Best Fit minimizes immediate allocation waste but tends to generate unusable tiny fragment slivers.`;
      } else {
        const reqs = [98, 183, 37, 122, 14, 124, 65, 67];
        const res = await api('/api/disk/compare', { cylinders: 200, head: 53, direction: 'UP', requests: reqs });
        state.lastCmpData = res;
        renderCompareTable(res, 'algorithm', [
          ['algorithmName', 'Algorithm'], ['totalMovement', 'Total Head Seek'], ['averageSeek', 'Average Seek Distance'], ['requestCount', 'Requests']
        ]);
        renderCompareCharts(res.rows.map((r) => r.algorithm),
          'Total Head Movement in Cylinders (lower is better)',
          [{ label: 'Total Movement', data: res.rows.map((r) => r.totalMovement), backgroundColor: '#38bdf8' }],
          'Average Seek Distance (lower is better)',
          [{ label: 'Avg Seek', data: res.rows.map((r) => r.averageSeek), backgroundColor: '#a78bfa' }]);
        $('cmp-analysis-box').innerHTML = `<b>Empirical Analysis:</b> SSTF minimizes total seek distance but starves extreme cylinders; SCAN and C-LOOK guarantee upper-bounded wait times under continuous loads.`;
      }
    } catch (e) { toast(e.message, true); }
  }

  function renderCompareTable(res, keyField, cols) {
    const isBest = (metric, keyVal) => res.best?.[metric]?.winners?.includes(keyVal);
    $('cmp-table-wrap').innerHTML = `<table><thead><tr>${cols.map((c) => `<th>${c[1]}</th>`).join('')}</tr></thead><tbody>${
      res.rows.map((r) => `<tr>${cols.map((c, idx) => `<td class="${idx === 0 ? 'sans' : ''} ${isBest(c[0], r[keyField]) ? 'best-cell' : ''}">${esc(r[c[0]])}</td>`).join('')}</tr>`).join('')
    }</tbody></table>`;
  }

  function renderCompareCharts(labels, t1, ds1, t2, ds2) {
    $('cmp-chart1-title').textContent = t1;
    $('cmp-chart2-title').textContent = t2;
    const opts = {
      responsive: true, maintainAspectRatio: false,
      plugins: { legend: { labels: { color: '#cbd5e1', font: { size: 11 } } } },
      scales: {
        x: { ticks: { color: '#94a3b8' }, grid: { color: 'rgba(148,163,184,0.08)' } },
        y: { beginAtZero: true, ticks: { color: '#94a3b8' }, grid: { color: 'rgba(148,163,184,0.08)' } }
      }
    };
    if (state.charts.cmp1) state.charts.cmp1.destroy();
    if (state.charts.cmp2) state.charts.cmp2.destroy();
    state.charts.cmp1 = new Chart($('chart-cmp-1').getContext('2d'), { type: 'bar', data: { labels, datasets: ds1 }, options: opts });
    state.charts.cmp2 = new Chart($('chart-cmp-2').getContext('2d'), { type: 'bar', data: { labels, datasets: ds2 }, options: opts });
  }

  $('btn-export-cmp-csv')?.addEventListener('click', () => {
    if (!state.lastCmpData) return toast('Run a benchmark first.', true);
    const rows = state.lastCmpData.rows;
    if (!rows.length) return;
    const keys = Object.keys(rows[0]);
    const csv = [keys.join(','), ...rows.map((r) => keys.map((k) => `"${r[k]}"`).join(','))].join('\n');
    downloadFile(`datacenteros_${state.cmpMode}_benchmark.csv`, csv, 'text/csv');
  });

  $('btn-export-cmp-json')?.addEventListener('click', () => {
    if (!state.lastCmpData) return toast('Run a benchmark first.', true);
    downloadFile(`datacenteros_${state.cmpMode}_benchmark.json`, JSON.stringify(state.lastCmpData, null, 2), 'application/json');
  });

  // ===========================================================================
  // MODULE 12: SYSTEM EVENT LOGS
  // ===========================================================================
  async function loadLogs() {
    try {
      const type = $('log-filter-type')?.value || 'all';
      const sev = $('log-filter-sev')?.value || 'all';
      const search = $('log-search-input')?.value || '';
      const events = await api(`/api/events?type=${type}&severity=${sev}&search=${encodeURIComponent(search)}&limit=150`);
      state.events = events;
      renderLogTable();
    } catch (e) { toast('Failed to load event logs: ' + e.message, true); }
  }

  function renderLogTable() {
    const tbody = $('logs-tbody');
    if (!tbody) return;
    if (!state.events.length) {
      tbody.innerHTML = '<tr><td colspan="5" style="text-align:center;color:var(--text-muted);padding:24px;">No event logs recorded matching criteria.</td></tr>';
      return;
    }
    tbody.innerHTML = state.events.map((e) => `
      <tr>
        <td style="color:var(--text-muted);">${new Date(e.timestamp).toLocaleTimeString()}</td>
        <td><span class="badge badge-NEW">${esc(e.type)}</span></td>
        <td style="color:var(--cyan);font-family:var(--font-mono);">${esc(e.source)}</td>
        <td><span class="badge ${e.severity === 'critical' ? 'badge-FAILED' : (e.severity === 'warning' ? 'badge-WAITING' : 'badge-READY')}">${esc(e.severity.toUpperCase())}</span></td>
        <td class="sans">${esc(e.message)}</td>
      </tr>
    `).join('');
  }

  $('log-filter-type')?.addEventListener('change', loadLogs);
  $('log-filter-sev')?.addEventListener('change', loadLogs);
  $('log-search-input')?.addEventListener('input', loadLogs);
  $('btn-refresh-logs')?.addEventListener('click', loadLogs);
  $('btn-export-logs-csv')?.addEventListener('click', () => {
    if (!state.events.length) return toast('No logs to export.', true);
    const header = 'timestamp,type,source,severity,message';
    const lines = state.events.map((e) => `"${new Date(e.timestamp).toISOString()}","${e.type}","${e.source}","${e.severity}","${e.message.replace(/"/g, '""')}"`);
    downloadFile('datacenteros_events.csv', [header, ...lines].join('\n'), 'text/csv');
  });

  // ===========================================================================
  // GLOBAL SIMULATION CONTROLS & LOADERS
  // ===========================================================================
  $('btn-sim-run')?.addEventListener('click', () => runIntegratedSim(true));
  $('btn-sim-pause')?.addEventListener('click', () => {
    if (!state.simResult) return;
    if (state.playing) stopPlayback();
    else startPlayback();
  });
  $('btn-sim-step')?.addEventListener('click', async () => {
    if (!state.simResult) {
      await runIntegratedSim(false);
      return;
    }
    stopPlayback();
    if (state.tick < state.simResult.timeline.length - 1) renderTick(state.tick + 1);
  });
  $('btn-sim-reset')?.addEventListener('click', () => {
    stopPlayback();
    if (state.simResult) renderTick(0);
  });
  $('sim-scrubber')?.addEventListener('input', (e) => {
    stopPlayback();
    renderTick(parseInt(e.target.value, 10) || 0);
  });
  $('sim-speed')?.addEventListener('change', () => { if (state.playing) startPlayback(); });

  $('sample-select')?.addEventListener('change', (e) => {
    const s = state.samples.find((x) => x.id === e.target.value);
    if (!s) return;
    loadSample(s);
    e.target.value = '';
  });

  function loadSample(s) {
    state.jobs = JSON.parse(JSON.stringify(s.processes));
    const rc = s.recommendedConfig || {};
    if (rc.cpuAlgorithm) { $('cfg-cpu').value = rc.cpuAlgorithm; if ($('cpu-algo')) $('cpu-algo').value = rc.cpuAlgorithm; }
    if (rc.quantum) { $('cfg-quantum').value = rc.quantum; if ($('cpu-quantum')) $('cpu-quantum').value = rc.quantum; }
    if (rc.memoryTotal) { $('cfg-mem-total').value = rc.memoryTotal; if ($('mem-total')) $('mem-total').value = rc.memoryTotal; }
    if (rc.memoryStrategy) { $('cfg-mem-strategy').value = rc.memoryStrategy; if ($('mem-strategy')) $('mem-strategy').value = rc.memoryStrategy; }
    if (rc.diskAlgorithm) { $('cfg-disk-algo').value = rc.diskAlgorithm; if ($('disk-algo')) $('disk-algo').value = rc.diskAlgorithm; }
    state.simResult = null;
    state.cpuResult = null;
    state.memResult = null;
    renderProcessTable();
    toast(`Loaded workload preset: ${s.name}`);
    runIntegratedSim(false);
  }

  $('btn-random-workload')?.addEventListener('click', async () => {
    try {
      const gen = await api('/api/generate', { profile: 'mixed', count: 7, seed: Math.floor(Math.random() * 100000) });
      state.jobs = gen;
      state.simResult = null;
      renderProcessTable();
      toast('Generated 7 randomized data center tasks via C++ engine.');
      runIntegratedSim(false);
    } catch (e) { toast(e.message, true); }
  });

  $('btn-export-report')?.addEventListener('click', () => {
    if (!state.simResult) return toast('Run a simulation first to export its full telemetry report.', true);
    downloadFile('datacenteros_telemetry_report.json', JSON.stringify(state.simResult, null, 2), 'application/json');
  });

  // ===========================================================================
  // PLATFORM STATUS & INITIALIZATION
  // ===========================================================================
  async function fetchPlatformStatus() {
    try {
      const s = await api('/api/status');
      state.platformStatus = s;
      const hDot = $('health-bar-dot');
      const hTitle = $('health-bar-title');
      const hBadge = $('health-bar-badge');
      const sBarText = $('sidebar-health-text');
      const banner = $('overview-health-bar');

      if (s.health === 'healthy') {
        banner.className = 'health-bar healthy';
        if (hDot) hDot.className = 'status-dot healthy';
        if (hTitle) hTitle.textContent = 'Data Center Fleet Status: Normal Operations';
        if (hBadge) { hBadge.className = 'badge badge-RUNNING'; hBadge.textContent = 'HEALTHY'; }
        if (sBarText) { sBarText.textContent = 'OPTIMAL'; sBarText.style.color = 'var(--emerald)'; }
      } else if (s.health === 'degraded') {
        banner.className = 'health-bar degraded';
        if (hDot) hDot.className = 'status-dot degraded';
        if (hTitle) hTitle.textContent = 'Data Center Fleet Status: Performance Degraded';
        if (hBadge) { hBadge.className = 'badge badge-WAITING'; hBadge.textContent = 'DEGRADED'; }
        if (sBarText) { sBarText.textContent = 'DEGRADED'; sBarText.style.color = 'var(--amber)'; }
      } else {
        banner.className = 'health-bar critical';
        if (hDot) hDot.className = 'status-dot critical';
        if (hTitle) hTitle.textContent = 'Data Center Fleet Status: Critical Incident Alert';
        if (hBadge) { hBadge.className = 'badge badge-FAILED'; hBadge.textContent = 'CRITICAL'; }
        if (sBarText) { sBarText.textContent = 'CRITICAL'; sBarText.style.color = 'var(--rose)'; }
      }
    } catch (e) {
      console.warn('Status check failed: ' + e.message);
    }
  }

  async function init() {
    try {
      const info = await api('/api/info');
      $('engine-status').innerHTML = '<span class="pulse"></span>C++ Kernel Online';
      $('engine-backend').textContent = info.threadingBackend;

      // Parallel bootstrap of platform data
      const [samples, servers, incidents, scenarios] = await Promise.all([
        api('/api/samples'),
        api('/api/servers'),
        api('/api/incidents'),
        api('/api/scenarios')
      ]);

      state.samples = samples;
      state.servers = servers;
      state.incidents = incidents;
      state.scenarios = scenarios;

      const sel = $('sample-select');
      state.samples.forEach((s) => {
        const opt = document.createElement('option');
        opt.value = s.id;
        opt.textContent = s.name;
        sel.appendChild(opt);
      });

      await loadDeadlockPresets();
      updateFleetStatusKPIs();
      updateServerSelects();
      updateIncidentKPIs();
      await fetchPlatformStatus();

      // Load default Mixed Data Center Workload
      const def = state.samples.find((s) => s.id === 'mixed') || state.samples[0];
      if (def) loadSample(def);

      // Periodic background status refresh every 15s
      setInterval(fetchPlatformStatus, 15000);

    } catch (e) {
      $('engine-status').className = 'engine-pill offline';
      $('engine-status').innerHTML = '<span class="pulse"></span>Engine Offline';
      $('engine-backend').textContent = e.message;
      toast('Failed to connect to backend engine: ' + e.message, true);
    }
  }

  init();
})();
