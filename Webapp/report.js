// ===== ماژول تولید گزارش PDF =====
// نکته فنی مهم: کتابخانه jsPDF فونت‌های داخلی‌اش فارسی/عربی را درست (با اتصال حروف
// و راست‌به‌چپ) رسم نمی‌کند. برای همین هر متن فارسی را اول با فونت خود مرورگر (که
// شکل‌دهی حروف را درست انجام می‌دهد) روی یک canvas می‌کشیم و بعد آن canvas را به‌عنوان
// تصویر داخل PDF می‌گذاریم. المان‌های گرافیکی (نوار گیج، دایره وضعیت) مستقیم با
// دستورات ترسیم jsPDF کشیده می‌شوند چون به فونت نیازی ندارند.

const REPORT_PX_PER_MM = 10; // نسبت تبدیل پیکسل canvas به میلی‌متر PDF

function textToImage(text, { font = '600 34px Tahoma, Arial', color = '#1c2b33', paddingPx = 6 } = {}) {
  const canvas = document.createElement('canvas');
  const ctx = canvas.getContext('2d');
  ctx.font = font;
  const metrics = ctx.measureText(text);
  const textWidth = Math.ceil(metrics.width);
  const textHeight = Math.ceil((metrics.fontBoundingBoxAscent || 34) + (metrics.fontBoundingBoxDescent || 10));

  const scale = 2; // برای وضوح بیشتر در PDF
  canvas.width = (textWidth + paddingPx * 2) * scale;
  canvas.height = (textHeight + paddingPx * 2) * scale;
  ctx.scale(scale, scale);
  ctx.font = font;
  ctx.fillStyle = color;
  ctx.textBaseline = 'top';
  ctx.direction = 'rtl';
  ctx.textAlign = 'right';
  ctx.fillText(text, textWidth + paddingPx, paddingPx);

  return {
    dataUrl: canvas.toDataURL('image/png'),
    widthMM: canvas.width / scale / REPORT_PX_PER_MM,
    heightMM: canvas.height / scale / REPORT_PX_PER_MM,
  };
}

// متن را در PDF می‌کشد؛ x = لبه راست متن (چون فارسی راست‌چین است)
function drawText(doc, text, xRight, y, opts) {
  const img = textToImage(text, opts);
  doc.addImage(img.dataUrl, 'PNG', xRight - img.widthMM, y, img.widthMM, img.heightMM);
  return img.heightMM;
}

function zoneColorHex(value, zones) {
  for (const z of zones) {
    if (value < z.to || z === zones[zones.length - 1]) {
      // z.color اینجا var(--zone-x) است؛ برای PDF مستقیم hex لازم داریم
      if (z.color.includes('green')) return '#4caf50';
      if (z.color.includes('yellow')) return '#f4c430';
      return '#e5484d';
    }
  }
  return '#4caf50';
}

// یک ردیف پارامتر: برچسب + نوار گیج رنگی + مقدار عددی
function drawParamRow(doc, { label, value, unit, min, max, zones }, x, y, width) {
  const labelH = drawText(doc, label, x + width, y, { font: '600 30px Tahoma, Arial', paddingPx: 4 });
  const barY = y + labelH + 1.5;
  const barH = 4;
  const barX = x;
  const barW = width * 0.62;

  doc.setFillColor(238, 242, 243);
  doc.roundedRect(barX, barY, barW, barH, 1, 1, 'F');

  const frac = Math.max(0, Math.min(1, (value - min) / (max - min)));
  const color = zoneColorHex(value, zones);
  const [r, g, b] = hexToRgb(color);
  doc.setFillColor(r, g, b);
  if (frac > 0) doc.roundedRect(barX, barY, barW * frac, barH, 1, 1, 'F');

  const valueImg = textToImage(`${value}${unit || ''}`, { font: '700 30px Tahoma, Arial', color, paddingPx: 4 });
  doc.addImage(valueImg.dataUrl, 'PNG', barX + barW + 3, barY - 1.3, valueImg.widthMM, valueImg.heightMM);

  return barY + barH + 5;
}

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function drawHeader(doc, logoDataUrl, title) {
  const pageW = doc.internal.pageSize.getWidth();
  if (logoDataUrl) doc.addImage(logoDataUrl, 'PNG', 14, 10, 52, 52);

  drawText(doc, title, pageW - 14, 22, { font: '700 40px Tahoma, Arial' });
  // این تاریخ/ساعت لحظه خروجی‌گرفتن (از ساعت مرورگر) است، نه ساعت خود دستگاه -
  // دستگاه ساعت واقعی ندارد، برای همین به هشدارها زمان نسبت داده نمی‌شود.
  const now = new Date();
  const dateStr = now.toLocaleDateString('fa-IR') + ' - ' + now.toLocaleTimeString('fa-IR', { hour: '2-digit', minute: '2-digit' });
  drawText(doc, dateStr, pageW - 14, 34, { font: '400 22px Tahoma, Arial', color: '#8a9aa2' });

  doc.setDrawColor(226, 236, 239);
  doc.setLineWidth(0.4);
  doc.line(14, 68, pageW - 14, 68);
  return 76;
}

function drawFooter(doc) {
  const pageW = doc.internal.pageSize.getWidth();
  const pageH = doc.internal.pageSize.getHeight();
  doc.setDrawColor(226, 236, 239);
  doc.line(14, pageH - 16, pageW - 14, pageH - 16);
  drawText(doc, 'نیکسان توان — کنترلر سیستم تصفیه آب خورشیدی', pageW - 14, pageH - 13, { font: '400 20px Tahoma, Arial', color: '#8a9aa2' });
}

function severityLabel(sev) {
  return sev === 'critical' ? 'بحرانی' : sev === 'warning' ? 'هشدار' : 'اطلاع';
}
function severityColorHex(sev) {
  return sev === 'critical' ? '#e5484d' : sev === 'warning' ? '#f4c430' : '#0f7fa0';
}

function drawAlertsSection(doc, alerts, x, y, width, compact) {
  if (alerts.length === 0) {
    y += drawText(doc, 'هشداری ثبت نشده است.', x + width, y, { font: '400 26px Tahoma, Arial', color: '#8a9aa2' }) + 3;
    return y;
  }
  for (const a of alerts) {
    const rowH = compact ? 10 : 14;
    const [r, g, b] = hexToRgb(severityColorHex(a.severity));
    doc.setFillColor(r, g, b);
    doc.circle(x + width - 2, y + 4, 1.4, 'F');

    drawText(doc, a.title, x + width - 6, y, { font: '600 26px Tahoma, Arial' });
    drawText(doc, severityLabel(a.severity), x + width - 6, y + 6.5, { font: '400 20px Tahoma, Arial', color: '#8a9aa2' });

    doc.setDrawColor(240, 240, 240);
    doc.line(x, y + rowH - 2, x + width, y + rowH - 2);
    y += rowH;
  }
  return y;
}

/* ==================== محتوای نمونه (برای تست مستقل این فایل) ====================
   وقتی این ماژول در برنامه اصلی ادغام شود، این بخش با state/RANGES واقعی از app.js
   جایگزین می‌شود. هر پارامتری که به سیستم اضافه/حذف شود باید همین‌جا هم به‌روزرسانی شود. */
const DEMO_PARAMS = [
  { label: 'TDS خروجی (آب شرب)', value: 50, unit: ' ppm', min: 3, max: 200, zones: [{ to: 100, color: 'green' }, { to: 150, color: 'yellow' }, { to: 200, color: 'red' }] },
  { label: 'TDS ورودی (آب خام)', value: 640, unit: ' ppm', min: 100, max: 3000, zones: [{ to: 1000, color: 'green' }, { to: 2000, color: 'yellow' }, { to: 3000, color: 'red' }] },
  { label: 'نرخ دفع املاح', value: 92, unit: '%', min: 0, max: 100, zones: [{ to: 85, color: 'red' }, { to: 90, color: 'yellow' }, { to: 100, color: 'green' }] },
  { label: 'دمای آب شرب', value: 25.3, unit: '°C', min: -5, max: 80, zones: [{ to: 1, color: 'red' }, { to: 5, color: 'yellow' }, { to: 45, color: 'green' }, { to: 50, color: 'yellow' }, { to: 80, color: 'red' }] },
  { label: 'دمای آب خام', value: 26.5, unit: '°C', min: -5, max: 80, zones: [{ to: 1, color: 'red' }, { to: 5, color: 'yellow' }, { to: 45, color: 'green' }, { to: 50, color: 'yellow' }, { to: 80, color: 'red' }] },
  { label: 'فیلتر پیش‌تصفیه (ظرفیت استفاده‌شده)', value: 18, unit: '%', min: 0, max: 100, zones: [{ to: 60, color: 'green' }, { to: 85, color: 'yellow' }, { to: 100, color: 'red' }] },
  { label: 'فیلتر ممبران (ظرفیت استفاده‌شده)', value: 43, unit: '%', min: 0, max: 100, zones: [{ to: 60, color: 'green' }, { to: 85, color: 'yellow' }, { to: 100, color: 'red' }] },
  { label: 'ساعت کارکرد لامپ UV', value: 1200, unit: ' h', min: 0, max: 9000, zones: [{ to: 6000, color: 'green' }, { to: 8000, color: 'yellow' }, { to: 9000, color: 'red' }] },
  { label: 'حجم آب تولیدی', value: 486, unit: ' L', min: 0, max: 2000, zones: [{ to: 2000, color: 'green' }] },
];
const DEMO_STATUS = [
  { label: 'وضعیت پمپ تصفیه', on: true },
  { label: 'وضعیت پمپ آب خام', on: false },
];
const DEMO_ALERTS = [
  { severity: 'warning', title: 'گرفتگی نسبی فیلتر پیش‌تصفیه' },
  { severity: 'info', title: 'کالیبراسیون کانال ۲ TDS انجام شد' },
];

async function generateParametersReport(params = DEMO_PARAMS, statusRows = DEMO_STATUS, alerts = DEMO_ALERTS) {
  const { jsPDF } = window.jspdf;
  const doc = new jsPDF({ unit: 'mm', format: 'a4' });
  const pageW = doc.internal.pageSize.getWidth();
  const contentX = 14, contentW = pageW - 28;

  let y = drawHeader(doc, typeof LOGO_DATA_URL !== 'undefined' ? LOGO_DATA_URL : null, 'گزارش پارامترهای سیستم');

  for (const p of params) {
    y = drawParamRow(doc, p, contentX, y, contentW);
  }

  y += 4;
  for (const s of statusRows) {
    const color = s.on ? '#4caf50' : '#b0bec5';
    const [r, g, b] = hexToRgb(color);
    doc.setFillColor(r, g, b);
    doc.circle(contentX + contentW - 2, y + 3, 1.4, 'F');
    drawText(doc, `${s.label}: ${s.on ? 'روشن' : 'خاموش'}`, contentX + contentW - 6, y, { font: '600 26px Tahoma, Arial' });
    y += 9;
  }

  y += 4;
  y += drawText(doc, 'هشدارهای اخیر', contentX + contentW, y, { font: '700 32px Tahoma, Arial' }) + 3;
  y = drawAlertsSection(doc, alerts, contentX, y, contentW, true);

  drawFooter(doc);
  doc.save('گزارش-پارامترهای-سیستم.pdf');
}

async function generateAlertsReport(alerts = DEMO_ALERTS) {
  const { jsPDF } = window.jspdf;
  const doc = new jsPDF({ unit: 'mm', format: 'a4' });
  const pageW = doc.internal.pageSize.getWidth();
  const contentX = 14, contentW = pageW - 28;

  let y = drawHeader(doc, typeof LOGO_DATA_URL !== 'undefined' ? LOGO_DATA_URL : null, 'گزارش هشدارهای سیستم');
  y = drawAlertsSection(doc, alerts, contentX, y, contentW, false);

  drawFooter(doc);
  doc.save('گزارش-هشدارها.pdf');
}
