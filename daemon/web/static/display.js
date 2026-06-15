const SLOT_LABELS = ['Линия 1 (верх)', 'Линия 2 (низ)'];
const FONT_LABELS = { small: 'Мелкий', medium: 'Средний', large: 'Крупный' };
let widgets = [];
let fonts = ['small', 'medium', 'large'];
let previewTimer = null;

function lineCount() {
  return Number(document.getElementById('lineCount').value);
}

function fontClass(name) {
  return name || 'medium';
}

function renderForm(layout) {
  const form = document.getElementById('layoutForm');
  const count = lineCount();
  form.innerHTML = '';

  if (count === 0) {
    form.innerHTML = '<p class="hint">Текст не показывается — только яркость и sleep-режим.</p>';
    schedulePreview();
    return;
  }

  for (let slot = 0; slot < count; slot += 1) {
    const current = layout.find((item) => item.slot === slot) || {
      widget: slot === 0 ? 'clock' : 'session',
      font: slot === 0 ? 'large' : 'medium',
    };
    const row = document.createElement('div');
    row.className = 'layout-row layout-row-3';
    row.innerHTML = `
      <label>${SLOT_LABELS[slot]}</label>
      <select id="widget-${slot}" data-slot="${slot}"></select>
      <select id="font-${slot}" data-slot="${slot}" title="Размер шрифта"></select>
    `;
    form.appendChild(row);
    const wsel = row.querySelector(`#widget-${slot}`);
    const fsel = row.querySelector(`#font-${slot}`);
    widgets.forEach((widget) => {
      const option = document.createElement('option');
      option.value = widget;
      option.textContent = widget;
      if (widget === current.widget) option.selected = true;
      wsel.appendChild(option);
    });
    fonts.forEach((font) => {
      const option = document.createElement('option');
      option.value = font;
      option.textContent = FONT_LABELS[font] || font;
      if (font === (current.font || 'medium')) option.selected = true;
      fsel.appendChild(option);
    });
    wsel.addEventListener('change', schedulePreview);
    fsel.addEventListener('change', schedulePreview);
  }
  schedulePreview();
}

function currentLayout() {
  const count = lineCount();
  if (count === 0) return [];
  return Array.from({ length: count }, (_, slot) => ({
    slot,
    widget: document.getElementById(`widget-${slot}`).value,
    font: document.getElementById(`font-${slot}`).value,
  }));
}

function applyPreview(data) {
  const count = data.line_count ?? lineCount();
  const empty = document.getElementById('oledEmpty');
  const lineEls = [document.getElementById('line0'), document.getElementById('line1')];

  if (count === 0) {
    lineEls.forEach((el) => {
      el.textContent = '';
      el.hidden = true;
    });
    empty.hidden = false;
  } else {
    empty.hidden = true;
    lineEls.forEach((el, idx) => {
      if (idx >= count) {
        el.textContent = '';
        el.hidden = true;
        return;
      }
      el.hidden = false;
      const line = data.widgets.find((w) => w.pos === idx);
      if (!line) {
        el.textContent = '';
        el.className = 'oled-line';
        return;
      }
      el.textContent = line.text || '';
      el.className = `oled-line ${fontClass(line.font)}`;
    });
  }

  const oled = document.getElementById('oledPreview');
  const opacity = (data.brightness || 255) / 255;
  oled.style.opacity = String(0.35 + opacity * 0.65);
}

async function refreshPreview() {
  const res = await fetch('/api/display/preview', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ layout: currentLayout(), line_count: lineCount() }),
  });
  const data = await res.json();
  applyPreview(data);
}

function schedulePreview() {
  clearTimeout(previewTimer);
  previewTimer = setTimeout(refreshPreview, 200);
}

async function loadLayout() {
  const res = await fetch('/api/display/layout');
  const data = await res.json();
  widgets = data.widgets;
  fonts = data.fonts || fonts;
  document.getElementById('brightness').value = data.brightness;
  document.getElementById('brightnessVal').textContent = data.brightness;
  document.getElementById('sleepDisplay').value = data.sleep_display_mode || 'off';
  document.getElementById('lineCount').value = String(data.line_count ?? 2);

  const defaults = [
    { slot: 0, widget: 'clock', font: 'large' },
    { slot: 1, widget: 'session', font: 'medium' },
  ];
  const layout = [0, 1].map((slot) => data.layout.find((i) => i.slot === slot) || defaults[slot]);
  renderForm(layout);
}

document.getElementById('lineCount').addEventListener('change', () => {
  const defaults = [
    { slot: 0, widget: 'clock', font: 'large' },
    { slot: 1, widget: 'session', font: 'medium' },
  ];
  renderForm(defaults);
});

document.getElementById('brightness').addEventListener('input', (e) => {
  document.getElementById('brightnessVal').textContent = e.target.value;
  schedulePreview();
});

document.getElementById('saveBtn').addEventListener('click', async () => {
  const body = {
    layout: currentLayout(),
    line_count: lineCount(),
    brightness: Number(document.getElementById('brightness').value),
    sleep_display_mode: document.getElementById('sleepDisplay').value,
  };
  const res = await fetch('/api/display/layout', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  const data = await res.json();
  document.getElementById('saveStatus').textContent = data.ok ? 'Saved — sent to device' : 'Save failed';
});

loadLayout();
setInterval(refreshPreview, 3000);
