// Smart Data Center OS Simulator — Frontend Controller
// All OS calculations are performed by the C++ engine via /api/* endpoints.
'use strict';

(() => {
  // ----------------------------------------------------------------- State
  const state = {
    jobs: [],
    samples: [],
    filterState: 'ALL',
    simResult: null,
    tick: 0,
    playing: false,
    timer: null,
    cpuResult: null,
    walkIdx: 0,
    memResult: null,
    memStep: 0,
    diskResult: null,
    diskAnimStep: 0,
    diskTimer: null,
    cmpMode: 'cpu',
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
    el.textContent = msg;
    el.className = 'toast' + (isError ? ' error' : '');
    el.style.display = 'block';
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { el.style.display = 'none'; }, 3600);
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

  // ----------------------------------------------------------------- Tooltip
  const tip = $('tooltip');
  function attachTip(el, html) {
    el.addEventListener('mouseenter', (e) => {
      tip.innerHTML = html;
      tip.style.display = 'block';
      moveTip(e);
    });
    el.addEventListener('mousemove', moveTip);
    el.addEventListener('mouseleave', () => { tip.style.display = 'none'; });
  }
  function moveTip(e) {
    tip.style.left = Math.min(window.innerWidth - 270, e.clientX + 14) + 'px';
    tip.style.top = Math.min(window.innerHeight - 120, e.clientY + 14) + 'px';
  }

  // ----------------------------------------------------------------- Navigation
  document.querySelectorAll('.nav-item').forEach((btn) => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('.nav-item').forEach((b) => b.classList.remove('active'));
      document.querySelectorAll('.view').forEach((v) => v.classList.remove('active'));
      btn.classList.add('active');
      const v = btn.dataset.view;
      $('view-' + v).classList.add('active');
      if (v === 'compare' && !state.cmpLoaded) runComparison();
      if (v === 'cpu' && !state.cpuResult && state.jobs.length) runCpuLab();
      if (v === 'memory' && !state.memResult && state.jobs.length) runMemoryLab();
      if (v === 'disk' && !state.diskResult) runDiskLab();
    });
  });

  // ----------------------------------------------------------------- Gantt Renderer
  function renderGantt(container, slices, currentTick = null) {
    container.innerHTML = '';
    if (!slices || !slices.length) {
      container.innerHTML = '<div style="color:var(--text-muted);padding:12px;">No CPU intervals yet.</div>';
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
      blk.style.flex = `${dur} 0 ${Math.max(24, pct * 6)}px`;
      if (s.pid >= 0) blk.style.background = colorForPid(s.pid);
      blk.innerHTML = `<span>${esc(s.name)}</span><span style="font-size:9px;opacity:.75;">${s.start}–${s.end}</span>`;
      attachTip(blk, `<b>${esc(s.name)}</b> ${s.pid >= 0 ? `(PID ${s.pid})` : ''}<br>Start: <b>${s.start}</b> · End: <b>${s.end}</b><br>Duration: <b>${dur}</b> tick(s)`);
      track.appendChild(blk);

      const tk = document.createElement('div');
      tk.className = 'gantt-tick';
      tk.style.flex = `${dur} 0 ${Math.max(24, pct * 6)}px`;
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
    container.innerHTML = '';
    if (!mem || !mem.blocks || !mem.blocks.length) {
      container.innerHTML = '<div style="color:var(--text-muted);padding:12px;">No memory data.</div>';
      return;
    }
    const total = mem.total || 1;
    mem.blocks.forEach((b) => {
      const seg = document.createElement('div');
      const isFree = b.pid < 0;
      seg.className = 'mem-seg ' + (isFree ? 'free' : 'alloc');
      seg.style.width = ((b.size / total) * 100) + '%';
      if (!isFree) seg.style.background = colorForPid(b.pid);
      seg.innerHTML = `<span>${isFree ? 'FREE' : 'P' + b.pid}</span><span style="font-size:9.5px;opacity:.8;">${b.size}K</span>`;
      attachTip(seg, `<b>${isFree ? 'Free Hole' : 'PID ' + b.pid}</b><br>Address: ${b.start} – ${b.start + b.size}<br>Block Size: ${b.size} KB<br>Requested: ${b.used} KB`);
      container.appendChild(seg);
    });
  }

  // ----------------------------------------------------------------- Integrated Simulation
  function getSimConfig() {
    return {
      cpuAlgorithm: $('cfg-cpu').value,
      quantum: parseInt($('cfg-quantum').value, 10) || 2,
      memoryTotal: parseInt($('cfg-mem-total').value, 10) || 1024,
      memoryStrategy: $('cfg-mem-strategy').value,
      diskAlgorithm: $('cfg-disk-algo').value,
      diskCylinders: parseInt($('disk-cyls').value, 10) || 200,
      diskHead: parseInt($('disk-head').value, 10) || 53,
      diskDirection: $('disk-dir').value,
      includeTimeline: true,
    };
  }

  async function runIntegratedSim(autoPlay = true) {
    if (!state.jobs.length) {
      toast('Add at least one job or load a sample workload first.', true);
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
      else toast(`Simulation completed (${res.summary.makespan} ticks, ${res.summary.completed}/${res.summary.processCount} jobs done).`);
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
        toast('Simulation playback finished.');
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

    // KPIs
    $('kpi-total').textContent = state.jobs.length;
    $('kpi-new-sub').textContent = `${snap.counts.new} NEW`;
    $('kpi-running').textContent = snap.counts.running;
    $('kpi-running-name').textContent = snap.runningPid >= 0 ? `${snap.runningName} (PID ${snap.runningPid})` : 'CPU Idle';
    $('kpi-ready').textContent = snap.counts.ready;
    $('kpi-waiting').textContent = snap.counts.waiting;
    $('kpi-completed').textContent = snap.counts.terminated;
    const failedSoFar = snap.processes.filter((p) => p.failed).length;
    $('kpi-failed-sub').textContent = `${failedSoFar} failed`;
    $('kpi-cpu-util').textContent = `${snap.cpuUtilization}%`;
    $('kpi-ctx-sub').textContent = `${snap.contextSwitches} ctx switches`;
    $('kpi-mem-util').textContent = `${snap.memory.utilization}%`;
    $('kpi-mem-sub').textContent = `${snap.memory.reserved} / ${snap.memory.total} KB`;
    $('kpi-disk-move').textContent = snap.disk.totalMovement;
    $('kpi-disk-sub').textContent = `Head @ ${snap.disk.head} (${snap.disk.direction}) · ${snap.disk.completedOps} ops`;
    $('kpi-conflicts').textContent = snap.resourceConflicts;

    // Gantt up to current tick (show full with highlight)
    $('overview-gantt-meta').textContent = `Policy: ${res.config.cpuAlgorithm} · Avg Wait: ${res.summary.avgWaiting} · Avg TAT: ${res.summary.avgTurnaround} · Throughput: ${res.summary.throughput}`;
    renderGantt($('overview-gantt'), res.gantt, snap.time);

    // Memory bar
    $('overview-mem-meta').textContent = `${snap.memory.reserved}/${snap.memory.total} KB (${snap.memory.utilization}%) · Ext Frag: ${snap.memory.externalFragmentation} KB · Failures: ${snap.memory.failures}`;
    renderMemBar($('overview-mem-bar'), snap.memory);
    $('overview-mem-legend').textContent = `Free blocks: ${snap.memory.freeBlocks} · Largest hole: ${snap.memory.largestFree} KB · Strategy: ${snap.memory.strategy}`;

    // Shared resources
    const resBox = $('overview-resources');
    resBox.innerHTML = '';
    snap.resources.forEach((r) => {
      const card = document.createElement('div');
      card.className = 'res-card' + (r.locked ? ' locked' : '');
      card.innerHTML = `
        <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;">
          <b>${esc(r.name)}</b>
          <span class="badge ${r.locked ? 'badge-WAITING' : 'badge-RUNNING'}">${r.kind.toUpperCase()} (${r.holders.length}/${r.capacity})</span>
        </div>
        <div style="font-size:11.5px;color:var(--text-secondary);font-family:var(--font-mono);">
          Holders: ${r.holders.length ? r.holders.map((p) => 'P' + p).join(', ') : 'None'}<br>
          Waiters: ${r.waiters.length ? r.waiters.map((p) => 'P' + p).join(', ') : 'None'}<br>
          Acquires: ${r.acquisitions} · Conflicts: ${r.conflicts}
        </div>`;
      resBox.appendChild(card);
    });

    // Event log up to current time
    const evs = res.events.filter((e) => e.time <= snap.time);
    $('overview-event-count').textContent = `${evs.length} / ${res.events.length} events`;
    const logEl = $('overview-events');
    logEl.innerHTML = evs.slice(-60).reverse().map((e) => `
      <div class="event-item">
        <span class="event-t">t=${e.time}</span>
        <span class="event-type ev-${esc(e.type)}">${esc(e.type)}</span>
        <span>${esc(e.message)}</span>
      </div>`).join('');

    // Update Process Table live states
    renderProcessTable();
  }

  function renderOverviewChart(res) {
    const ctx = $('chart-overview-telemetry').getContext('2d');
    if (state.charts.overview) state.charts.overview.destroy();
    const h = res.metricsHistory || [];
    $('overview-perf-summary').textContent = `Peak Mem: ${res.summary.peakMemoryUtilization}% · Disk Util: ${res.summary.diskUtilization}%`;
    state.charts.overview = new Chart(ctx, {
      type: 'line',
      data: {
        labels: h.map((x) => 't=' + x.time),
        datasets: [
          { label: 'CPU Utilization %', data: h.map((x) => x.cpuUtilization), borderColor: '#38bdf8', backgroundColor: 'rgba(56,189,248,0.12)', fill: true, tension: 0.25, pointRadius: 1.5 },
          { label: 'Memory Utilization %', data: h.map((x) => x.memoryUtilization), borderColor: '#34d399', backgroundColor: 'rgba(52,211,153,0.10)', fill: true, tension: 0.25, pointRadius: 1.5 },
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

  // ----------------------------------------------------------------- Process Manager
  function renderProcessTable() {
    const tbody = $('process-tbody');
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
      return `<tr>
        <td><b>${j.pid}</b></td>
        <td class="sans" style="font-weight:600;color:${colorForPid(j.pid)}">${esc(j.name)}</td>
        <td>${j.arrival}</td>
        <td>${j.cpuBurst}</td>
        <td>${j.priority}</td>
        <td>${j.memory} KB</td>
        <td style="font-size:11px;color:var(--text-secondary);">${esc(j.burstSpec || '')}</td>
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
    }).join('') || '<tr><td colspan="13" style="text-align:center;color:var(--text-muted);padding:18px;">No processes match this filter.</td></tr>';

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
    $('job-form-title').textContent = 'Create New Process';
    $('job-submit-btn').textContent = '+ Add Job';
    $('job-cancel-edit').style.display = 'none';
  }

  $('job-cancel-edit').addEventListener('click', resetJobForm);
  $('job-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const editPid = parseInt($('job-edit-pid').value, 10);
    const nextPid = state.jobs.reduce((m, x) => Math.max(m, x.pid), 0) + 1;
    const ioList = $('job-io').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => !isNaN(n));
    const resList = $('job-res').value.split(/[\s,;]+/).map((x) => x.trim()).filter(Boolean);
    const spec = $('job-spec').value.trim();

    const job = {
      pid: editPid || nextPid,
      name: $('job-name').value.trim() || `job-${editPid || nextPid}`,
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
      toast(`Updated PID ${editPid}`);
    } else {
      state.jobs.push(job);
      toast(`Added ${job.name} (PID ${job.pid})`);
    }
    state.simResult = null;
    resetJobForm();
    renderProcessTable();
  });

  $('state-filters').querySelectorAll('.pill').forEach((p) => {
    p.addEventListener('click', () => {
      $('state-filters').querySelectorAll('.pill').forEach((x) => x.classList.remove('active'));
      p.classList.add('active');
      state.filterState = p.dataset.state;
      renderProcessTable();
    });
  });

  $('btn-clear-jobs').addEventListener('click', () => {
    state.jobs = [];
    state.simResult = null;
    renderProcessTable();
    toast('Cleared all jobs.');
  });

  $('btn-batch-add').addEventListener('click', async () => {
    try {
      const gen = await api('/api/generate', { profile: 'mixed', count: 5, seed: Math.floor(Math.random() * 10000) });
      let nextPid = state.jobs.reduce((m, x) => Math.max(m, x.pid), 0) + 1;
      gen.forEach((g) => { g.pid = nextPid++; state.jobs.push(g); });
      state.simResult = null;
      renderProcessTable();
      toast('Appended 5 generated jobs.');
    } catch (e) { toast(e.message, true); }
  });

  // CSV Import & Export
  $('btn-csv-export').addEventListener('click', () => {
    const header = 'pid,name,arrival,cpuBurst,priority,memory,burstSpec';
    const lines = state.jobs.map((j) => `${j.pid},"${j.name}",${j.arrival},${j.cpuBurst},${j.priority},${j.memory},"${j.burstSpec || ''}"`);
    downloadFile('workload.csv', [header, ...lines].join('\n'), 'text/csv');
  });

  $('csv-import-input').addEventListener('change', (e) => {
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
        toast(`Imported ${parsed.length} jobs from CSV.`);
      } else toast('No valid rows found in CSV.', true);
    };
    reader.readAsText(file);
    e.target.value = '';
  });

  // ----------------------------------------------------------------- CPU Lab
  async function runCpuLab() {
    if (!state.jobs.length) return toast('Add jobs first.', true);
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
      <div style="margin-bottom:10px;display:flex;gap:14px;flex-wrap:wrap;font-size:12.5px;">
        <span>Running: <b style="color:var(--emerald)">${st.runningPid >= 0 ? esc(st.runningName) + ' (rem=' + st.runningRemaining + ')' : 'IDLE'}</b></span>
        ${res.quantum ? `<span>Quantum: <b style="color:var(--amber)">${res.quantum}</b></span>` : ''}
        <span>Context Switches: <b>${st.contextSwitches}</b></span>
      </div>
      <div style="font-size:11px;color:var(--text-secondary);text-transform:uppercase;margin-bottom:4px;">Ready Queue (FIFO order)</div>
      <div class="queue-chips">${st.readyQueue.length ? st.readyQueue.map((q) => `<span class="q-chip">${esc(q.name)} (rem:${q.remaining}, p:${q.priority})</span>`).join('') : '<span style="color:var(--text-muted);font-size:12px;">Empty</span>'}</div>
      <div style="font-size:11px;color:var(--text-secondary);text-transform:uppercase;margin:12px 0 4px;">Decision Events at t=${st.time}</div>
      <ul style="padding-left:18px;font-size:12.5px;color:var(--text-primary);">${st.events.map((e) => `<li>${esc(e)}</li>`).join('')}</ul>`;
  }

  $('btn-cpu-run').addEventListener('click', runCpuLab);
  $('btn-walk-prev').addEventListener('click', () => { if (state.walkIdx > 0) { state.walkIdx--; renderWalkthrough(); } });
  $('btn-walk-next').addEventListener('click', () => { if (state.cpuResult && state.walkIdx < state.cpuResult.steps.length - 1) { state.walkIdx++; renderWalkthrough(); } });

  // ----------------------------------------------------------------- Memory Lab
  $('mem-mode').addEventListener('change', () => {
    $('mem-parts-field').style.display = $('mem-mode').value === 'FIXED' ? 'flex' : 'none';
  });

  async function runMemoryLab() {
    if (!state.jobs.length) return toast('Add jobs first.', true);
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

  $('btn-mem-run').addEventListener('click', runMemoryLab);
  $('btn-mem-prev').addEventListener('click', () => { if (state.memStep > 0) { state.memStep--; renderMemStep(); } });
  $('btn-mem-next').addEventListener('click', () => { if (state.memResult && state.memStep < state.memResult.steps.length - 1) { state.memStep++; renderMemStep(); } });

  // ----------------------------------------------------------------- Disk Lab & Animation
  $('btn-disk-from-jobs').addEventListener('click', () => {
    const reqs = [];
    state.jobs.forEach((j) => (j.ioRequests || []).forEach((c) => reqs.push(c)));
    if (!reqs.length) return toast('Current workload has no disk I/O requests.', true);
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
        <div class="kpi-card emerald"><div class="kpi-label">Average Seek</div><div class="kpi-value">${res.averageSeek}</div><div class="kpi-sub">per request</div></div>
        <div class="kpi-card"><div class="kpi-label">Requests Served</div><div class="kpi-value">${res.requestCount}</div><div class="kpi-sub">Final head @ ${res.finalHead}</div></div>`;
      $('disk-order-strip').textContent = `Service Order: ${res.initialHead} → ${res.path.slice(1).join(' → ')}`;
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

  $('btn-disk-run').addEventListener('click', runDiskLab);
  $('btn-disk-play').addEventListener('click', playDiskAnim);
  $('btn-disk-step').addEventListener('click', () => {
    clearInterval(state.diskTimer);
    if (!state.diskResult) return;
    state.diskAnimStep = (state.diskAnimStep + 1) % state.diskResult.path.length;
    drawDiskCanvas(state.diskAnimStep);
  });

  // ----------------------------------------------------------------- Synchronization Lab
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
        <span class="badge ${ok ? 'badge-RUNNING' : 'badge-FAILED'}">${ok ? '✓ DATA CONSISTENT' : '✗ RACE CONDITION DETECTED'}</span>
      </div>
      <div class="kpi-grid" style="margin-bottom:10px;">
        <div class="kpi-card"><div class="kpi-label">Expected</div><div class="kpi-value">${r.expected}</div></div>
        <div class="kpi-card ${ok ? 'emerald' : 'rose'}"><div class="kpi-label">Actual</div><div class="kpi-value">${r.actual}</div></div>
        <div class="kpi-card ${r.lostUpdates > 0 ? 'rose' : 'emerald'}"><div class="kpi-label">Lost Updates</div><div class="kpi-value">${r.lostUpdates ?? 0}</div></div>
        <div class="kpi-card cyan"><div class="kpi-label">Elapsed</div><div class="kpi-value">${r.elapsedMs} ms</div><div class="kpi-sub">${r.threads} real OS threads</div></div>
        ${r.maxConcurrentInCriticalSection !== undefined ? `<div class="kpi-card violet"><div class="kpi-label">Max In CS</div><div class="kpi-value">${r.maxConcurrentInCriticalSection}</div><div class="kpi-sub">${r.contentions} contentions</div></div>` : ''}
        ${r.maxConcurrent !== undefined ? `<div class="kpi-card violet"><div class="kpi-label">Max Concurrent</div><div class="kpi-value">${r.maxConcurrent}/${r.permits}</div><div class="kpi-sub">${r.blockedWaits} blocked</div></div>` : ''}
      </div>
      <p style="font-size:12.5px;color:var(--text-secondary);">${esc(r.explanation)}</p>
      <div style="font-size:11px;color:var(--text-muted);margin-top:6px;">Backend: ${esc(r.backend)}</div>
    </div>`;
  }

  function renderSyncTrace(r) {
    $('sync-trace-meta').textContent = `Showing ${r.trace.length} of ${r.traceTotalEvents} captured real thread events`;
    $('sync-trace-log').innerHTML = r.trace.map((e) => `
      <div class="event-item">
        <span class="event-t">${e.tUs}µs</span>
        <span style="min-width:60px;color:${colorForPid(e.thread + 1)};font-weight:700;">Thread ${e.thread}</span>
        <span style="min-width:95px;color:var(--amber);font-weight:700;">${esc(e.action)}</span>
        <span>val=${e.value}${e.active ? ` · active=${e.active}` : ''}</span>
      </div>`).join('');
  }

  $('btn-sync-run').addEventListener('click', async () => {
    try {
      const r = await api('/api/sync', syncPayload());
      $('sync-results-area').innerHTML = renderSyncCard(r);
      renderSyncTrace(r);
    } catch (e) { toast(e.message, true); }
  });

  $('btn-sync-compare').addEventListener('click', async () => {
    try {
      const [race, mtx] = await Promise.all([
        api('/api/sync', syncPayload('race')),
        api('/api/sync', syncPayload('mutex')),
      ]);
      $('sync-results-area').innerHTML = `<div class="grid-2">${renderSyncCard(race)}${renderSyncCard(mtx)}</div>`;
      renderSyncTrace(race);
    } catch (e) { toast(e.message, true); }
  });

  // ----------------------------------------------------------------- Comparison Lab
  document.querySelectorAll('[data-cmp]').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('[data-cmp]').forEach((x) => x.classList.remove('active'));
      b.classList.add('active');
      state.cmpMode = b.dataset.cmp;
      runComparison();
    });
  });
  $('btn-compare-run').addEventListener('click', runComparison);

  async function runComparison() {
    state.cmpLoaded = true;
    try {
      if (state.cmpMode === 'cpu') {
        if (!state.jobs.length) return toast('Add jobs first.', true);
        const res = await api('/api/cpu/compare', { processes: state.jobs, quantum: parseInt($('cpu-quantum').value, 10) || 2, priorityOrder: $('cpu-prio-order').value });
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
        $('cmp-analysis-box').innerHTML = `<b>Key Tradeoffs:</b> SRTF/SJF typically minimize average waiting time, while Round Robin improves response time at the cost of extra context switches. Priority scheduling favors critical jobs but risks starvation without aging.`;
      } else if (state.cmpMode === 'memory') {
        if (!state.jobs.length) return toast('Add jobs first.', true);
        const memJobs = state.jobs.map((j) => ({ pid: j.pid, name: j.name, size: Math.max(1, j.memory || 64), arrival: j.arrival, duration: j.cpuBurst }));
        const parts = $('mem-partitions').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => n > 0);
        const res = await api('/api/memory/compare', { total: parseInt($('mem-total').value, 10) || 512, mode: $('mem-mode').value, partitions: parts, jobs: memJobs });
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
        $('cmp-analysis-box').innerHTML = `<b>Key Tradeoffs:</b> First Fit is fast and balanced; Best Fit minimizes immediate waste but can leave tiny slivers; Worst Fit preserves larger leftover holes; Next Fit spreads allocations evenly across the address space.`;
      } else {
        const reqs = $('disk-reqs').value.split(/[\s,;]+/).map((x) => parseInt(x, 10)).filter((n) => !isNaN(n));
        const res = await api('/api/disk/compare', { cylinders: parseInt($('disk-cyls').value, 10) || 200, head: parseInt($('disk-head').value, 10) || 53, direction: $('disk-dir').value, requests: reqs });
        renderCompareTable(res, 'algorithm', [
          ['algorithmName', 'Algorithm'], ['totalMovement', 'Total Head Movement'], ['averageSeek', 'Average Seek Distance'], ['requestCount', 'Requests']
        ]);
        renderCompareCharts(res.rows.map((r) => r.algorithm),
          'Total Head Movement in Cylinders (lower is better)',
          [{ label: 'Total Movement', data: res.rows.map((r) => r.totalMovement), backgroundColor: '#38bdf8' }],
          'Average Seek Distance (lower is better)',
          [{ label: 'Avg Seek', data: res.rows.map((r) => r.averageSeek), backgroundColor: '#a78bfa' }]);
        $('cmp-analysis-box').innerHTML = `<b>Key Tradeoffs:</b> SSTF often achieves the shortest total movement but risks starving outer cylinders; SCAN/LOOK prevent starvation; C-SCAN/C-LOOK give the most uniform response times.`;
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

  // ----------------------------------------------------------------- Global Controls
  $('btn-sim-run').addEventListener('click', () => runIntegratedSim(true));
  $('btn-sim-pause').addEventListener('click', () => {
    if (!state.simResult) return;
    if (state.playing) stopPlayback();
    else startPlayback();
  });
  $('btn-sim-step').addEventListener('click', async () => {
    if (!state.simResult) {
      await runIntegratedSim(false);
      return;
    }
    stopPlayback();
    if (state.tick < state.simResult.timeline.length - 1) renderTick(state.tick + 1);
  });
  $('btn-sim-reset').addEventListener('click', () => {
    stopPlayback();
    if (state.simResult) renderTick(0);
  });
  $('sim-scrubber').addEventListener('input', (e) => {
    stopPlayback();
    renderTick(parseInt(e.target.value, 10) || 0);
  });
  $('sim-speed').addEventListener('change', () => { if (state.playing) startPlayback(); });

  $('sample-select').addEventListener('change', (e) => {
    const s = state.samples.find((x) => x.id === e.target.value);
    if (!s) return;
    loadSample(s);
    e.target.value = '';
  });

  function loadSample(s) {
    state.jobs = JSON.parse(JSON.stringify(s.processes));
    const rc = s.recommendedConfig || {};
    if (rc.cpuAlgorithm) { $('cfg-cpu').value = rc.cpuAlgorithm; $('cpu-algo').value = rc.cpuAlgorithm; }
    if (rc.quantum) { $('cfg-quantum').value = rc.quantum; $('cpu-quantum').value = rc.quantum; }
    if (rc.memoryTotal) { $('cfg-mem-total').value = rc.memoryTotal; $('mem-total').value = rc.memoryTotal; }
    if (rc.memoryStrategy) { $('cfg-mem-strategy').value = rc.memoryStrategy; $('mem-strategy').value = rc.memoryStrategy; }
    if (rc.diskAlgorithm) { $('cfg-disk-algo').value = rc.diskAlgorithm; $('disk-algo').value = rc.diskAlgorithm; }
    if (rc.diskHead !== undefined) $('disk-head').value = rc.diskHead;
    if (rc.diskCylinders) $('disk-cyls').value = rc.diskCylinders;
    state.simResult = null;
    state.cpuResult = null;
    state.memResult = null;
    renderProcessTable();
    toast(`Loaded sample: ${s.name}`);
    runIntegratedSim(false);
  }

  $('btn-random-workload').addEventListener('click', async () => {
    try {
      const gen = await api('/api/generate', { profile: 'mixed', count: 7, seed: Math.floor(Math.random() * 100000) });
      state.jobs = gen;
      state.simResult = null;
      renderProcessTable();
      toast('Generated 7 random data center jobs via C++ engine.');
      runIntegratedSim(false);
    } catch (e) { toast(e.message, true); }
  });

  $('btn-export-report').addEventListener('click', () => {
    if (!state.simResult) return toast('Run a simulation first to export its report.', true);
    downloadFile('smart_datacenter_os_report.json', JSON.stringify(state.simResult, null, 2), 'application/json');
  });

  function downloadFile(name, content, type) {
    const blob = new Blob([content], { type });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  // ----------------------------------------------------------------- Boot
  async function init() {
    try {
      const info = await api('/api/info');
      $('engine-status').innerHTML = '<span class="pulse"></span>C++ Engine Online';
      $('engine-backend').textContent = info.threadingBackend;
      state.samples = await api('/api/samples');
      const sel = $('sample-select');
      state.samples.forEach((s) => {
        const opt = document.createElement('option');
        opt.value = s.id;
        opt.textContent = s.name;
        sel.appendChild(opt);
      });
      // Load default Mixed Data Center workload and run initial telemetry snapshot
      const def = state.samples.find((s) => s.id === 'mixed') || state.samples[0];
      if (def) loadSample(def);
    } catch (e) {
      $('engine-status').className = 'engine-pill offline';
      $('engine-status').innerHTML = '<span class="pulse"></span>Engine Offline';
      $('engine-backend').textContent = e.message;
      toast('Failed to connect to C++ engine: ' + e.message, true);
    }
  }

  init();
})();
