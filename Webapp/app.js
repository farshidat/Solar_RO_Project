// لایه داده مرکزی: همه صفحات از همین state می‌خونن. بعداً اگر منبع داده عوض شد
// (مثلاً بخشی از پارامترها هم از MQTT بیاد) فقط همین‌جا تغییر می‌کنه.
const state = {
  tds: {
    inlet:  { ec: 0, temp: 0, tds: 0 },
    outlet: { ec: 0, temp: 0, tds: 0 },
  },
  pumps: { treatment: false, uv: false, raw: false },
  systemEnabled: false,
  hasData: false,
  pumpsKnown: false,
  inputsKnown: false,
  inputs: {
    pressureOk: false,
    tankFull: false,
    leak: false,
    tankPressureBar: null,
  },
  locked: false,
  fault: 'none',
  dryRunRetries: 0,
  relays: { r1: false, r2: false, purify: false, night: false },
  bench: {
    enabled: false,
    vSolar: null,
    vSolarAdc: null,
    irradiancePct: null,
    tankPressureBar: null,
    pressureAdc: null,
  },
  opMode: '',
  opModeLabel: '',
  standbyReason: 'none',
  nightLight: false,
  intakeWaitActive: false,
  intakeWaitSec: 0,
  intakeWaitMs: 0,
  intakeRawFails: 0,
  _powerCmdUntil: 0, // ignore stale WS overwrite shortly after user toggles power
  // حالت سیستم (NVS در فریمور). فعلاً برای پیش‌نمایش UI قابل سوئیچ است.
  scenario: 'B', // 'A' = آب شهری · 'B' = پمپ خام
  // روال فعال (آب‌گیری / تصفیه / روشنایی شبانه / انتظار و ...)
  activeRoutine: 'انتظار',
  // SoC باتری؛ null = هنوز داده زنده نیست → نمایش --
  batterySoc: null,
  // مسیر Drain (Relay 2) — فعلاً برای نمایش شماتیک
  drainOpen: false,
  deviceEpoch: null,
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
  ambientTemp:     { min: -10, max: 60, zones: zonesFromStops(-10, [[5, 'yellow'], [45, 'green'], [60, 'yellow']]) },
  // فشار تانک ۴۰L — P_low=1.5 / P_high=3.5
  tankPressure:    { min: 0, max: 5, zones: zonesFromStops(0, [[1.5, 'red'], [3.5, 'green'], [5, 'yellow']]) },
  // تابش٪ — شب‌چراغ <5٪ · روز از 35٪ (هیسترزیس خروج 30٪)
  irradiance:      { min: 0, max: 100, zones: zonesFromStops(0, [[5, 'red'], [30, 'yellow'], [100, 'green']]) },
  // ولتاژ پنل 0–60V هم‌تراز با باند تابش
  panelVoltage:    { min: 0, max: 60, zones: zonesFromStops(0, [[3, 'red'], [18, 'yellow'], [60, 'green']]) },
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

/* ==================== صفحه خانه — شماتیک ==================== */
// وضعیت پمپ‌ها و دمای مخازن واقعی‌اند؛ سطح مخازن و ظرفیت فیلترها فعلاً نمایشی است.

const SCH = {
  ink: '#3d4a52',
  cream: '#f3efe2',
  blueBody: '#8fd0ea',
  blueDeep: '#5aa8c8',
  graySoft: '#d7dde1',
  grayMid: '#9aa6ad',
  grayPipe: '#c5d5dc',
  raw: '#17a8a0',
  rawFlow: '#0e7a74',
  clean: '#4fa8e0',
  cleanFlow: '#1f7eb0',
  port: { pump: 24, tank: 17, filter: 18 },
  // false = برگرد به حالت بدون قطره (tag: ui-tank-before-droplets)
  tankFillDroplets: true,
};

function schShade(on, color, off = SCH.grayPipe) {
  return on ? color : off;
}

function schPipeSeg(x1, x2, y, on, color, flowColor) {
  const fill = schShade(on, color);
  const dash = schShade(on, flowColor, '#a8b6bc');
  const cls = on ? 'flow' : '';
  return `
    <line x1="${x1}" y1="${y}" x2="${x2}" y2="${y}"
      stroke="${fill}" stroke-width="6" stroke-linecap="butt"/>
    <line x1="${x1}" y1="${y}" x2="${x2}" y2="${y}"
      stroke="${dash}" stroke-width="2.2" stroke-linecap="butt"
      stroke-dasharray="5 4" class="${cls}" opacity="${on ? 1 : 0.3}"/>
  `;
}

/** برکه منبع + لوله عمود با خم نرم تا پمپ خام (جریان فقط با پمپ خام) */
function schIntakeFromPond(pumpInletX, pipeY, on) {
  const pondCx = 26;
  const pondCy = pipeY + 44;
  const dropX = pondCx;
  const color = schShade(on, SCH.raw);
  const flow = schShade(on, SCH.rawFlow, '#a8b6bc');
  const cls = on ? 'flow' : '';
  const s = 1.5;

  const pondPath = `
    M ${pondCx - 14 * s} ${pondCy}
    C ${pondCx - 15 * s} ${pondCy - 8 * s}, ${pondCx - 10 * s} ${pondCy - 11 * s}, ${pondCx - 4 * s} ${pondCy - 9 * s}
    C ${pondCx - 1 * s} ${pondCy - 13 * s}, ${pondCx + 5 * s} ${pondCy - 12 * s}, ${pondCx + 8 * s} ${pondCy - 8 * s}
    C ${pondCx + 14 * s} ${pondCy - 10 * s}, ${pondCx + 16 * s} ${pondCy - 3 * s}, ${pondCx + 14 * s} ${pondCy + 2 * s}
    C ${pondCx + 17 * s} ${pondCy + 7 * s}, ${pondCx + 11 * s} ${pondCy + 11 * s}, ${pondCx + 5 * s} ${pondCy + 9 * s}
    C ${pondCx + 1 * s} ${pondCy + 12 * s}, ${pondCx - 5 * s} ${pondCy + 11 * s}, ${pondCx - 8 * s} ${pondCy + 7 * s}
    C ${pondCx - 14 * s} ${pondCy + 9 * s}, ${pondCx - 16 * s} ${pondCy + 3 * s}, ${pondCx - 14 * s} ${pondCy}
    Z`;
  const waterPath = `
    M ${pondCx - 10 * s} ${pondCy}
    C ${pondCx - 11 * s} ${pondCy - 5 * s}, ${pondCx - 6 * s} ${pondCy - 7 * s}, ${pondCx - 1 * s} ${pondCy - 6 * s}
    C ${pondCx + 2 * s} ${pondCy - 9 * s}, ${pondCx + 7 * s} ${pondCy - 7 * s}, ${pondCx + 9 * s} ${pondCy - 4 * s}
    C ${pondCx + 12 * s} ${pondCy - 5 * s}, ${pondCx + 12 * s} ${pondCy}, ${pondCx + 10 * s} ${pondCy + 3 * s}
    C ${pondCx + 12 * s} ${pondCy + 6 * s}, ${pondCx + 7 * s} ${pondCy + 8 * s}, ${pondCx + 3 * s} ${pondCy + 6 * s}
    C ${pondCx} ${pondCy + 8 * s}, ${pondCx - 5 * s} ${pondCy + 7 * s}, ${pondCx - 7 * s} ${pondCy + 4 * s}
    C ${pondCx - 11 * s} ${pondCy + 5 * s}, ${pondCx - 12 * s} ${pondCy + 2 * s}, ${pondCx - 10 * s} ${pondCy}
    Z`;

  const pondTop = pondCy - 9 * s;
  const bend = 12;
  // مسیر: برکه → بالا → خم نرم → پمپ (هم‌جهت جریان)
  const d = `M ${dropX} ${pondTop + 8}
             L ${dropX} ${pipeY + bend}
             Q ${dropX} ${pipeY} ${dropX + bend} ${pipeY}
             L ${pumpInletX} ${pipeY}`;

  return `
    <path d="${pondPath}" fill="#c5e8e4" stroke="${SCH.ink}" stroke-width="1.25"/>
    <path d="${waterPath}" fill="${SCH.raw}" opacity="0.92"/>
    ${on ? `<path d="M ${pondCx - 4} ${pondCy} Q ${pondCx} ${pondCy - 2} ${pondCx + 4} ${pondCy}" fill="none" stroke="#fff" stroke-width="1" opacity="0.45" class="flow"/>` : ''}
    <path d="${d}" fill="none" stroke="${color}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round"/>
    <path d="${d}" fill="none" stroke="${flow}" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"
      stroke-dasharray="5 4" class="${cls}" opacity="${on ? 1 : 0.3}"/>
  `;
}

function schIconPump(cx, pipeY, on, waterColor) {
  const cy = pipeY - 4;
  const r = 16;
  const body = on ? SCH.cream : '#eceff1';
  const ring = on ? (waterColor === SCH.clean ? SCH.blueBody : '#7dcdc4') : SCH.graySoft;
  const hub = on ? SCH.ink : SCH.grayMid;
  const base = on ? SCH.ink : SCH.grayMid;
  const pipeFill = schShade(on, waterColor);
  const spin = on ? 'pump-impeller-spin' : '';
  const { pump: port } = SCH.port;

  let vanes = '';
  for (let i = 0; i < 8; i++) {
    const a = (i * 45 - 90) * Math.PI / 180;
    const x = cx + Math.cos(a) * 9.5;
    const y = cy + Math.sin(a) * 9.5;
    vanes += `<rect x="${(x - 1.9).toFixed(2)}" y="${(y - 2.8).toFixed(2)}" width="3.8" height="5.6" rx="0.7"
      fill="${hub}" transform="rotate(${i * 45} ${x.toFixed(2)} ${y.toFixed(2)})"/>`;
  }

  return `
    <rect x="${cx - port}" y="${pipeY - 4}" width="${port * 2}" height="8" rx="2"
      fill="${pipeFill}" stroke="${SCH.ink}" stroke-width="1.5"/>
    <rect x="${cx - port}" y="${pipeY - 7}" width="4" height="14" rx="1"
      fill="${pipeFill}" stroke="${SCH.ink}" stroke-width="1.3"/>
    <rect x="${cx + port - 4}" y="${pipeY - 7}" width="4" height="14" rx="1"
      fill="${pipeFill}" stroke="${SCH.ink}" stroke-width="1.3"/>
    <path d="M ${cx - 7} ${cy + 12} L ${cx + 7} ${cy + 12} L ${cx + 10} ${cy + 23} L ${cx - 10} ${cy + 23} Z"
      fill="${base}" stroke="${SCH.ink}" stroke-width="1.1"/>
    <rect x="${cx - 12}" y="${cy + 22}" width="24" height="4.5" rx="1.2" fill="${base}"/>
    <circle cx="${cx}" cy="${cy}" r="${r}" fill="${ring}" stroke="${SCH.ink}" stroke-width="2.2"/>
    <circle cx="${cx}" cy="${cy}" r="12" fill="${body}" stroke="${SCH.ink}" stroke-width="1.3"/>
    <g class="${spin}" style="transform-origin:${cx}px ${cy}px">
      ${vanes}
      <circle cx="${cx}" cy="${cy}" r="3.8" fill="${hub}"/>
    </g>
  `;
}

/** فیلتر تمیز — بدون زبانه خاکستری کناری و بدون تداخل با درصد */
function schIconFilter(cx, pipeY, pct, tone = 'pre') {
  const isRo = tone === 'ro';
  const { filter: port } = SCH.port;
  const headW = 22, headH = 16;
  const bodyW = 20, bodyH = 38;
  const headTop = pipeY - headH / 2;
  const bodyTop = pipeY + headH / 2 - 1;
  const leftColor = SCH.raw;
  const rightColor = isRo ? SCH.clean : SCH.raw;
  const headFill = isRo ? SCH.blueBody : '#7dcdc4';
  const bodyFill = isRo ? '#d9eff8' : '#eceff1';
  const fillColor = isRo ? SCH.blueDeep : '#7a8a92';
  const fillMax = bodyH - 8;
  const fillH = Math.max(3, fillMax * (pct / 100));
  const fillY = bodyTop + bodyH - 4 - fillH;
  const labelY = bodyTop + bodyH + 14;
  const clipId = `fclip-${tone}-${cx}`;

  return `
    <line x1="${cx - port}" y1="${pipeY}" x2="${cx + port}" y2="${pipeY}"
      stroke="${leftColor}" stroke-width="6" stroke-linecap="butt"/>
    <line x1="${cx}" y1="${pipeY}" x2="${cx + port}" y2="${pipeY}"
      stroke="${rightColor}" stroke-width="6" stroke-linecap="butt"/>
    <rect x="${cx - headW / 2}" y="${headTop}" width="${headW}" height="${headH}" rx="4"
      fill="${headFill}" stroke="${SCH.ink}" stroke-width="1.5"/>
    <path d="
      M ${cx - bodyW / 2} ${bodyTop}
      L ${cx - bodyW / 2 + 1} ${bodyTop + bodyH - 6}
      Q ${cx} ${bodyTop + bodyH + 2} ${cx + bodyW / 2 - 1} ${bodyTop + bodyH - 6}
      L ${cx + bodyW / 2} ${bodyTop} Z"
      fill="${bodyFill}" stroke="${SCH.ink}" stroke-width="1.5"/>
    <clipPath id="${clipId}">
      <path d="
        M ${cx - bodyW / 2 + 2} ${bodyTop + 2}
        L ${cx - bodyW / 2 + 2.5} ${bodyTop + bodyH - 7}
        Q ${cx} ${bodyTop + bodyH - 1} ${cx + bodyW / 2 - 2.5} ${bodyTop + bodyH - 7}
        L ${cx + bodyW / 2 - 2} ${bodyTop + 2} Z"/>
    </clipPath>
    <rect x="${cx - 5}" y="${fillY}" width="10" height="${fillH}" rx="3"
      fill="${fillColor}" opacity="0.85" clip-path="url(#${clipId})"/>
    <text x="${cx}" y="${labelY}" text-anchor="middle" class="sch-pct">${Math.round(pct)}%</text>
  `;
}

function schIconValve(cx, pipeY, on) {
  const body = on ? '#7dcdc4' : SCH.graySoft;
  const stem = on ? SCH.ink : SCH.grayMid;
  const pipeFill = schShade(on, SCH.raw);
  const port = 18;
  return `
    <rect x="${cx - port}" y="${pipeY - 4}" width="${port * 2}" height="8" rx="2"
      fill="${pipeFill}" stroke="${SCH.ink}" stroke-width="1.4"/>
    <rect x="${cx - 11}" y="${pipeY - 11}" width="22" height="22" rx="4"
      fill="${body}" stroke="${SCH.ink}" stroke-width="1.7"/>
    <circle cx="${cx}" cy="${pipeY}" r="5.5" fill="${SCH.cream}" stroke="${SCH.ink}" stroke-width="1.3"/>
    <line x1="${cx}" y1="${pipeY - 11}" x2="${cx}" y2="${pipeY - 18}" stroke="${stem}" stroke-width="2.2" stroke-linecap="round"/>
    <rect x="${cx - 7}" y="${pipeY - 22}" width="14" height="5" rx="1.5" fill="${stem}"/>
  `;
}

/** قطره‌های ورودی از اتصال لوله چپ، داخل فضای خالی بالای سطح آب */
function schTankFillDrips(cx, pipeY, waterY, waterColor, clipId) {
  if (!SCH.tankFillDroplets) return '';
  const startY = pipeY + 1;
  const endY = waterY - 1;
  if (endY < startY + 6) return '';
  // نقطه ورود: نزدیک نازل چپ داخل مخزن
  const baseX = cx - 9;
  let drips = '';
  for (let i = 0; i < 3; i++) {
    const dx = baseX + i * 3.2;
    const begin = `${(i * 0.3).toFixed(1)}s`;
    drips += `
      <circle cx="${dx}" cy="${startY}" r="1.7" fill="${waterColor}" opacity="0">
        <animate attributeName="cy" from="${startY}" to="${endY}" dur="0.85s" begin="${begin}" repeatCount="indefinite"/>
        <animate attributeName="opacity" values="0;0.95;0.9;0" keyTimes="0;0.12;0.75;1" dur="0.85s" begin="${begin}" repeatCount="indefinite"/>
      </circle>
      <ellipse cx="${dx}" cy="${startY}" rx="1.1" ry="1.6" fill="${waterColor}" opacity="0">
        <animate attributeName="cy" from="${startY}" to="${endY}" dur="0.85s" begin="${begin}" repeatCount="indefinite"/>
        <animate attributeName="opacity" values="0;0.7;0.55;0" keyTimes="0;0.12;0.75;1" dur="0.85s" begin="${begin}" repeatCount="indefinite"/>
      </ellipse>`;
  }
  return `<g class="tank-fill-drips" clip-path="url(#${clipId})">${drips}</g>`;
}

function schIconTank(cx, pipeY, { pct, waterColor, tempC = null, outlets = 'both', levelText = null, binaryFull = null, filling = false }) {
  const w = 34, h = 54;
  const x = cx - w / 2;
  const y = pipeY - 20;
  // پر/کم باید از نظر بصری واضح باشد (نه فقط چند پیکسل اختلاف)
  const fillPct = binaryFull === true ? 88 : binaryFull === false ? 10 : pct;
  const fillH = Math.max(3, (h - 10) * (fillPct / 100));
  const waterY = y + h - 3 - fillH;
  const { tank: port } = SCH.port;
  const clipId = `tankClip-${cx}-${Math.round(fillPct)}-${levelText || 'p'}`;
  let ribs = '';
  for (let i = 1; i <= 4; i++) {
    const ry = y + 8 + i * ((h - 14) / 5);
    ribs += `<line x1="${x + 2}" y1="${ry}" x2="${x + w - 2}" y2="${ry}" stroke="${SCH.ink}" stroke-width="1.2" opacity="0.35"/>`;
  }
  const leftNozzle = `<rect x="${cx - port}" y="${pipeY - 3.5}" width="6" height="7" fill="${waterColor}" stroke="${SCH.ink}" stroke-width="1.1"/>`;
  const rightNozzle = outlets === 'both'
    ? `<rect x="${cx + port - 6}" y="${pipeY - 3.5}" width="6" height="7" fill="${waterColor}" stroke="${SCH.ink}" stroke-width="1.1"/>`
    : '';

  const tempY = y - 14;
  const pctY = y + h + 18;
  const levelLabel = levelText != null ? levelText : `${Math.round(pct)}%`;
  const tempHtml = tempC != null
    ? `<text x="${cx}" y="${tempY}" text-anchor="middle" class="sch-temp">${Number(tempC).toFixed(1)}°C</text>`
    : '';
  const drips = filling ? schTankFillDrips(cx, pipeY, waterY, waterColor, clipId) : '';

  return `
    ${tempHtml}
    <rect x="${cx - 8}" y="${y - 7}" width="16" height="5" rx="2" fill="${SCH.grayMid}" stroke="${SCH.ink}" stroke-width="1.2"/>
    <rect x="${cx - 5}" y="${y - 3}" width="10" height="5" fill="${SCH.graySoft}" stroke="${SCH.ink}" stroke-width="1"/>
    <rect x="${x}" y="${y}" width="${w}" height="${h}" rx="10" fill="#eef7f6" stroke="${SCH.ink}" stroke-width="1.8"/>
    <defs><clipPath id="${clipId}"><rect x="${x + 2}" y="${y + 2}" width="${w - 4}" height="${h - 4}" rx="8"/></clipPath></defs>
    <rect x="${x + 2}" y="${waterY}" width="${w - 4}" height="${fillH}"
      fill="${waterColor}" clip-path="url(#${clipId})" opacity="0.9"/>
    ${ribs}
    <ellipse cx="${cx}" cy="${waterY}" rx="${w / 2 - 4}" ry="2.5" fill="#fff" opacity="0.35"/>
    ${drips}
    ${leftNozzle}${rightNozzle}
    <ellipse cx="${cx}" cy="${y + h + 3}" rx="14" ry="3.5" fill="${SCH.grayMid}" stroke="${SCH.ink}" stroke-width="1"/>
    <text x="${cx}" y="${pctY}" text-anchor="middle" class="sch-pct">${levelLabel}</text>
  `;
}

function schMainsIntake(valveInletX, pipeY, on) {
  const color = schShade(on, SCH.raw);
  const flow = schShade(on, SCH.rawFlow, '#a8b6bc');
  const cls = on ? 'flow' : '';
  const x0 = 18;
  return `
    <circle cx="${x0}" cy="${pipeY}" r="7" fill="${on ? '#c5e8e4' : SCH.graySoft}" stroke="${SCH.ink}" stroke-width="1.3"/>
    <path d="M ${x0 - 3} ${pipeY - 2} L ${x0 + 3} ${pipeY} L ${x0 - 3} ${pipeY + 2} Z" fill="${SCH.ink}"/>
    <line x1="${x0 + 7}" y1="${pipeY}" x2="${valveInletX}" y2="${pipeY}"
      stroke="${color}" stroke-width="6" stroke-linecap="butt"/>
    <line x1="${x0 + 7}" y1="${pipeY}" x2="${valveInletX}" y2="${pipeY}"
      stroke="${flow}" stroke-width="2.2" stroke-dasharray="5 4" class="${cls}" opacity="${on ? 1 : 0.3}"/>
  `;
}

function buildSchematic(svgEl, opts) {
  const {
    scenario = 'B',
    relay1On = false,
    treatmentOn = false,
    systemEnabled = false,
    preFilterPct = 82,
    membranePct = 57,
    rawTankPct = 90,
    productFull = false,
    inletTemp = 0,
    productTemp = 0,
  } = opts;

  const pipeY = 52;
  const productLabel = productFull ? 'پر' : 'کم';
  // پر شدن: مخزن کم/خالی + پمپ مربوطه روشن (فقط وقتی سیستم فعال است)
  const productFilling = systemEnabled && !productFull && treatmentOn;
  const rawTankFull = rawTankPct >= 50;
  const rawFilling = systemEnabled && !!relay1On;

  let nodes;
  let labelTexts;
  let s = `<g>`;

  if (scenario === 'A') {
    // شیر ورودی → پیش‌تصفیه → تصفیه+UV → ممبران → مخزن شرب
    // جریان قبل از شیر با همان شرط بخش بعد از شیر (هم‌زمان)
    const aFlowIn = !!(relay1On || treatmentOn);
    const aFlowTreat = !!treatmentOn;
    nodes = [
      { type: 'valve',  cx: 56,  on: relay1On, port: 18 },
      { type: 'filter', cx: 118, pct: preFilterPct, tone: 'pre', port: SCH.port.filter },
      { type: 'pump',   cx: 186, on: treatmentOn, port: SCH.port.pump, water: SCH.raw },
      { type: 'filter', cx: 254, pct: membranePct, tone: 'ro', port: SCH.port.filter },
      { type: 'tank',   cx: 322, pct: productFull ? 88 : 10, color: SCH.clean, temp: productTemp, levelText: productLabel, outlets: 'in', port: SCH.port.tank, binaryFull: productFull, filling: productFilling },
    ];
    labelTexts = [
      ['شیر', 'ورودی'], ['فیلتر', 'پیش‌تصفیه'], ['تصفیه', '+ UV'],
      ['ممبران', 'RO'], ['مخزن', 'آب شرب'],
    ];
    s += schMainsIntake(nodes[0].cx - nodes[0].port, pipeY, aFlowIn);
    s += schPipeSeg(nodes[0].cx + nodes[0].port, nodes[1].cx - nodes[1].port, pipeY, aFlowIn, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[1].cx + nodes[1].port, nodes[2].cx - nodes[2].port, pipeY, aFlowTreat, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[2].cx + nodes[2].port, nodes[3].cx - nodes[3].port, pipeY, aFlowTreat, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[3].cx + nodes[3].port, nodes[4].cx - nodes[4].port, pipeY, aFlowTreat, SCH.clean, SCH.cleanFlow);
  } else {
    // پمپ آب خام → تانک ۴۰L → پیش‌تصفیه → تصفیه+UV → ممبران → مخزن شرب
    nodes = [
      { type: 'pump',   cx: 68,  on: relay1On, port: SCH.port.pump, water: SCH.raw },
      { type: 'tank',   cx: 120, pct: rawTankFull ? 88 : 10, color: SCH.raw, temp: inletTemp, outlets: 'both', port: SCH.port.tank, levelText: rawTankFull ? 'پر' : 'خالی', binaryFull: rawTankFull, filling: rawFilling },
      { type: 'filter', cx: 172, pct: preFilterPct, tone: 'pre', port: SCH.port.filter },
      { type: 'pump',   cx: 224, on: treatmentOn, port: SCH.port.pump, water: SCH.raw },
      { type: 'filter', cx: 276, pct: membranePct, tone: 'ro', port: SCH.port.filter },
      { type: 'tank',   cx: 330, pct: productFull ? 88 : 10, color: SCH.clean, temp: productTemp, levelText: productLabel, outlets: 'in', port: SCH.port.tank, binaryFull: productFull, filling: productFilling },
    ];
    labelTexts = [
      ['پمپ', 'آب خام'], ['تانک', '۴۰ لیتری'], ['فیلتر', 'پیش‌تصفیه'],
      ['تصفیه', '+ UV'], ['ممبران', 'RO'], ['مخزن', 'آب شرب'],
    ];
    s += schIntakeFromPond(nodes[0].cx - nodes[0].port, pipeY, relay1On);
    s += schPipeSeg(nodes[0].cx + nodes[0].port, nodes[1].cx - nodes[1].port, pipeY, relay1On, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[1].cx + nodes[1].port, nodes[2].cx - nodes[2].port, pipeY, treatmentOn, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[2].cx + nodes[2].port, nodes[3].cx - nodes[3].port, pipeY, treatmentOn, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[3].cx + nodes[3].port, nodes[4].cx - nodes[4].port, pipeY, treatmentOn, SCH.raw, SCH.rawFlow);
    s += schPipeSeg(nodes[4].cx + nodes[4].port, nodes[5].cx - nodes[5].port, pipeY, treatmentOn, SCH.clean, SCH.cleanFlow);
  }

  for (const n of nodes) {
    if (n.type === 'pump') s += schIconPump(n.cx, pipeY, n.on, n.water);
    else if (n.type === 'valve') s += schIconValve(n.cx, pipeY, n.on);
    else if (n.type === 'tank') {
      s += schIconTank(n.cx, pipeY, {
        pct: n.pct,
        waterColor: n.color,
        tempC: n.temp,
        outlets: n.outlets,
        levelText: n.levelText ?? null,
        binaryFull: n.binaryFull ?? null,
        filling: !!n.filling,
      });
    } else s += schIconFilter(n.cx, pipeY, n.pct, n.tone);
  }

  const labelY1 = 138, labelY2 = 148;
  nodes.forEach((n, i) => {
    const [l1, l2] = labelTexts[i];
    s += `<text x="${n.cx}" y="${labelY1}" text-anchor="middle" class="sch-label">${l1}</text>`;
    s += `<text x="${n.cx}" y="${labelY2}" text-anchor="middle" class="sch-label">${l2}</text>`;
  });

  s += `</g>`;
  svgEl.setAttribute('viewBox', '0 0 360 168');
  svgEl.setAttribute('height', '168');
  svgEl.innerHTML = s;
}

// از بازسازی مداوم SVG (و ریست شدن انیمیشن قطره) در هر تیک WebSocket جلوگیری می‌کند
let _schematicKey = '';
function buildSchematicIfChanged(svgEl, opts) {
  const key = [
    opts.scenario, !!opts.relay1On, !!opts.treatmentOn, !!opts.systemEnabled,
    !!opts.productFull, opts.rawTankPct,
    Math.round(opts.inletTemp * 10), Math.round(opts.productTemp * 10),
    opts.preFilterPct, opts.membranePct,
    !!SCH.tankFillDroplets,
  ].join('|');
  if (key === _schematicKey && svgEl.getAttribute('data-sch-key') === key) return;
  _schematicKey = key;
  svgEl.setAttribute('data-sch-key', key);
  buildSchematic(svgEl, opts);
}

function inferActiveRoutine() {
  if (state.pumps.raw) return 'آب‌گیری';
  if (state.pumps.treatment) return 'تصفیه';
  return state.activeRoutine || 'انتظار';
}

function formatMmSs(totalSec) {
  const s = Math.max(0, Math.floor(Number(totalSec) || 0));
  const mm = String(Math.floor(s / 60)).padStart(2, '0');
  const ss = String(s % 60).padStart(2, '0');
  return `${mm}:${ss}`;
}

function liveIrradiancePct() {
  if (typeof state.bench.irradiancePct === 'number') {
    return Math.max(0, Math.min(100, state.bench.irradiancePct));
  }
  if (typeof state.bench.vSolar === 'number') {
    return Math.max(0, Math.min(100, (state.bench.vSolar / 60) * 100));
  }
  return null;
}

/** برچسب مد ۳ حالته — اولویت با opModeLabel فیرمور */
function formatOpModeLabel() {
  if (state.opModeLabel) return state.opModeLabel;
  const m = state.opMode;
  if (m === 'active') {
    if (state.pumps.raw) return 'حالت فعال (آبگیری)';
    if (state.pumps.treatment) return 'حالت فعال (تصفیه)';
    return 'حالت فعال';
  }
  if (m === 'standby') {
    const map = {
      tank_full: 'حالت انتظار (مخزن پر است)',
      no_raw_water: 'حالت انتظار (عدم دسترسی به آب خام)',
      fault: 'حالت انتظار (خطا / وقفه حفاظتی)',
      other: 'حالت انتظار',
      none: 'حالت انتظار',
    };
    return map[state.standbyReason] || 'حالت انتظار';
  }
  if (m === 'night') {
    return `حالت شب (چراغ شب: ${state.nightLight ? 'روشن' : 'خاموش'})`;
  }
  return inferActiveRoutine();
}

function renderOpModeBox() {
  const box = document.getElementById('opModeBox');
  const valEl = document.getElementById('opModeVal');
  const waitInline = document.getElementById('intakeWaitInline');
  const timerEl = document.getElementById('intakeWaitTimer');
  const resetBtn = document.getElementById('btnResetIntakeWait');
  if (!valEl) return;

  const hardIntakeLock = state.fault === 'intake_dry' && state.locked;
  const waitActive = !!state.intakeWaitActive && !hardIntakeLock;
  const waitSec = state.intakeWaitSec > 0
    ? state.intakeWaitSec
    : Math.ceil((state.intakeWaitMs || 0) / 1000);
  // فقط وقتی واقعاً معکوس می‌شمارد (ثانیه > 0)
  const counting = waitActive && waitSec > 0;

  if (waitActive) {
    valEl.textContent = 'حالت انتظار (عدم دسترسی به آب خام)';
  } else {
    valEl.textContent = formatOpModeLabel();
  }

  if (box) {
    box.classList.toggle('is-wait', waitActive || state.opMode === 'standby');
    box.classList.toggle('is-night', state.opMode === 'night');
    box.classList.toggle('is-active', state.opMode === 'active');
  }

  // کل بلوک وقفه فقط در حالت وقفه؛ تایمر فقط وقتی ثانیه > 0
  if (waitInline) waitInline.hidden = !waitActive;
  if (timerEl) {
    timerEl.hidden = !counting;
    timerEl.textContent = counting ? formatMmSs(waitSec) : '';
  }
  if (resetBtn) resetBtn.hidden = !waitActive;
}

const SCENARIO_LABELS = {
  A: 'حالت A — آب شهری',
  B: 'حالت B — پمپ آب خام',
};

function updateModeDropdown() {
  const label = document.getElementById('modeDdLabel');
  if (label) label.textContent = SCENARIO_LABELS[state.scenario] || SCENARIO_LABELS.B;
  document.querySelectorAll('#scenarioSelect [data-mode]').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.mode === state.scenario);
  });
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

  // چیپ باتری: همیشه نمایش؛ بدون داده زنده → --
  const soc = state.batterySoc;
  if (soc == null) {
    document.getElementById('batteryVal').innerHTML = `-- <span class="unit">٪</span>`;
    buildBattery(document.getElementById('batteryIcon'), 0);
  } else {
    document.getElementById('batteryVal').innerHTML = `${Math.round(soc)} <span class="unit">٪</span>`;
    buildBattery(document.getElementById('batteryIcon'), soc);
  }
  // فعلاً دمای مخزن آب شرب؛ بعداً سنسور محیط
  document.getElementById('mainTempVal').innerHTML = `${outlet.temp.toFixed(1)} <span class="unit">°C</span>`;

  updateModeDropdown();

  // مخزن شرب: همیشه از فلوتر (حتی وقتی سیستم خاموش است)
  const productFull = state.inputsKnown ? !!state.inputs.tankFull : false;

  // تانک خام (B): اولویت با فشار زنده بنچ (1.5 / 3.5 bar)؛ وگرنه استنتاج پمپ
  let rawTankFull = state._rawTankUiFull !== false;
  const pBar = state.bench.tankPressureBar;
  if (typeof pBar === 'number') {
    if (pBar >= 3.5) rawTankFull = true;
    else if (pBar <= 1.5) rawTankFull = false;
    // باند میانی: سطح قبلی را نگه می‌دارد (هیسترزیس)
  } else if (state.systemEnabled) {
    rawTankFull = !state.pumps.raw;
  } else if (state.inputsKnown) {
    rawTankFull = !!state.inputs.pressureOk;
  }
  state._rawTankUiFull = rawTankFull;
  const rawTankPct = rawTankFull ? 88 : 10;

  buildSchematicIfChanged(document.getElementById('schematic'), {
    scenario: state.scenario,
    relay1On: state.pumps.raw,
    treatmentOn: state.pumps.treatment,
    systemEnabled: !!state.systemEnabled,
    preFilterPct: 100 - MOCK_VALUES.filterPre,
    membranePct: 100 - MOCK_VALUES.filterMembrane,
    rawTankPct,
    productFull,
    inletTemp: inlet.temp,
    productTemp: outlet.temp,
  });

  const tdsKey = `${outlet.tds.toFixed(0)}|${inlet.tds.toFixed(0)}`;
  if (tdsKey !== state._homeTdsKey) {
    state._homeTdsKey = tdsKey;
    buildRings(document.getElementById('tdsRings'), outlet.tds, inlet.tds);
    const outletColor = zoneColorForValue(outlet.tds, RANGES.tdsOutlet.zones);
    const inletColor = zoneColorForValue(inlet.tds, RANGES.tdsInlet.zones);
    const wrap = document.querySelector('.rings-wrap');
    let center = wrap?.querySelector('.rings-center-label');
    if (!center && wrap) {
      center = document.createElement('div');
      center.className = 'rings-center-label';
      center.innerHTML = `
        <div class="row out"><span class="dot"></span>خروجی <b></b> ppm</div>
        <div class="row in"><span class="dot"></span>ورودی <b></b> ppm</div>`;
      wrap.appendChild(center);
    }
    if (center) {
      const outRow = center.querySelector('.row.out');
      const inRow = center.querySelector('.row.in');
      if (outRow) {
        outRow.querySelector('.dot').style.background = outletColor;
        outRow.querySelector('b').textContent = outlet.tds.toFixed(0);
      }
      if (inRow) {
        inRow.querySelector('.dot').style.background = inletColor;
        inRow.querySelector('b').textContent = inlet.tds.toFixed(0);
      }
    }
  }

  const sideRows = document.getElementById('sideRows');
  const saltRejection = computeSaltRejection();
  const irrPct = liveIrradiancePct();
  const irrShown = irrPct != null ? Math.round(irrPct) : 0;
  const sideKey = [
    saltRejection, MOCK_VALUES.uvHours, irrShown, !!state.hasData, irrPct != null, MOCK_VALUES.volumeLiters,
  ].join('|');
  if (sideKey !== state._homeSideKey) {
    state._homeSideKey = sideKey;
    sideRows.innerHTML = '';
    addGaugeRow(sideRows, { label: 'نرخ دفع املاح', value: saltRejection, range: RANGES.saltRejection, unit: '%', active: state.hasData });
    addGaugeRow(sideRows, { label: 'ساعت UV', value: MOCK_VALUES.uvHours, range: RANGES.uvHours, unit: ' h', active: false });
    addGradientRow(sideRows, {
      label: 'میزان تابش',
      value: irrShown,
      min: 0, max: 100, unit: '%', ledColor: '#f5a300',
      active: irrPct != null,
    });
    sideRows.insertAdjacentHTML('beforeend', `
      <div class="volume-row">
        <span class="volume-label">حجم آب تولیدی</span>
        <div class="volume-box"><b>${MOCK_VALUES.volumeLiters}</b><span>L</span></div>
      </div>
    `);
  }

  renderOpModeBox();
  const alertsEl = document.getElementById('homeAlertsVal');
  if (alertsEl) {
    if (state.fault === 'intake_dry' && state.locked) {
      alertsEl.textContent = 'قفل سخت آب‌گیری (intake_dry) — ریست فیزیکی لازم است';
    } else if (state.locked && state.fault && state.fault !== 'none') {
      alertsEl.textContent = `قفل سیستم: ${state.fault}`;
    } else {
      alertsEl.textContent = 'فعلاً هشداری ثبت نشده';
    }
  }
}

/* ==================== صفحه عملکرد ==================== */
function resolveRawTankFull() {
  let rawTankFull = state._rawTankUiFull !== false;
  const pBar = state.bench.tankPressureBar;
  if (typeof pBar === 'number') {
    if (pBar >= 3.5) rawTankFull = true;
    else if (pBar <= 1.5) rawTankFull = false;
  } else if (state.systemEnabled) {
    rawTankFull = !state.pumps.raw;
  } else if (state.inputsKnown) {
    rawTankFull = !!state.inputs.pressureOk;
  }
  state._rawTankUiFull = rawTankFull;
  return rawTankFull;
}

function statusClassForValue(value, zones) {
  const c = zoneColorForValue(value, zones) || '';
  if (c.includes('red')) return 'bad';
  if (c.includes('yellow')) return 'warn';
  return 'ok';
}

/** ساخت یک‌باره گیج صنعتی؛ آپدیت بعدی فقط pointer/fill/LCD */
function createIndustrialGauge(container, { id, label, range, unit, decimals = 0 }) {
  const { min, max, zones } = range;
  const ticks = 5;
  const labels = [];
  for (let i = 0; i <= ticks; i++) {
    const tv = min + ((max - min) * i) / ticks;
    const danger = statusClassForValue(tv, zones) === 'bad';
    const text = Number.isInteger(min) && Number.isInteger(max) && decimals === 0
      ? String(Math.round(tv))
      : tv.toFixed(decimals > 0 ? decimals : (max <= 10 ? 1 : 0));
    labels.push(`<span class="${danger ? 'danger' : ''}">${text}</span>`);
  }
  const segs = zones.map(z => {
    const w = ((z.to - z.from) / (max - min)) * 100;
    return `<div class="seg" style="width:${w}%;background:${z.color}"></div>`;
  }).join('');

  const el = document.createElement('div');
  el.className = 'ind-gauge';
  el.dataset.gaugeId = id;
  el.innerHTML = `
    <div class="ind-gauge-title">${label}</div>
    <div class="ind-gauge-main">
      <div class="ind-gauge-scale-wrap">
        <div class="ind-scale-labels">${labels.join('')}</div>
        <div class="ind-track">
          <div class="ind-track-segs">${segs}</div>
          <div class="ind-fill" style="width:0%"></div>
          <div class="ind-pointer" style="left:0%"></div>
        </div>
      </div>
      <div class="ind-lcd-col">
        <div class="ind-lcd">--<span class="unit">${unit || ''}</span></div>
        <div class="ind-status">Status <span class="ind-status-pill"></span></div>
      </div>
    </div>`;
  container.appendChild(el);
  return el;
}

function updateIndustrialGauge(el, { value, range, unit, active, decimals = 0 }) {
  if (!el) return;
  const { min, max, zones } = range;
  const v = Number(value);
  const safe = Number.isFinite(v) ? Math.max(min, Math.min(max, v)) : min;
  const pct = ((safe - min) / (max - min)) * 100;
  el.classList.toggle('disabled', !active);
  const fill = el.querySelector('.ind-fill');
  const pointer = el.querySelector('.ind-pointer');
  const lcd = el.querySelector('.ind-lcd');
  const pill = el.querySelector('.ind-status-pill');
  if (fill) fill.style.width = pct + '%';
  if (pointer) pointer.style.left = pct + '%';
  if (lcd) {
    const num = Number.isFinite(v) ? v.toFixed(decimals) : '--';
    lcd.innerHTML = `${num}<span class="unit">${unit || ''}</span>`;
  }
  if (pill) {
    pill.className = 'ind-status-pill ' + (Number.isFinite(v) ? statusClassForValue(v, zones) : '');
  }
}

function upsertBinaryRow(container, { id, label, text, pillClass, active }) {
  let row = container.querySelector(`[data-row-id="${id}"]`);
  if (!row) {
    row = document.createElement('div');
    row.dataset.rowId = id;
    row.innerHTML = `<span class="label"></span><span class="status-pill"></span>`;
    container.appendChild(row);
  }
  row.className = 'binary-level-row' + (active ? '' : ' disabled');
  row.querySelector('.label').textContent = label;
  const pill = row.querySelector('.status-pill');
  pill.className = 'status-pill ' + (pillClass || '');
  pill.textContent = text;
}

let _perfBuilt = false;
function ensurePerformanceScaffold() {
  const container = document.getElementById('perf-rows');
  if (!container || _perfBuilt) return container;
  const gauges = [
    { id: 'tankPressure', label: 'فشار منبع (تانک ۴۰L)', range: RANGES.tankPressure, unit: 'bar', decimals: 2 },
    { id: 'irradiance', label: 'میزان تابش', range: RANGES.irradiance, unit: '%', decimals: 0 },
    { id: 'panelVoltage', label: 'ولتاژ پنل', range: RANGES.panelVoltage, unit: 'V', decimals: 1 },
    { id: 'filterPre', label: 'فیلتر پیش‌تصفیه (استفاده‌شده)', range: RANGES.filterUsed, unit: '%', decimals: 0 },
    { id: 'filterMembrane', label: 'فیلتر ممبران (استفاده‌شده)', range: RANGES.filterUsed, unit: '%', decimals: 0 },
    { id: 'uvHours', label: 'ساعت کارکرد لامپ UV', range: RANGES.uvHours, unit: 'h', decimals: 0 },
    { id: 'productTemp', label: 'دمای آب شرب', range: RANGES.productTemp, unit: '°C', decimals: 1 },
    { id: 'ambientTemp', label: 'دمای محیط', range: RANGES.ambientTemp, unit: '°C', decimals: 1 },
  ];
  gauges.forEach(g => createIndustrialGauge(container, g));
  _perfBuilt = true;
  return container;
}

function renderPerformancePage() {
  const container = ensurePerformanceScaffold();
  if (!container) return;

  const productFull = state.inputsKnown ? !!state.inputs.tankFull : false;
  const rawFull = resolveRawTankFull();
  const irr = liveIrradiancePct();
  const vSolar = typeof state.bench.vSolar === 'number' ? state.bench.vSolar : null;
  const pBar = typeof state.bench.tankPressureBar === 'number' ? state.bench.tankPressureBar : null;

  const byId = id => container.querySelector(`[data-gauge-id="${id}"]`);
  updateIndustrialGauge(byId('tankPressure'), {
    value: pBar ?? 0, range: RANGES.tankPressure, unit: 'bar', active: pBar != null, decimals: 2,
  });
  updateIndustrialGauge(byId('irradiance'), {
    value: irr ?? 0, range: RANGES.irradiance, unit: '%', active: irr != null, decimals: 0,
  });
  updateIndustrialGauge(byId('panelVoltage'), {
    value: vSolar ?? 0, range: RANGES.panelVoltage, unit: 'V', active: vSolar != null, decimals: 1,
  });
  updateIndustrialGauge(byId('filterPre'), {
    value: MOCK_VALUES.filterPre, range: RANGES.filterUsed, unit: '%', active: true, decimals: 0,
  });
  updateIndustrialGauge(byId('filterMembrane'), {
    value: MOCK_VALUES.filterMembrane, range: RANGES.filterUsed, unit: '%', active: true, decimals: 0,
  });
  updateIndustrialGauge(byId('uvHours'), {
    value: MOCK_VALUES.uvHours, range: RANGES.uvHours, unit: 'h', active: true, decimals: 0,
  });
  updateIndustrialGauge(byId('productTemp'), {
    value: state.tds.outlet.temp, range: RANGES.productTemp, unit: '°C', active: state.hasData, decimals: 1,
  });
  updateIndustrialGauge(byId('ambientTemp'), {
    value: MOCK_VALUES.ambientTemp, range: RANGES.ambientTemp, unit: '°C', active: false, decimals: 1,
  });

  upsertBinaryRow(container, {
    id: 'leak', label: 'وضعیت سنسور نشتی',
    text: state.inputs.leak ? 'نشتی!' : 'بدون نشتی',
    pillClass: state.inputs.leak ? 'danger' : 'on',
    active: state.inputsKnown,
  });
  upsertBinaryRow(container, {
    id: 'rawTank', label: 'سطح مخزن آب خام',
    text: rawFull ? 'پر' : 'خالی',
    pillClass: rawFull ? 'on' : 'danger',
    active: state.inputsKnown || pBar != null || state.pumpsKnown,
  });
  upsertBinaryRow(container, {
    id: 'productTank', label: 'سطح مخزن آب شرب',
    text: productFull ? 'پر' : 'خالی',
    pillClass: productFull ? 'on' : 'danger',
    active: state.inputsKnown,
  });
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
  const irr = liveIrradiancePct();
  const vSolar = typeof state.bench.vSolar === 'number' ? state.bench.vSolar : 0;
  const pBar = typeof state.bench.tankPressureBar === 'number' ? state.bench.tankPressureBar : 0;
  return [
    { label: 'TDS خروجی (آب شرب)', value: state.tds.outlet.tds, unit: ' ppm', ...RANGES.tdsOutlet },
    { label: 'TDS ورودی (آب خام)', value: state.tds.inlet.tds, unit: ' ppm', ...RANGES.tdsInlet },
    { label: 'نرخ دفع املاح', value: computeSaltRejection(), unit: '%', ...RANGES.saltRejection },
    { label: 'فشار منبع', value: pBar, unit: ' bar', ...RANGES.tankPressure },
    { label: 'میزان تابش', value: irr != null ? Math.round(irr) : 0, unit: '%', ...RANGES.irradiance },
    { label: 'ولتاژ پنل', value: vSolar, unit: ' V', ...RANGES.panelVoltage },
    { label: 'دمای آب شرب', value: state.tds.outlet.temp, unit: '°C', ...RANGES.productTemp },
    { label: 'فیلتر پیش‌تصفیه (ظرفیت استفاده‌شده)', value: MOCK_VALUES.filterPre, unit: '%', ...RANGES.filterUsed },
    { label: 'فیلتر ممبران (ظرفیت استفاده‌شده)', value: MOCK_VALUES.filterMembrane, unit: '%', ...RANGES.filterUsed },
    { label: 'ساعت کارکرد لامپ UV', value: MOCK_VALUES.uvHours, unit: ' h', ...RANGES.uvHours },
    { label: 'حجم آب تولیدی', value: MOCK_VALUES.volumeLiters, unit: ' L', min: 0, max: 2000, zones: [{ from: 0, to: 2000, color: 'var(--zone-green)' }] },
  ];
}
function getReportStatusRows() {
  return [
    { label: 'مخزن آب شرب', on: !!state.inputs.tankFull },
    { label: 'مخزن آب خام', on: resolveRawTankFull() },
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
  if (id === 'settingsCalibTds' || id === 'settingsCalibPressure' ||
      id === 'settingsCalibVsolar' || id === 'settingsCalibAmbient' ||
      id === 'settingsDateTime') {
    refreshCalibLiveReadouts();
  }
}

function refreshCalibLiveReadouts() {
  const setTxt = (id, t) => { const el = document.getElementById(id); if (el) el.textContent = t; };
  setTxt('calibCurrentTemp1', `کنونی: ${Number(state.tds.inlet.temp).toFixed(1)}°C`);
  setTxt('calibCurrentEc1', `کنونی: ${Number(state.tds.inlet.ec).toFixed(0)} µS/cm`);
  setTxt('calibCurrentTemp2', `کنونی: ${Number(state.tds.outlet.temp).toFixed(1)}°C`);
  setTxt('calibCurrentEc2', `کنونی: ${Number(state.tds.outlet.ec).toFixed(0)} µS/cm`);
  const pBar = state.bench.tankPressureBar;
  setTxt('calibCurrentPressure', pBar == null ? 'کنونی: --' : `کنونی: ${Number(pBar).toFixed(2)} bar`);
  const vS = state.bench.vSolar;
  setTxt('calibCurrentVsolar', vS == null ? 'کنونی: --' : `کنونی: ${Number(vS).toFixed(1)} V`);
  setTxt('calibCurrentAmbient', `کنونی: ${Number(MOCK_VALUES.ambientTemp).toFixed(1)}°C (نمایشی)`);
  if (typeof state.deviceEpoch === 'number' && state.deviceEpoch > 0) {
    setTxt('deviceTimeVal', new Date(state.deviceEpoch * 1000).toLocaleString('fa-IR'));
  } else {
    setTxt('deviceTimeVal', 'تنظیم نشده');
  }
}

document.getElementById('btnCalibration').addEventListener('click', () => {
  showSettingsView('settingsCalibMenu');
});
document.getElementById('btnDateTime').addEventListener('click', () => {
  const now = new Date();
  const dateEl = document.getElementById('dtDate');
  const timeEl = document.getElementById('dtTime');
  if (dateEl) dateEl.value = now.toISOString().slice(0, 10);
  if (timeEl) timeEl.value = now.toTimeString().slice(0, 5);
  const dtRes = document.getElementById('dtResult');
  if (dtRes) dtRes.hidden = true;
  showSettingsView('settingsDateTime');
});

document.querySelectorAll('.back-btn[data-back]').forEach(btn => {
  btn.addEventListener('click', () => showSettingsView(btn.dataset.back));
});

const CALIB_VIEWS = {
  tds: 'settingsCalibTds',
  pressure: 'settingsCalibPressure',
  vsolar: 'settingsCalibVsolar',
  ambient: 'settingsCalibAmbient',
};
document.querySelectorAll('.calib-menu-item[data-calib]').forEach(btn => {
  btn.addEventListener('click', () => {
    const view = CALIB_VIEWS[btn.dataset.calib];
    if (view) showSettingsView(view);
  });
});

const CALIB_META = {
  temp1: { channel: 1, cmd: 'calibrate_temp', label: 'دما - کانال ۱', unit: '°C' },
  ec1:   { channel: 1, cmd: 'calibrate_ec',   label: 'EC - کانال ۱', unit: 'µS/cm' },
  temp2: { channel: 2, cmd: 'calibrate_temp', label: 'دما - کانال ۲', unit: '°C' },
  ec2:   { channel: 2, cmd: 'calibrate_ec',   label: 'EC - کانال ۲', unit: 'µS/cm' },
};

document.getElementById('calibTdsOkBtn').addEventListener('click', () => {
  const root = document.getElementById('settingsCalibTds');
  const entered = [...root.querySelectorAll('.calib-input')].filter(i => i.value.trim() !== '');
  const resultBox = document.getElementById('calibTdsResult');
  if (entered.length === 0) {
    resultBox.innerHTML = '<div>هیچ مقداری وارد نشد.</div>';
  } else {
    resultBox.innerHTML = entered.map(i => {
      const meta = CALIB_META[i.dataset.key];
      sendCommand({ cmd: meta.cmd, channel: meta.channel, value: parseFloat(i.value) });
      return `<div id="calib-status-${meta.channel}-${meta.cmd}">در حال کالیبراسیون «${meta.label}»...</div>`;
    }).join('');
    entered.forEach(i => { i.value = ''; });
  }
  resultBox.hidden = false;
});

document.getElementById('calibPressureOkBtn').addEventListener('click', () => {
  const input = document.getElementById('calibPressureRef');
  const resultBox = document.getElementById('calibPressureResult');
  const v = parseFloat(input.value);
  if (!Number.isFinite(v)) {
    resultBox.textContent = 'مقدار فشار مرجع معتبر نیست.';
    resultBox.hidden = false;
    return;
  }
  sendCommand({ cmd: 'calibrate_pressure', value: v });
  resultBox.innerHTML = `<div id="calib-status-0-calibrate_pressure">در حال کالیبراسیون فشار با ${v.toFixed(2)} bar...</div>`;
  resultBox.hidden = false;
  input.value = '';
});

document.getElementById('calibVsolarOkBtn').addEventListener('click', () => {
  const input = document.getElementById('calibVsolarRef');
  const resultBox = document.getElementById('calibVsolarResult');
  const v = parseFloat(input.value);
  if (!Number.isFinite(v)) {
    resultBox.textContent = 'مقدار ولتاژ مرجع معتبر نیست.';
    resultBox.hidden = false;
    return;
  }
  sendCommand({ cmd: 'calibrate_vsolar', value: v });
  resultBox.innerHTML = `<div id="calib-status-0-calibrate_vsolar">در حال کالیبراسیون ولتاژ با ${v.toFixed(1)} V...</div>`;
  resultBox.hidden = false;
  input.value = '';
});

document.getElementById('dtApplyBtn').addEventListener('click', () => {
  const dateEl = document.getElementById('dtDate');
  const timeEl = document.getElementById('dtTime');
  const resultBox = document.getElementById('dtResult');
  if (!dateEl.value || !timeEl.value) {
    resultBox.textContent = 'تاریخ و ساعت را کامل وارد کنید.';
    resultBox.hidden = false;
    return;
  }
  const local = new Date(`${dateEl.value}T${timeEl.value}:00`);
  const epoch = Math.floor(local.getTime() / 1000);
  if (!Number.isFinite(epoch)) {
    resultBox.textContent = 'تاریخ/ساعت نامعتبر است.';
    resultBox.hidden = false;
    return;
  }
  sendCommand({ cmd: 'set_time', epoch });
  resultBox.innerHTML = '<div id="calib-status-0-calibrate_time">در حال تنظیم ساعت برد...</div>';
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
  if (state.locked) {
    showToast('سیستم قفل است — برق برد را قطع/وصل کنید');
    setToggleVisual(powerToggle, false);
    state.systemEnabled = false;
    return;
  }
  const turningOn = !powerToggle.classList.contains('on');
  setToggleVisual(powerToggle, turningOn);
  state.systemEnabled = turningOn;
  state._powerCmdUntil = Date.now() + 1500;
  sendCommand({ cmd: 'power', on: turningOn });
});

function syncPowerToggles() {
  if (Date.now() < state._powerCmdUntil) return;
  setToggleVisual(powerToggle, !!state.systemEnabled);
}

function asBool(v) {
  return v === true || v === 1 || v === 'true' || v === '1';
}

/** عدد زنده از WS — عدد یا رشته‌ی عددی */
function asFiniteNumber(v) {
  if (typeof v === 'number' && Number.isFinite(v)) return v;
  if (typeof v === 'string' && v.trim() !== '') {
    const n = Number(v);
    if (Number.isFinite(n)) return n;
  }
  return null;
}

/** فشار منبع: top-level / bench / inputs / یا از ADC */
function ingestTankPressure(data) {
  const adc = asFiniteNumber(data.pressureAdc)
    ?? (data.bench ? asFiniteNumber(data.bench.pressureAdc) : null);
  const bar = asFiniteNumber(data.tankPressureBar)
    ?? (data.bench ? asFiniteNumber(data.bench.tankPressureBar) : null)
    ?? (data.inputs ? asFiniteNumber(data.inputs.tankPressureBar) : null);

  if (adc != null) state.bench.pressureAdc = adc;
  if (bar != null) {
    state.bench.tankPressureBar = bar;
    state.inputs.tankPressureBar = bar;
  } else if (adc != null) {
    // اگر فقط ADC آمد، bar را از همان مقیاس بنچ بساز (0–3.3V → 0–5 bar)
    const mapped = (adc / 3.3) * 5;
    state.bench.tankPressureBar = mapped;
    state.inputs.tankPressureBar = mapped;
  }
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
let confirmKind = null; // 'filter' | 'intake_wait'

function openConfirm(key) {
  pendingFilterKey = key;
  confirmKind = 'filter';
  const f = filters.find(x => x.key === key);
  document.getElementById('confirmText').textContent = `آیا از تعویض‌شدن «${f.label}» مطمئن هستید؟`;
  document.getElementById('confirmModal').hidden = false;
}

function openIntakeWaitResetConfirm() {
  if (state.fault === 'intake_dry' && state.locked) return;
  confirmKind = 'intake_wait';
  document.getElementById('confirmText').textContent =
    'آیا مطمئنید می‌خواهید وقفه ۳۰ دقیقه‌ای را ریست کنید؟';
  document.getElementById('confirmModal').hidden = false;
}

document.getElementById('btnResetIntakeWait')?.addEventListener('click', openIntakeWaitResetConfirm);

document.getElementById('confirmNo').addEventListener('click', () => {
  document.getElementById('confirmModal').hidden = true;
  confirmKind = null;
});
document.getElementById('confirmYes').addEventListener('click', () => {
  document.getElementById('confirmModal').hidden = true;
  if (confirmKind === 'intake_wait') {
    sendCommand({ cmd: 'reset_intake_wait' });
    showToast('درخواست ریست وقفه آب‌گیری ارسال شد');
  } else if (confirmKind === 'filter' && pendingFilterKey) {
    const f = filters.find(x => x.key === pendingFilterKey);
    if (f) {
      f.usedPct = 0;
      document.getElementById('filterModal').hidden = true;
      showToast(`«${f.label}» تعویض و صفر شد`);
    }
  }
  confirmKind = null;
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
  if (name === 'settings') {
    showSettingsView('settingsMain');
    syncPowerToggles();
  }
  if (name === 'alerts') renderAlertsPage();
}
document.querySelectorAll('.nav-item[data-page]').forEach(btn => {
  btn.addEventListener('click', () => showPage(btn.dataset.page));
});

// تغییر سناریو A/B فقط از صفحه تنظیمات؛ هدر فقط نمایش است
(function setupScenarioSelect() {
  const box = document.getElementById('scenarioSelect');
  if (!box) return;
  box.querySelectorAll('[data-mode]').forEach(opt => {
    opt.addEventListener('click', () => {
      if (opt.dataset.mode === state.scenario) return;
      state.scenario = opt.dataset.mode;
      sendCommand({ cmd: 'scenario', mode: state.scenario });
      updateModeDropdown();
      if (document.querySelector('.nav-item.active[data-page]')?.dataset.page === 'home') {
        renderHomePage();
      }
      showToast(SCENARIO_LABELS[state.scenario] || 'سناریو تغییر کرد');
    });
  });
})();

/* ==================== اتصال WebSocket به ESP32 ==================== */
let socket = null;
let _wsReconnectTimer = null;
let _uiRaf = 0;
let _uiDirty = false;

function activePageName() {
  return document.querySelector('.nav-item.active[data-page]')?.dataset.page || 'home';
}

/** یک فریم رندر برای چند پکت — جلوگیری از jank روی موبایل */
function scheduleUiPaint() {
  _uiDirty = true;
  if (_uiRaf) return;
  _uiRaf = requestAnimationFrame(() => {
    _uiRaf = 0;
    if (!_uiDirty) return;
    _uiDirty = false;
    updateModeDropdown();
    const page = activePageName();
    if (page === 'home') renderHomePage();
    else if (page === 'performance') renderPerformancePage();
    else if (page === 'settings') {
      syncPowerToggles();
      const activeCalib = document.querySelector(
        '#settingsCalibTds.active, #settingsCalibPressure.active, #settingsCalibVsolar.active, #settingsCalibAmbient.active, #settingsDateTime.active'
      );
      if (activeCalib) refreshCalibLiveReadouts();
    }
  });
}

function sendCommand(obj) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(obj));
  }
}

/** Soft RTC: phone → ESP on each WebSocket connect (lost only on power cut). */
function syncDeviceClockFromPhone() {
  const epoch = Math.floor(Date.now() / 1000);
  if (!Number.isFinite(epoch) || epoch < 1700000000) return;
  sendCommand({ cmd: 'set_time', epoch, auto: true });
}

function applyWsPayload(data) {
  if (data.tds1 || data.tds2) {
    if (data.tds1) Object.assign(state.tds.inlet, data.tds1);
    if (data.tds2) Object.assign(state.tds.outlet, data.tds2);
    state.hasData = true;
  }

  if ('systemEnabled' in data) {
    if (Date.now() >= state._powerCmdUntil) {
      state.systemEnabled = asBool(data.systemEnabled);
    }
  }

  if ('locked' in data) state.locked = asBool(data.locked);
  if (typeof data.fault === 'string') state.fault = data.fault;
  if (typeof data.dryRunRetries === 'number') state.dryRunRetries = data.dryRunRetries;
  if (typeof data.intakeRawFails === 'number') state.intakeRawFails = data.intakeRawFails;

  if (data.inputs) {
    state.inputs.pressureOk = asBool(data.inputs.pressureOk);
    state.inputs.tankFull = asBool(data.inputs.tankFull);
    state.inputs.leak = asBool(data.inputs.leak);
    state.inputsKnown = true;
  }

  if (typeof data.opMode === 'string') state.opMode = data.opMode;
  if (typeof data.opModeLabel === 'string') state.opModeLabel = data.opModeLabel;
  if (typeof data.standbyReason === 'string') state.standbyReason = data.standbyReason;
  if ('nightLight' in data) state.nightLight = asBool(data.nightLight);
  if ('intakeWaitActive' in data) state.intakeWaitActive = asBool(data.intakeWaitActive);
  if (typeof data.intakeWaitSec === 'number') state.intakeWaitSec = data.intakeWaitSec;
  if (typeof data.intakeWaitMs === 'number') state.intakeWaitMs = data.intakeWaitMs;

  const vSolar = asFiniteNumber(data.vSolar)
    ?? (data.bench ? asFiniteNumber(data.bench.vSolar) : null);
  const irr = asFiniteNumber(data.irradiancePct);
  const soc = asFiniteNumber(data.soc);
  const vAdc = asFiniteNumber(data.vSolarAdc)
    ?? (data.bench ? asFiniteNumber(data.bench.vSolarAdc) : null);
  if (vSolar != null) state.bench.vSolar = vSolar;
  if (irr != null) state.bench.irradiancePct = irr;
  if (soc != null) state.batterySoc = soc;
  if (vAdc != null) state.bench.vSolarAdc = vAdc;
  if (data.bench && 'enabled' in data.bench) state.bench.enabled = asBool(data.bench.enabled);
  ingestTankPressure(data);

  if (data.relays) {
    state.relays.r1 = asBool(data.relays.r1);
    state.relays.r2 = asBool(data.relays.r2);
    state.relays.purify = asBool(data.relays.purify);
    state.relays.night = asBool(data.relays.night);
    if (!('nightLight' in data)) state.nightLight = state.relays.night;
  }

  if (typeof data.scenario === 'string') {
    if (data.scenario.indexOf('A') >= 0) state.scenario = 'A';
    else if (data.scenario.indexOf('B') >= 0) state.scenario = 'B';
  }
  if (typeof data.routine === 'string') {
    const map = {
      idle: 'انتظار',
      intake: 'آب‌گیری',
      purifying: 'تصفیه',
      dry_run_wait: 'انتظار خطای فشار',
      locked: 'قفل سیستم',
    };
    state.activeRoutine = map[data.routine] || data.routine;
  }

  if (data.pumps) {
    state.pumps.treatment = asBool(data.pumps.treatment);
    state.pumps.uv = asBool(data.pumps.uv);
    state.pumps.raw = asBool(data.pumps.raw);
    state.pumpsKnown = true;
  }

  const epoch = asFiniteNumber(data.epoch);
  if (epoch != null) state.deviceEpoch = epoch;

  if (data.calibResult) {
    const { type, channel, ok } = data.calibResult;
    const labels = {
      ec: 'EC',
      temp: 'دما',
      pressure: 'فشار',
      vsolar: 'ولتاژ پنل',
      time: 'تاریخ و ساعت',
    };
    const meta = Object.values(CALIB_META).find(m =>
      m.channel === channel && m.cmd === (type === 'ec' ? 'calibrate_ec' : 'calibrate_temp'));
    const label = meta ? meta.label : (labels[type] || type);
    const statusId = type === 'pressure' ? 'calib-status-0-calibrate_pressure'
      : type === 'vsolar' ? 'calib-status-0-calibrate_vsolar'
      : type === 'time' ? 'calib-status-0-calibrate_time'
      : `calib-status-${channel}-calibrate_${type}`;
    const statusEl = document.getElementById(statusId);
    if (statusEl) statusEl.textContent = ok ? `«${label}» موفق بود` : `«${label}» ناموفق بود`;
    showToast(ok ? `کالیبراسیون «${label}» موفق بود` : `کالیبراسیون «${label}» ناموفق بود`);
  }
}

function connectWS() {
  if (_wsReconnectTimer) {
    clearTimeout(_wsReconnectTimer);
    _wsReconnectTimer = null;
  }
  if (socket) {
    try { socket.onclose = null; socket.close(); } catch (_) { /* ignore */ }
    socket = null;
  }

  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${proto}://${location.host}/ws`);
  const dot = document.getElementById('conn-dot');
  const app = document.getElementById('app');

  socket.onopen = () => {
    dot.classList.add('connected');
    app.classList.remove('offline');
    syncDeviceClockFromPhone();
  };
  socket.onclose = () => {
    dot.classList.remove('connected');
    app.classList.add('offline');
    if (!_wsReconnectTimer) {
      _wsReconnectTimer = setTimeout(() => {
        _wsReconnectTimer = null;
        connectWS();
      }, 2000);
    }
  };
  socket.onerror = () => { try { socket.close(); } catch (_) { /* ignore */ } };

  socket.onmessage = (evt) => {
    let data;
    try { data = JSON.parse(evt.data); } catch (e) { return; }
    applyWsPayload(data);
    scheduleUiPaint();
  };
}

showPage('home');
document.getElementById('app').classList.add('offline'); // تا وصل نشدیم، آفلاین نمایش داده شود
connectWS();

if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => navigator.serviceWorker.register('sw.js'));
}
