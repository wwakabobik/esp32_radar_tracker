const CHART_HINTS = {
  week_total: 'Total desk hours per day (work sessions).',
  week_timeline: 'Blue bars = radar presence; empty = absent or no data.',
  sleep: 'Sleep phases (calm / restless / awake) and quality score. Movements stored with timestamps.',
  ai_timeline: 'Confidence on-device AI state changes over the last 24 hours.',
};

const SLEEP_DEEP_COLOR = '#1e3a8a';
const SLEEP_LIGHT_COLOR = '#6366f1';
const SLEEP_AWAKE_COLOR = '#f87171';
const PRESENCE_COLOR = '#3b82f6';
const ABSENT_COLOR = 'rgba(148, 163, 184, 0.35)';
const SLEEP_QUALITY_COLOR = '#34d399';

function tsToHour(ts, dayStart) {
  return (ts - dayStart) / 3600000;
}

function hourLabel(h) {
  const hh = Math.floor(h);
  const mm = Math.round((h - hh) * 60);
  return `${String(hh).padStart(2, '0')}:${String(mm).padStart(2, '0')}`;
}

function destroyChart() {
  if (window.weekChart) {
    window.weekChart.destroy();
    window.weekChart = null;
  }
}

function setChartHint(type) {
  const el = document.getElementById('chart-hint');
  if (el) el.textContent = CHART_HINTS[type] || '';
}

function qualityColor(q) {
  if (q >= 75) return '#34d399';
  if (q >= 50) return '#fbbf24';
  return '#f87171';
}

function renderWeekTotal(data) {
  const labels = data.days.map((d) => d.date.slice(5));
  const hours = data.days.map((d) => +(d.seconds / 3600).toFixed(2));
  const ctx = document.getElementById('week-chart');
  window.weekChart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels,
      datasets: [{ label: 'Desk hours', data: hours, backgroundColor: PRESENCE_COLOR }],
    },
    options: {
      scales: { y: { beginAtZero: true, title: { display: true, text: 'hours' } } },
      plugins: { legend: { display: false } },
    },
  });
}

function renderWeekTimeline(data) {
  const labels = data.days.map((d) => d.date.slice(5));
  const presentData = [];
  const absentData = [];

  data.days.forEach((day, idx) => {
    const dayStart = day.day_start * 1000;
    day.intervals.forEach((iv) => {
      const startH = tsToHour(iv.start * 1000, dayStart);
      const endH = tsToHour(iv.end * 1000, dayStart);
      if (endH <= startH) return;
      const point = { x: [startH, endH], y: labels[idx] };
      if (iv.present) presentData.push(point);
      else absentData.push(point);
    });
  });

  const ctx = document.getElementById('week-chart');
  window.weekChart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels,
      datasets: [
        {
          label: 'Absent',
          data: absentData,
          backgroundColor: ABSENT_COLOR,
          borderSkipped: false,
          barPercentage: 0.85,
        },
        {
          label: 'Present',
          data: presentData,
          backgroundColor: PRESENCE_COLOR,
          borderSkipped: false,
          barPercentage: 0.85,
        },
      ],
    },
    options: {
      indexAxis: 'y',
      parsing: { xAxisKey: 'x', yAxisKey: 'y' },
      scales: {
        x: {
          type: 'linear',
          min: 0,
          max: 24,
          ticks: {
            stepSize: 3,
            callback: (v) => `${v}:00`,
          },
          title: { display: true, text: 'time of day' },
        },
        y: { reverse: false },
      },
      plugins: {
        legend: { position: 'bottom' },
        tooltip: {
          callbacks: {
            label(ctx) {
              const [a, b] = ctx.raw.x;
              return `${ctx.dataset.label}: ${hourLabel(a)} – ${hourLabel(b)}`;
            },
          },
        },
      },
    },
  });
}

function renderSleepChart(data) {
  const labels = data.nights.map((n) => n.date.slice(5));
  const deepHours = data.nights.map((n) => n.phases?.deep_hours ?? 0);
  const lightHours = data.nights.map((n) => n.phases?.light_hours ?? 0);
  const awakeHours = data.nights.map((n) => n.phases?.awake_hours ?? 0);
  const qualities = data.nights.map((n) => n.quality);

  const ctx = document.getElementById('week-chart');
  window.weekChart = new Chart(ctx, {
    type: 'bar',
    data: {
      labels,
      datasets: [
        {
          label: 'Calm (est.)',
          data: deepHours,
          backgroundColor: SLEEP_DEEP_COLOR,
          stack: 'sleep',
          yAxisID: 'y',
        },
        {
          label: 'Restless (est.)',
          data: lightHours,
          backgroundColor: SLEEP_LIGHT_COLOR,
          stack: 'sleep',
          yAxisID: 'y',
        },
        {
          label: 'Awake',
          data: awakeHours,
          backgroundColor: SLEEP_AWAKE_COLOR,
          stack: 'sleep',
          yAxisID: 'y',
        },
        {
          type: 'line',
          label: 'Quality %',
          data: qualities,
          borderColor: SLEEP_QUALITY_COLOR,
          backgroundColor: SLEEP_QUALITY_COLOR,
          tension: 0.3,
          yAxisID: 'y1',
          pointRadius: 4,
        },
      ],
    },
    options: {
      scales: {
        y: {
          beginAtZero: true,
          stacked: true,
          position: 'left',
          title: { display: true, text: 'hours' },
        },
        y1: {
          beginAtZero: true,
          max: 100,
          position: 'right',
          grid: { drawOnChartArea: false },
          title: { display: true, text: 'quality' },
        },
      },
      plugins: {
        legend: { position: 'bottom' },
        tooltip: {
          callbacks: {
            afterBody(items) {
              const i = items[0]?.dataIndex;
              if (i == null) return [];
              const n = data.nights[i];
              const p = n.phases || {};
              return [
                `Movements: ${n.movements}`,
                `Calm: ${p.deep_pct ?? 0}%`,
                `Restless: ${p.light_pct ?? 0}%`,
                `Awake: ${p.awake_pct ?? 0}%`,
                p.breath_rate_bpm != null ? `Breath rate: ${p.breath_rate_bpm} bpm` : 'Breath rate: n/a',
                'Estimated from radar motion energy',
              ];
            },
          },
        },
      },
    },
  });
}

function renderAiTimeline(data) {
  const entries = data.entries || [];
  const labels = entries.map((e) => {
    const d = new Date(e.ts * 1000);
    return d.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  });
  const conf = entries.map((e) => e.confidence ?? 0);
  const ctx = document.getElementById('week-chart');
  window.weekChart = new Chart(ctx, {
    type: 'line',
    data: {
      labels,
      datasets: [{
        label: 'AI confidence %',
        data: conf,
        borderColor: '#8b5cf6',
        tension: 0.2,
        pointRadius: 0,
      }],
    },
    options: {
      scales: { y: { min: 0, max: 100 } },
      plugins: {
        tooltip: {
          callbacks: {
            afterLabel(ctx) {
              const e = entries[ctx.dataIndex];
              return e ? `${e.state} (${e.mode})` : '';
            },
          },
        },
      },
    },
  });
}

async function fetchChart() {
  const type = document.getElementById('chart-type')?.value || 'week_total';
  setChartHint(type);
  destroyChart();

  if (type === 'week_total') {
    const res = await fetch('/api/dashboard/week');
    renderWeekTotal(await res.json());
    return;
  }
  if (type === 'week_timeline') {
    const res = await fetch('/api/dashboard/presence/week');
    renderWeekTimeline(await res.json());
    return;
  }
  if (type === 'sleep') {
    const res = await fetch('/api/dashboard/sleep/week');
    renderSleepChart(await res.json());
    return;
  }
  if (type === 'ai_timeline') {
    const res = await fetch('/api/ai/timeline?hours=24');
    renderAiTimeline(await res.json());
  }
}

async function fetchToday() {
  const res = await fetch('/api/dashboard/today');
  const data = await res.json();
  document.getElementById('mode').textContent = `Mode: ${data.mode}`;
  document.getElementById('online').textContent = `Online: ${data.online ? 'yes' : 'no'}`;
  document.getElementById('present').textContent = `Present: ${data.present ? 'yes' : 'no'}${data.fatigue ? ' (fatigue)' : ''}`;
  document.getElementById('aiState').textContent = `AI: ${data.ai_state || '—'}${data.ai_confidence != null ? ` (${data.ai_confidence}%)` : ''}`;
  document.getElementById('fatigueCount').textContent = `Fatigue hints today: ${data.fatigue_events_today ?? 0}`;
  const hours = Math.floor(data.today_seconds / 3600);
  const minutes = Math.floor((data.today_seconds % 3600) / 60);
  document.getElementById('today').textContent = `${hours}h ${String(minutes).padStart(2, '0')}m`;
  const sSec = data.session_seconds;
  const sMin = Math.floor(sSec / 60);
  const sRem = sSec % 60;
  document.getElementById('session').textContent =
    sMin > 0 ? `Session: ${sMin}m` : `Session: ${sRem}s`;
}

document.getElementById('chart-type')?.addEventListener('change', fetchChart);

document.getElementById('ota-btn')?.addEventListener('click', async () => {
  const el = document.getElementById('ota-status');
  el.textContent = 'Triggering OTA...';
  const res = await fetch('/api/dashboard/ota', { method: 'POST' });
  const data = await res.json();
  el.textContent = res.ok ? `OTA sent: ${data.url}` : 'OTA failed';
});

fetchToday();
fetchChart();
loadGestureSummary('gestureSummary');
setInterval(fetchToday, 5000);
