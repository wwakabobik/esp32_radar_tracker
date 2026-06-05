const SLOT_LABELS = ['Line 1 (top)', 'Line 2 (bottom)'];
let widgets = [];

function renderForm(layout) {
  const form = document.getElementById('layoutForm');
  form.innerHTML = '';
  for (let slot = 0; slot < 2; slot += 1) {
    const current = layout.find((item) => item.slot === slot)?.widget || 'clock';
    const row = document.createElement('div');
    row.className = 'layout-row';
    row.innerHTML = `
      <label for="slot-${slot}">${SLOT_LABELS[slot]}</label>
      <select id="slot-${slot}" data-slot="${slot}"></select>
    `;
    form.appendChild(row);
    const select = row.querySelector('select');
    widgets.forEach((widget) => {
      const option = document.createElement('option');
      option.value = widget;
      option.textContent = widget;
      if (widget === current) option.selected = true;
      select.appendChild(option);
    });
    select.addEventListener('change', updatePreview);
  }
  updatePreview();
}

function currentLayout() {
  return SLOT_LABELS.map((_, slot) => ({
    slot,
    widget: document.getElementById(`slot-${slot}`).value,
  }));
}

function updatePreview() {
  const layout = currentLayout();
  document.getElementById('line0').textContent = layout[0].widget;
  document.getElementById('line1').textContent = layout[1].widget;
}

async function loadLayout() {
  const res = await fetch('/api/display/layout');
  const data = await res.json();
  widgets = data.widgets;
  renderForm(data.layout);
}

document.getElementById('saveBtn').addEventListener('click', async () => {
  const res = await fetch('/api/display/layout', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ layout: currentLayout() }),
  });
  const data = await res.json();
  document.getElementById('saveStatus').textContent = data.ok ? 'Saved' : 'Save failed';
});

loadLayout();
