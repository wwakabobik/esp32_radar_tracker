let sinceSeq = 0;
const rows = document.getElementById('logRows');
const allEntries = [];

function fmtTime(ts) {
  if (!ts) return '—';
  const d = new Date(ts * 1000);
  return d.toLocaleTimeString('ru-RU', { hour12: false, fractionalSecondDigits: 3 });
}

function summarize(entry) {
  const p = entry.payload || {};
  if (entry.kind === 'radar') {
    return [
      `pres=${p.presence ? 1 : 0}`,
      `gest=${p.gesture_dist ?? p.dist ?? '—'}`,
      `body=${p.presence_dist ?? '—'}`,
      `g=${p.gate_dist ?? '—'}`,
      `m=${p.m_dist ?? '—'}`,
      `s=${p.s_dist ?? '—'}`,
      `mv=${p.moving ? 1 : 0}`,
    ].join(' ');
  }
  if (entry.kind === 'gesture_debug') {
    return [
      `zone=${p.in_zone ? 1 : 0}`,
      `d=${p.dist ?? '—'}`,
      `hold=${p.hold_left_ms ?? '—'}`,
    ].join(' ');
  }
  if (entry.kind === 'gesture') {
    return `next dist=${p.value ?? 0} ts=${p.ts ?? '—'}`;
  }
  return JSON.stringify(p);
}

function appendEntry(entry) {
  allEntries.push(entry);
  const tr = document.createElement('tr');
  tr.innerHTML = `
    <td>${entry.seq}</td>
    <td>${fmtTime(entry.hub_ts)}</td>
    <td>${fmtTime(entry.device_ts ?? entry.payload?.ts)}</td>
    <td class="kind-${entry.kind}">${entry.kind}</td>
    <td>${entry.mode}</td>
    <td>${summarize(entry)}</td>`;
  tr.title = JSON.stringify(entry.payload, null, 2);
  rows.appendChild(tr);
}

async function poll() {
  const kind = document.getElementById('kindFilter').value;
  const q = new URLSearchParams({ since: String(sinceSeq), limit: '200' });
  if (kind) q.set('kind', kind);
  try {
    const res = await fetch(`/api/sensor/log?${q}`);
    const data = await res.json();
    sinceSeq = data.last_seq;
    for (const e of data.entries) {
      appendEntry(e);
    }
    while (rows.children.length > 800) {
      rows.removeChild(rows.firstChild);
      allEntries.shift();
    }
    document.getElementById('logStatus').textContent = `entries: ${rows.children.length}, seq: ${sinceSeq}`;
    if (document.getElementById('autoScroll').checked) {
      document.getElementById('logBody').scrollTop = document.getElementById('logBody').scrollHeight;
    }
  } catch (e) {
    document.getElementById('logStatus').textContent = 'offline';
  }
}

document.getElementById('clearLog').addEventListener('click', async () => {
  await fetch('/api/sensor/log/clear', { method: 'POST' });
  sinceSeq = 0;
  rows.innerHTML = '';
  allEntries.length = 0;
});

document.getElementById('exportLog').addEventListener('click', async () => {
  const text = JSON.stringify(allEntries, null, 2);
  await navigator.clipboard.writeText(text);
  document.getElementById('logStatus').textContent = `copied ${allEntries.length} entries`;
});

document.getElementById('kindFilter').addEventListener('change', () => {
  sinceSeq = 0;
  rows.innerHTML = '';
  allEntries.length = 0;
});

setInterval(poll, 250);
poll();
