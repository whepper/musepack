// Small formatting helpers for the record-shelf UI.

export function fmtTime(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds < 0) return '0:00';
  const s = Math.floor(seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = s % 60;
  if (h > 0) return `${h}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
  return `${m}:${String(sec).padStart(2, '0')}`;
}

/** "1986" or "1986-06-16" -> "1986" (short year, honoring partial dates). */
export function yearOf(date?: string): string {
  if (!date) return '';
  return date.slice(0, 4);
}

/** "1986-06-16" -> "16 Jun 1986". */
export function formatDate(date?: string): string {
  if (!date) return '';
  const [y, m, d] = date.split('-');
  if (!y) return date;
  const month = new Date(Number(y), (Number(m) || 1) - 1, 1).toLocaleString('en', { month: 'short' });
  return d ? `${Number(d)} ${month} ${y}` : `${month} ${y}`;
}

/** Country code -> display ("XE" -> "Europe"). */
export function countryName(code?: string): string {
  if (!code) return '';
  const map: Record<string, string> = {
    XE: 'Europe',
    XW: 'Worldwide',
    XN: 'Northern Europe',
    US: 'United States',
    UK: 'United Kingdom',
    GB: 'United Kingdom',
    DE: 'Germany',
    FR: 'France',
    JP: 'Japan',
    NL: 'Netherlands',
  };
  return map[code] ?? code;
}

export function mediumLabel(format?: string): string {
  if (!format) return 'Digital';
  const lower = format.toLowerCase();
  if (lower.includes('vinyl') || lower.includes('lp')) return 'Vinyl';
  if (lower.includes('sacd')) return 'SACD';
  if (lower.includes('dvd')) return 'DVD';
  if (lower.includes('blu-ray') || lower.includes('blu ray')) return 'Blu-ray';
  if (lower.includes('cd')) return 'CD';
  if (lower.includes('digital') || lower.includes('download')) return 'Digital';
  return format;
}

/** Compact collector line for the shelf: "1986 · CD · 2 versions". */
export function collectorLine(opts: {
  year?: string;
  media?: string[];
  releaseCount?: number;
}): string {
  const parts: string[] = [];
  if (opts.year) parts.push(opts.year);
  if (opts.media?.length) parts.push(opts.media.map(mediumLabel).join('/'));
  if (opts.releaseCount !== undefined && opts.releaseCount > 1) {
    parts.push(`${opts.releaseCount} version${opts.releaseCount > 1 ? 's' : ''}`);
  }
  return parts.join(' · ');
}
