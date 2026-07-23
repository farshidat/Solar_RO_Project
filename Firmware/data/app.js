// لایه داده مرکزی: همه صفحات از همین state می‌خونن. بعداً اگر منبع داده عوض شد
// (مثلاً بخشی از پارامترها هم از MQTT بیاد) فقط همین‌جا تغییر می‌کنه.
const state = {
  tds: {
    inlet:  { ec: 0, temp: 0, tds: 0 },
    outlet: { ec: 0, temp: 0, tds: 0 },
  },
  pumps: { treatment: false, uv: false, raw: false },
  hasData: false,
  pumpsKnown: false,
};

function zonesFromStops(min, stops) {
  const zones = [];
  let from = min;
  for (const [to, color] of stops) {
    zones.push({ from, to, color: `var(--zone-${color})` });
    from = to;
  }
  return zones;
}

const RANGES = {
  tdsInlet:  { min: 100, max: 3000, zones: zonesFromStops(100, [[1000, 'green'], [2000, 'yellow'], [3000, 'red']]) },
  tdsOutlet: { min: 3, max: 200, zones: zonesFromStops(3, [[100, 'green'], [150, 'yellow'], [200, 'red']]) },
  productTemp: { min: -5, max: 80, zones: zonesFromStops(-5, [[1, 'red'], [5, 'yellow'], [45, 'green'], [50, 'yellow'], [80, 'red']]) },
  saltRejection: { min: 0, max: 100, zones: zonesFromStops(0, [[85, 'red'], [90, 'yellow'], [100, 'green']]) },
  // placeholder - هنوز پرسیده نشده
  rawTankLevel:    { min: 0, max: 100, zones: zonesFromStops(0, [[20, 'red'], [40, 'yellow'], [100, 'green']]) },
  productTankLevel:{ min: 0, max: 100, zones: zonesFromStops(0, [[20, 'red'], [40, 'yellow'], [100, 'green']]) },
  // ظرفیت استفاده‌شده فیلتر: هرچه بیشتر یعنی فرسوده‌تر (بر خلاف "باقی‌مانده"، اینجا زیاد=بد)
  filterUsed: { min: 0, max: 100, zones: zonesFromStops(0, [[60, 'green'], [85, 'yellow'], [100, 'red']]) },
  uvHours:         { min: 0, max: 9000, zones: zonesFromStops(0, [[6000, 'green'], [8000, 'yellow'], [9000, 'red']]) },
  ph:              { min: 0, max: 14, zones: zonesFromStops(0, [[6, 'green'], [9, 'yellow'], [14, 'red']]) },
  ambientTemp:     { min: -10, max: 60, zones: zonesFromStops(-10, [[5, 'yellow'], [45, 'green'], [60, 'yellow']]) },
};

// مقادیر پارامترهایی که هنوز سنسور/منطق واقعی‌شان پیاده نشده (فازهای بعدی).
// یک‌جا نگه‌داشته می‌شوند تا هم صفحات و هم گزارش PDF از همین منبع بخوانند.
const MOCK_VALUES = {
  rawTankLevel: 65,
  productTankLevel: 80,
  filterPre: 18,
  filterMembrane: 43,
  uvHours: 1200,
  ph: 7.2,
  ambientTemp: 22,
  irradiance: 72,
  volumeLiters: 486,
};

function zoneColorForValue(value, zones) {
  for (const z of zones) if (value < z.to || z === zones[zones.length - 1]) return z.color;
}

// نرخ دفع املاح = (1 - TDS_خروجی/TDS_ورودی) × 100
function computeSaltRejection() {
  const { inlet, outlet } = state.tds;
  return inlet.tds > 0 ? Math.round((1 - outlet.tds / inlet.tds) * 100) : 0;
}

const ICONS = {
  home: '<path d="M4 11 12 4l8 7" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M6 10v9h5v-5h2v5h5v-9" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>',
  performance: '<path d="M4 17a8 8 0 0 1 16 0" stroke-width="2" stroke-linecap="round"/><path d="M12 17 16 10" stroke-width="2" stroke-linecap="round"/><circle cx="12" cy="17" r="1.6" stroke-width="2"/>',
  settings: '<path d="M4 7h10M17 7h3M4 12h3M8 12h12M4 17h10M17 17h3" stroke-width="2" stroke-linecap="round"/><circle cx="16" cy="7" r="2" stroke-width="2"/><circle cx="6" cy="12" r="2" stroke-width="2"/><circle cx="14" cy="17" r="2" stroke-width="2"/>',
  alerts: '<path d="M12 3a5 5 0 0 0-5 5v3.2c0 .5-.2 1-.5 1.4L4.8 15H19.2l-1.7-2.4c-.3-.4-.5-.9-.5-1.4V8a5 5 0 0 0-5-5Z" stroke-width="2" stroke-linejoin="round"/><path d="M9.5 18.5a2.5 2.5 0 0 0 5 0" stroke-width="2" stroke-linecap="round"/>'
};
document.querySelectorAll('.nav-icon').forEach(el => {
  el.innerHTML = `<svg viewBox="0 0 24 24">${ICONS[el.dataset.icon]}</svg>`;
});

/* ==================== صفحه خانه ==================== */

// ----- شماتیک فرآیند: پمپ۱ - مخزن خام - فیلتر پیش‌تصفیه - پمپ۲ - ممبران - مخزن شرب -----
// وضعیت پمپ‌ها و دمای بالای دو مخزن واقعی هستند؛ سطح مخازن و ظرفیت فیلترها هنوز
// نمایشی است (فازهای ۳ تا ۸ - سنسورهای سطح و فرمول ظرفیت فیلتر هنوز پیاده نشده‌اند).
let _pumpGradSeq = 0;

/** آیکون پمپ: روشن = پروانه چرخان + حس جریان آب؛ خاموش = ثابت و خاکستری */
function pumpIcon(cx, cy, r, on) {
  const stroke = on ? '#1f8fc4' : '#b0bec5';
  const blade = on ? '#0a6f96' : '#90a4ae';
  const hub = on ? '#065a7a' : '#78909c';
  const swirlStroke = on ? 'rgba(255,255,255,0.45)' : 'transparent';
  const spinClass = on ? 'pump-impeller-spin' : '';
  const swirlClass = on ? 'pump-water-swirl' : '';
  const gradId = on ? `pumpWaterGrad-${++_pumpGradSeq}` : '';
  const bodyFill = on ? `url(#${gradId})` : '#eceff1';

  const blades = [0, 120, 240].map(deg => {
    const rad = (deg * Math.PI) / 180;
    // پره‌ها کمی بزرگ‌تر تا داخل بدنه پمپ واضح‌تر دیده شوند
    const tipX = cx + Math.sin(rad) * (r * 0.78);
    const tipY = cy - Math.cos(rad) * (r * 0.78);
    const a1 = rad - 0.62;
    const a2 = rad + 0.62;
    const b1x = cx + Math.sin(a1) * (r * 0.2);
    const b1y = cy - Math.cos(a1) * (r * 0.2);
    const b2x = cx + Math.sin(a2) * (r * 0.2);
    const b2y = cy - Math.cos(a2) * (r * 0.2);
    return `<path d="M ${b1x.toFixed(2)} ${b1y.toFixed(2)} Q ${tipX.toFixed(2)} ${tipY.toFixed(2)} ${b2x.toFixed(2)} ${b2y.toFixed(2)} Z" fill="${blade}"/>`;
  }).join('');

  return `
    ${on ? `
      <defs>
        <radialGradient id="${gradId}" cx="40%" cy="35%" r="70%">
          <stop offset="0%" stop-color="#b8e4f8"/>
          <stop offset="55%" stop-color="#5bb8e0"/>
          <stop offset="100%" stop-color="#2a9bcf"/>
        </radialGradient>
      </defs>
    ` : ''}
    <circle cx="${cx}" cy="${cy}" r="${r}" fill="${bodyFill}" stroke="${stroke}" stroke-width="2.8"/>
    ${on ? `
      <g class="${swirlClass}" style="transform-origin:${cx}px ${cy}px">
        <circle cx="${cx}" cy="${cy}" r="${r * 0.72}" fill="none" stroke="${swirlStroke}" stroke-width="1.6" stroke-dasharray="6 5"/>
        <circle cx="${cx}" cy="${cy}" r="${r * 0.48}" fill="none" stroke="rgba(255,255,255,0.35)" stroke-width="1.2" stroke-dasharray="4 4"/>
      </g>
    ` : `
      <circle cx="${cx}" cy="${cy}" r="${r * 0.72}" fill="none" stroke="#d5dce0" stroke-width="1.2"/>
    `}
    <g class="${spinClass}" style="transform-origin:${cx}px ${cy}px">
      ${blades}
      <circle cx="${cx}" cy="${cy}" r="${r * 0.16}" fill="${hub}"/>
      <circle cx="${cx}" cy="${cy}" r="${r * 0.06}" fill="${on ? '#e8f7fd' : '#cfd8dc'}"/>
    </g>
  `;
}

function buildSchematic(svgEl, { pump1On, tank1Pct, tank1Temp, preFilterPct, pump2On, membranePct, tank2Pct, tank2Temp }) {
  // مسیر لوله همیشه آبی است؛ فقط وقتی پمپ مربوطه روشن است حرکت (انیمیشن جریان) دارد
  const pipeColor = () => 'var(--pipe-on)';
  const capacityColor = pct => pct < 15 ? 'var(--zone-red)' : pct < 40 ? 'var(--zone-yellow)' : 'var(--zone-green)';

  function tank(x, pct, color, tempC) {
    const w = 46, h = 56, y = 38;
    const fillH = (h - 6) * (pct / 100);
    return `
      <text x="${x + w / 2}" y="10" font-size="9" fill="#8a9aa2" text-anchor="middle">${tempC.toFixed(1)}°C</text>
      <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="6" fill="#f3f7f9" stroke="#c9d6da" stroke-width="2"/>
      <rect x="${x + 3}" y="${y + h - 3 - fillH}" width="${w - 6}" height="${fillH}" rx="3" fill="${color}"/>
      <text x="${x + w / 2}" y="${y + h + 13}" font-size="9" fill="#8a9aa2" text-anchor="middle">${pct}%</text>
    `;
  }
  function filterCapsule(x, pct) {
    const w = 26, h = 56, y = 38;
    const fillH = (h - 6) * (pct / 100);
    const color = capacityColor(pct);
    return `
      <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${w / 2}" fill="#f3f7f9" stroke="#c9d6da" stroke-width="2"/>
      <rect x="${x + 3}" y="${y + h - 3 - fillH}" width="${w - 6}" height="${fillH}" rx="${(w - 6) / 2}" fill="${color}"/>
      <text x="${x + w / 2}" y="${y + h + 13}" font-size="9" fill="#8a9aa2" text-anchor="middle">${pct}%</text>
    `;
  }
  function pump(cx, on) {
    return pumpIcon(cx, 66, 15, on);
  }
  function pipe(x1, x2, on) {
    const dash = on ? 'stroke-dasharray="6 5" class="flow"' : '';
    return `<line x1="${x1}" y1="66" x2="${x2}" y2="66" stroke="${pipeColor(on)}" stroke-width="5" stroke-linecap="round" ${dash}/>`;
  }

  let s = `<g>`;
  s += pipe(15, 65, pump1On);
  s += pump(30, pump1On);
  s += pipe(45, 78, pump1On);
  s += tank(78, tank1Pct, '#4fa8e0', tank1Temp);
  s += pipe(124, 150, pump2On);
  s += filterCapsule(150, preFilterPct);
  s += pipe(176, 202, pump2On);
  s += pump(217, pump2On);
  s += pipe(232, 258, pump2On);
  s += filterCapsule(258, membranePct);
  s += pipe(284, 310, pump2On);
  s += tank(310, tank2Pct, '#17a8a0', tank2Temp);
  s += `</g>`;
  svgEl.innerHTML = s;
}

// ----- آیکون باتری (نمایشی - فاز ۶ پایش انرژی هنوز پیاده نشده) -----
function buildBattery(svgEl, pct) {
  const color = pct < 20 ? 'var(--zone-red)' : pct < 40 ? 'var(--zone-yellow)' : 'var(--zone-green)';
  const w = 24, fillW = (w - 4) * (pct / 100);
  svgEl.innerHTML = `
    <rect x="1" y="2" width="${w}" height="12" rx="2" fill="none" stroke="#8a9aa2" stroke-width="1.5"/>
    <rect x="${w + 1}" y="6" width="3" height="4" rx="1" fill="#8a9aa2"/>
    <rect x="3" y="4" width="${fillW}" height="8" rx="1" fill="${color}"/>
  `;
}

// ----- گیج دو حلقه‌ای TDS (fill-ring) -----
function buildRings(svgEl, outlet, inlet) {
  const cx = 100, cy = 100;
  function ring(radius, width, value, range, track) {
    const { min, max, zones } = range;
    const frac = Math.max(0, Math.min(1, (value - min) / (max - min)));
    const circ = 2 * Math.PI * radius;
    const color = zoneColorForValue(value, zones);
    const dash = `${circ * frac} ${circ}`;
    return `
      <circle cx="${cx}" cy="${cy}" r="${radius}" fill="none" stroke="${track}" stroke-width="${width}"/>
      <circle cx="${cx}" cy="${cy}" r="${radius}" fill="none" stroke="${color}" stroke-width="${width}"
        stroke-linecap="round" stroke-dasharray="${dash}" transform="rotate(-90 ${cx} ${cy})"/>
    `;
  }
  svgEl.innerHTML = ring(84, 16, outlet, RANGES.tdsOutlet, '#eef2f3') + ring(58, 16, inlet, RANGES.tdsInlet, '#eef2f3');
}

function addGaugeRow(container, { label, value, range, unit, active }) {
  const { min, max, zones } = range;
  const row = document.createElement('div');
  row.className = 'perf-row' + (active ? '' : ' disabled');
  row.innerHTML = `
    <div class="perf-row-top"><span class="perf-label">${label}</span><span class="perf-value"></span></div>
    <div class="bar-wrap"><span class="led"></span>
      <div class="bar-inner"><div class="marker"></div><div class="bar"></div></div>
    </div>`;
  container.appendChild(row);
  const pct = v => ((v - min) / (max - min)) * 100;
  row.querySelector('.bar').innerHTML = zones.map(z => `<div class="seg" style="width:${pct(z.to) - pct(z.from)}%;background:${z.color}"></div>`).join('');
  row.querySelector('.marker').style.left = pct(Math.max(min, Math.min(max, value))) + '%';
  row.querySelector('.led').style.background = zoneColorForValue(value, zones);
  row.querySelector('.perf-value').textContent = value + (unit || '');
  return row;
}

// ردیف تک‌رنگ (طیف زرد) برای شاخص‌هایی که وضعیت خوب/بد ندارند، فقط مقدار توصیفی‌اند
function addGradientRow(container, { label, value, min, max, unit, ledColor, active }) {
  const row = document.createElement('div');
  row.className = 'perf-row' + (active ? '' : ' disabled');
  row.innerHTML = `
    <div class="perf-row-top"><span class="perf-label">${label}</span><span class="perf-value"></span></div>
    <div class="bar-wrap"><span class="led" style="background:${ledColor}"></span>
      <div class="bar-inner"><div class="marker"></div><div class="bar gradient"></div></div>
    </div>`;
  container.appendChild(row);
  const pct = ((value - min) / (max - min)) * 100;
  row.querySelector('.marker').style.left = pct + '%';
  row.querySelector('.perf-value').textContent = value + (unit || '');
}

function renderHomePage() {
  const outlet = state.tds.outlet;
  const inlet = state.tds.inlet;

  // چیپ‌های بالای کارت شماتیک
  document.getElementById('batteryVal').innerHTML = `-- <span class="unit">٪</span>`; // فاز ۶ هنوز پیاده نشده
  buildBattery(document.getElementById('batteryIcon'), 0);
  // فعلاً دمای مخزن آب شرب؛ بعد از نصب سنسور دمای محیط، فقط منبع این مقدار عوض می‌شود
  document.getElementById('mainTempVal').innerHTML = `${outlet.temp.toFixed(1)} <span class="unit">°C</span>`;

  buildSchematic(document.getElementById('schematic'), {
    pump1On: state.pumps.raw, tank1Pct: 70, tank1Temp: inlet.temp,
    preFilterPct: 82, pump2On: state.pumps.treatment, membranePct: 57,
    tank2Pct: 45, tank2Temp: outlet.temp,
  });

  buildRings(document.getElementById('tdsRings'), outlet.tds, inlet.tds);
  const outletColor = zoneColorForValue(outlet.tds, RANGES.tdsOutlet.zones);
  const inletColor = zoneColorForValue(inlet.tds, RANGES.tdsInlet.zones);
  document.querySelector('.rings-wrap .rings-center-label')?.remove();
  document.querySelector('.rings-wrap').insertAdjacentHTML('beforeend', `
    <div class="rings-center-label">
      <div class="row"><span class="dot" style="background:${outletColor}"></span>خروجی <b>${outlet.tds.toFixed(0)}</b> ppm</div>
      <div class="row"><span class="dot" style="background:${inletColor}"></span>ورودی <b>${inlet.tds.toFixed(0)}</b> ppm</div>
    </div>
  `);

  const sideRows = document.getElementById('sideRows');
  sideRows.innerHTML = '';
  const saltRejection = computeSaltRejection();
  addGaugeRow(sideRows, { label: 'نرخ دفع املاح', value: saltRejection, range: RANGES.saltRejection, unit: '%', active: state.hasData });
  addGaugeRow(sideRows, { label: 'ساعت UV', value: MOCK_VALUES.uvHours, range: RANGES.uvHours, unit: ' h', active: false });
  addGradientRow(sideRows, { label: 'میزان تابش', value: MOCK_VALUES.irradiance, min: 0, max: 100, unit: '%', ledColor: '#f5a300', active: false });
  sideRows.insertAdjacentHTML('beforeend', `
    <div class="volume-row">
      <span class="volume-label">حجم آب تولیدی</span>
      <div class="volume-box"><b>${MOCK_VALUES.volumeLiters}</b><span>L</span></div>
    </div>
  `);
}

/* ==================== صفحه عملکرد ==================== */
function renderPerformancePage() {
  const container = document.getElementById('perf-rows');
  container.innerHTML = '';

  addGaugeRow(container, { label: 'سطح مخزن آب خام', value: MOCK_VALUES.rawTankLevel, range: RANGES.rawTankLevel, unit: '%', active: false });
  addGaugeRow(container, { label: 'سطح مخزن آب شرب', value: MOCK_VALUES.productTankLevel, range: RANGES.productTankLevel, unit: '%', active: false });
  addGaugeRow(container, { label: 'فیلتر پیش‌تصفیه', value: MOCK_VALUES.filterPre, range: RANGES.filterUsed, unit: '%', active: false });
  addGaugeRow(container, { label: 'فیلتر ممبران', value: MOCK_VALUES.filterMembrane, range: RANGES.filterUsed, unit: '%', active: false });
  addGaugeRow(container, { label: 'ساعت کارکرد لامپ UV', value: MOCK_VALUES.uvHours, range: RANGES.uvHours, unit: ' h', active: false });
  addGaugeRow(container, { label: 'pH', value: MOCK_VALUES.ph, range: RANGES.ph, unit: '', active: false });
  addGaugeRow(container, { label: 'دمای آب شرب', value: state.tds.outlet.temp, range: RANGES.productTemp, unit: '°C', active: state.hasData });
  addGaugeRow(container, { label: 'دمای محیط', value: MOCK_VALUES.ambientTemp, range: RANGES.ambientTemp, unit: '°C', active: false });

  addStatusRow(container, { label: 'وضعیت پمپ تصفیه', isOn: state.pumps.treatment, onLabel: 'روشن', offLabel: 'خاموش', active: state.pumpsKnown });
  addStatusRow(container, { label: 'وضعیت پمپ آب خام', isOn: state.pumps.raw, onLabel: 'روشن', offLabel: 'خاموش', active: state.pumpsKnown });
  addStatusRow(container, { label: 'وضعیت سنسور نشتی', isOn: false, onLabel: 'نشتی!', offLabel: 'بدون نشتی', dangerWhenOn: true, active: false });
}

function addStatusRow(container, { label, isOn, onLabel, offLabel, dangerWhenOn, active }) {
  const row = document.createElement('div');
  row.className = 'perf-row' + (active ? '' : ' disabled');
  const ledColor = dangerWhenOn
    ? (isOn ? 'var(--zone-red)' : 'var(--zone-green)')
    : (isOn ? 'var(--zone-green)' : '#b0bec5');
  const pillClass = dangerWhenOn ? (isOn ? 'danger' : 'on') : (isOn ? 'on' : '');
  row.innerHTML = `
    <div class="perf-row-top" style="margin-bottom:0">
      <span class="led" style="background:${ledColor}"></span>
      <span class="perf-label">${label}</span>
      <span class="status-pill ${pillClass}">${isOn ? onLabel : offLabel}</span>
    </div>`;
  container.appendChild(row);
}

/* ==================== صفحه هشدارها ==================== */
// فعلاً نمونه/آزمایشی است - منطق واقعی آلارم‌ها در فاز ۷ (منطق‌های حفاظتی) پیاده می‌شود.
// دستگاه ساعت واقعی (RTC/NTP) ندارد، پس به هشدارها زمان نسبت داده نمی‌شود.
const demoAlerts = [
  { severity: 'warning', title: 'گرفتگی نسبی فیلتر پیش‌تصفیه' },
  { severity: 'info', title: 'کالیبراسیون کانال ۲ TDS انجام شد' },
];
function renderAlertsPage() {
  const list = document.getElementById('alertsList');
  if (demoAlerts.length === 0) {
    list.innerHTML = '<div class="alerts-empty">هشداری وجود ندارد</div>';
    return;
  }
  list.innerHTML = demoAlerts.map(a => `
    <div class="alert-item ${a.severity}">
      <span class="sev-dot"></span>
      <div class="alert-body">
        <div class="alert-title">${a.title}</div>
      </div>
    </div>
  `).join('');
}

/* ==================== گزارش PDF (Export) ==================== */
// این تابع دقیقاً همان پارامترهایی را برمی‌گرداند که در صفحات خانه/عملکرد نشان داده
// می‌شوند - وقتی پارامتری به برنامه اضافه/حذف شود، اینجا هم باید به‌روزرسانی شود
// (طبق قانون پروژه: گزارش PDF باید همیشه با صفحات هم‌گام بماند).
function getReportParams() {
  return [
    { label: 'TDS خروجی (آب شرب)', value: state.tds.outlet.tds, unit: ' ppm', ...RANGES.tdsOutlet },
    { label: 'TDS ورودی (آب خام)', value: state.tds.inlet.tds, unit: ' ppm', ...RANGES.tdsInlet },
    { label: 'نرخ دفع املاح', value: computeSaltRejection(), unit: '%', ...RANGES.saltRejection },
    { label: 'دمای آب شرب', value: state.tds.outlet.temp, unit: '°C', ...RANGES.productTemp },
    { label: 'دمای آب خام', value: state.tds.inlet.temp, unit: '°C', ...RANGES.productTemp },
    { label: 'فیلتر پیش‌تصفیه (ظرفیت استفاده‌شده)', value: MOCK_VALUES.filterPre, unit: '%', ...RANGES.filterUsed },
    { label: 'فیلتر ممبران (ظرفیت استفاده‌شده)', value: MOCK_VALUES.filterMembrane, unit: '%', ...RANGES.filterUsed },
    { label: 'ساعت کارکرد لامپ UV', value: MOCK_VALUES.uvHours, unit: ' h', ...RANGES.uvHours },
    { label: 'حجم آب تولیدی', value: MOCK_VALUES.volumeLiters, unit: ' L', min: 0, max: 2000, zones: [{ from: 0, to: 2000, color: 'var(--zone-green)' }] },
  ];
  // نکته: zoneColorHex در report.js با .includes('green'/'yellow') کار می‌کند، پس
  // فرمت "var(--zone-green)" همان‌طور که در RANGES تعریف شده بدون تبدیل قابل استفاده است.
}
function getReportStatusRows() {
  return [
    { label: 'وضعیت پمپ تصفیه', on: state.pumps.treatment },
    { label: 'وضعیت پمپ آب خام', on: state.pumps.raw },
  ];
}

document.getElementById('btnExport').addEventListener('click', () => {
  document.getElementById('exportModal').hidden = false;
});
document.getElementById('exportModalClose').addEventListener('click', () => {
  document.getElementById('exportModal').hidden = true;
});
document.getElementById('exportParams').addEventListener('click', async () => {
  await generateParametersReport(getReportParams(), getReportStatusRows(), demoAlerts);
  document.getElementById('exportModal').hidden = true;
});
document.getElementById('exportAlerts').addEventListener('click', async () => {
  await generateAlertsReport(demoAlerts);
  document.getElementById('exportModal').hidden = true;
});

/* ==================== صفحه تنظیمات ==================== */
function showSettingsView(id) {
  document.querySelectorAll('.settings-view').forEach(v => v.classList.toggle('active', v.id === id));
}
document.getElementById('btnCalibration').addEventListener('click', () => {
  document.getElementById('calibCurrentTemp1').textContent = `کنونی: ${state.tds.inlet.temp.toFixed(1)}°C`;
  document.getElementById('calibCurrentEc1').textContent = `کنونی: ${state.tds.inlet.ec.toFixed(0)}`;
  document.getElementById('calibCurrentTemp2').textContent = `کنونی: ${state.tds.outlet.temp.toFixed(1)}°C`;
  document.getElementById('calibCurrentEc2').textContent = `کنونی: ${state.tds.outlet.ec.toFixed(0)}`;
  showSettingsView('settingsCalibration');
});
document.getElementById('calibBack').addEventListener('click', () => {
  showSettingsView('settingsMain');
  document.getElementById('calibResult').hidden = true;
});

const CALIB_META = {
  temp1: { channel: 1, cmd: 'calibrate_temp', label: 'دما - کانال ۱', unit: '°C' },
  ec1:   { channel: 1, cmd: 'calibrate_ec',   label: 'EC - کانال ۱', unit: 'µS/cm' },
  temp2: { channel: 2, cmd: 'calibrate_temp', label: 'دما - کانال ۲', unit: '°C' },
  ec2:   { channel: 2, cmd: 'calibrate_ec',   label: 'EC - کانال ۲', unit: 'µS/cm' },
};
document.getElementById('calibOkBtn').addEventListener('click', () => {
  const entered = [...document.querySelectorAll('.calib-input')].filter(i => i.value.trim() !== '');
  const resultBox = document.getElementById('calibResult');
  if (entered.length === 0) {
    resultBox.innerHTML = '<div>هیچ مقداری وارد نشد؛ کالیبراسیونی اجرا نمی‌شود.</div>';
  } else {
    resultBox.innerHTML = entered.map(i => {
      const meta = CALIB_META[i.dataset.key];
      sendCommand({ cmd: meta.cmd, channel: meta.channel, value: parseFloat(i.value) });
      return `<div id="calib-status-${meta.channel}-${meta.cmd}">⏳ در حال کالیبراسیون «${meta.label}» با مقدار ${i.value} ${meta.unit}...</div>`;
    }).join('');
    entered.forEach(i => { i.value = ''; });
  }
  resultBox.hidden = false;
});

// ----- دکمه‌های روشن/خاموش (کنترل دستی موقت رله‌ها تا منطق کامل کنترل نوشته شود) -----
function setToggleVisual(toggleEl, isOn) {
  toggleEl.classList.toggle('on', isOn);
  toggleEl.classList.toggle('off', !isOn);
  toggleEl.querySelector('.ptoggle-label').textContent = isOn ? 'ON' : 'OFF';
  const iconColor = isOn ? '#6fbf4f' : '#8a97a0';
  toggleEl.querySelectorAll('svg *').forEach(el => el.setAttribute('stroke', iconColor));
}

const powerToggle = document.getElementById('powerToggle');
powerToggle.addEventListener('click', () => {
  const turningOn = !powerToggle.classList.contains('on');
  setToggleVisual(powerToggle, turningOn);
  sendCommand({ cmd: 'power', on: turningOn });
});

const rawPumpToggle = document.getElementById('rawPumpToggle');
rawPumpToggle.addEventListener('click', () => {
  const turningOn = !rawPumpToggle.classList.contains('on');
  setToggleVisual(rawPumpToggle, turningOn);
  sendCommand({ cmd: 'raw_pump', on: turningOn });
});

// وقتی وضعیت واقعی رله‌ها از ESP32 می‌رسد (یا هنگام ورود به صفحه تنظیمات)، دکمه‌ها را با آن هماهنگ کن
function syncPowerToggles() {
  setToggleVisual(powerToggle, state.pumps.treatment);
  setToggleVisual(rawPumpToggle, state.pumps.raw);
}

// ----- تعویض فیلتر (فعلاً فقط محلی؛ فاز ۷ فرمول واقعی ظرفیت فیلترها را مشخص می‌کند) -----
const filters = [
  { key: 'ppf', label: 'فیلتر الیاف (PPF)', usedPct: 18 },
  { key: 'gac', label: 'فیلتر ذغال اکتیو (GAC)', usedPct: 34 },
  { key: 'membrane', label: 'فیلتر ممبران', usedPct: 61 },
  { key: 'uv', label: 'لامپ UV', usedPct: 13 },
];
function pctClass(p) { return p < 40 ? 'low' : p < 70 ? 'mid' : 'high'; }
function renderFilterGrid() {
  const grid = document.getElementById('filterGrid');
  grid.innerHTML = filters.map(f => `
    <button class="filter-option" data-key="${f.key}">
      <span>${f.label}</span>
      <span class="pct ${pctClass(f.usedPct)}">${f.usedPct}%</span>
      <span style="font-size:8px;color:#aab6bd">استفاده‌شده</span>
    </button>
  `).join('');
  grid.querySelectorAll('.filter-option').forEach(btn => {
    btn.addEventListener('click', () => openConfirm(btn.dataset.key));
  });
}
document.getElementById('btnFilters').addEventListener('click', () => {
  renderFilterGrid();
  document.getElementById('filterModal').hidden = false;
});
document.getElementById('filterModalClose').addEventListener('click', () => {
  document.getElementById('filterModal').hidden = true;
});

let pendingFilterKey = null;
function openConfirm(key) {
  pendingFilterKey = key;
  const f = filters.find(x => x.key === key);
  document.getElementById('confirmText').textContent = `آیا از تعویض‌شدن «${f.label}» مطمئن هستید؟`;
  document.getElementById('confirmModal').hidden = false;
}
document.getElementById('confirmNo').addEventListener('click', () => { document.getElementById('confirmModal').hidden = true; });
document.getElementById('confirmYes').addEventListener('click', () => {
  const f = filters.find(x => x.key === pendingFilterKey);
  f.usedPct = 0;
  document.getElementById('confirmModal').hidden = true;
  document.getElementById('filterModal').hidden = true;
  showToast(`«${f.label}» تعویض و صفر شد`);
});
function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2200);
}

/* ==================== ناوبری بین صفحات ==================== */
function showPage(name) {
  document.querySelectorAll('.page').forEach(p => p.classList.toggle('active', p.id === `page-${name}`));
  document.querySelectorAll('.nav-item[data-page]').forEach(b => b.classList.toggle('active', b.dataset.page === name));
  if (name === 'home') renderHomePage();
  if (name === 'performance') renderPerformancePage();
  if (name === 'settings') { showSettingsView('settingsMain'); syncPowerToggles(); }
  if (name === 'alerts') renderAlertsPage();
}
document.querySelectorAll('.nav-item[data-page]').forEach(btn => {
  btn.addEventListener('click', () => showPage(btn.dataset.page));
});

/* ==================== اتصال WebSocket به ESP32 ==================== */
let socket = null;
function sendCommand(obj) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(obj));
  }
}

function connectWS() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${proto}://${location.host}/ws`);
  const dot = document.getElementById('conn-dot');
  const app = document.getElementById('app');

  socket.onopen = () => { dot.classList.add('connected'); app.classList.remove('offline'); };
  socket.onclose = () => {
    dot.classList.remove('connected');
    app.classList.add('offline');
    setTimeout(connectWS, 2000);
  };
  socket.onerror = () => socket.close();

  socket.onmessage = (evt) => {
    let data;
    try { data = JSON.parse(evt.data); } catch (e) { return; }

    const activePage = document.querySelector('.nav-item.active[data-page]').dataset.page;

    if (data.tds1 || data.tds2) {
      if (data.tds1) Object.assign(state.tds.inlet, data.tds1);
      if (data.tds2) Object.assign(state.tds.outlet, data.tds2);
      state.hasData = true;
    }

    if (data.pumps) {
      state.pumps.treatment = !!data.pumps.treatment;
      state.pumps.uv = !!data.pumps.uv;
      state.pumps.raw = !!data.pumps.raw;
      state.pumpsKnown = true;
      if (activePage === 'settings') syncPowerToggles();
    }

    if (data.tds1 || data.tds2 || data.pumps) {
      if (activePage === 'home') renderHomePage();
      if (activePage === 'performance') renderPerformancePage();
    }

    if (data.calibResult) {
      const { type, channel, ok } = data.calibResult;
      const meta = Object.values(CALIB_META).find(m => m.channel === channel && m.cmd === (type === 'ec' ? 'calibrate_ec' : 'calibrate_temp'));
      const label = meta ? meta.label : `${type} کانال ${channel}`;
      showToast(ok ? `کالیبراسیون «${label}» موفق بود` : `کالیبراسیون «${label}» ناموفق بود`);
    }
  };
}

showPage('home');
document.getElementById('app').classList.add('offline'); // تا وصل نشدیم، آفلاین نمایش داده شود
connectWS();

if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => navigator.serviceWorker.register('sw.js'));
}
