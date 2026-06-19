/** Input validation aligned with firmware limits in include/config.h */

const DISPLAY_STEP = 0.1;

function fToC(f) {
  return ((f - 32) * 5) / 9;
}

function deltaFToC(df) {
  return (df * 5) / 9;
}

function roundTo(value, decimals) {
  const p = 10 ** decimals;
  return Math.round(value * p) / p;
}

export function parseNumber(value) {
  if (value == null) return NaN;
  const s = String(value).trim();
  if (!s || s === '-' || s === '.' || s === '-.') return NaN;
  const n = Number(s);
  return Number.isFinite(n) ? n : NaN;
}

export function isDecimalString(value) {
  const s = String(value ?? '').trim();
  return /^-?\d+(\.\d+)?$/.test(s) || /^-?\d*\.\d+$/.test(s);
}

export function filterDecimalInput(value, { allowNegative = false } = {}) {
  let s = String(value ?? '');
  let out = '';
  let dot = false;
  let minus = false;
  for (const ch of s) {
    if (ch >= '0' && ch <= '9') {
      out += ch;
      continue;
    }
    if (ch === '.' && !dot) {
      dot = true;
      out += ch;
      continue;
    }
    if (allowNegative && ch === '-' && !minus && out.length === 0) {
      minus = true;
      out += ch;
    }
  }
  return out;
}

export function filterIntegerInput(value) {
  return String(value ?? '').replace(/\D/g, '');
}

export function targetLimits(unit) {
  if (unit === 'C') {
    return {
      min: roundTo(fToC(35), 1),
      max: roundTo(fToC(95), 1),
      step: DISPLAY_STEP,
      decimals: 1,
      fallback: roundTo(fToC(68), 1),
    };
  }
  return { min: 35, max: 95, step: DISPLAY_STEP, decimals: 1, fallback: 68 };
}

export function deltaLimits(unit) {
  if (unit === 'C') {
    return {
      min: roundTo(deltaFToC(0.1), 1),
      max: roundTo(deltaFToC(10), 1),
      step: DISPLAY_STEP,
      decimals: 1,
      fallback: roundTo(deltaFToC(0.5), 1),
    };
  }
  return { min: 0.1, max: 10, step: DISPLAY_STEP, decimals: 1, fallback: 0.5 };
}

export function offsetLimits(unit) {
  if (unit === 'C') {
    return {
      min: roundTo(deltaFToC(-10), 1),
      max: roundTo(deltaFToC(10), 1),
      step: DISPLAY_STEP,
      decimals: 1,
      fallback: 0,
    };
  }
  return { min: -10, max: 10, step: DISPLAY_STEP, decimals: 1, fallback: 0 };
}

export function gradualStepLimits(unit) {
  if (unit === 'C') {
    return {
      min: roundTo(deltaFToC(0.5), 1),
      max: roundTo(deltaFToC(20), 1),
      step: DISPLAY_STEP,
      decimals: 1,
      fallback: roundTo(deltaFToC(5), 1),
    };
  }
  return { min: 0.5, max: 20, step: DISPLAY_STEP, decimals: 1, fallback: 5 };
}

export const HOURS_LIMITS = {
  min: 0,
  max: 9999,
  step: 0.5,
  decimals: 2,
  fallback: 24,
};

export const STABLE_HOURS_LIMITS = {
  min: 1,
  max: 65535,
  step: 1,
  decimals: 0,
  fallback: 24,
};

export const GRAVITY_LIMITS = {
  min: 0.9,
  max: 1.2,
  step: 0.001,
  decimals: 3,
  fallback: 1.01,
};

export const DREST_HOURS_LIMITS = {
  min: 24,
  max: 96,
  step: 24,
  decimals: 0,
  fallback: 48,
};

export const INTERVAL_HOURS_LIMITS = {
  min: 1,
  max: 9999,
  step: 1,
  decimals: 0,
  fallback: 12,
};

export function coerceDecimal(value, opts) {
  let n = parseNumber(value);
  if (!Number.isFinite(n)) n = opts.fallback;
  if (n < opts.min) n = opts.min;
  if (n > opts.max) n = opts.max;
  if (opts.step > 0) {
    n = Math.round(n / opts.step) * opts.step;
  }
  return n.toFixed(opts.decimals);
}

export function isDecimalValid(value, opts) {
  if (!isDecimalString(value)) return false;
  const n = parseNumber(value);
  if (!Number.isFinite(n)) return false;
  return n >= opts.min && n <= opts.max;
}

/** True when the field has text that fails validation (not while empty). */
export function showDecimalInvalid(value, opts) {
  const s = String(value ?? '').trim();
  if (!s) return false;
  return !isDecimalValid(value, opts);
}

export function decimalError(value, opts, label) {
  if (!isDecimalString(value)) return `${label} must be a number.`;
  const n = parseNumber(value);
  if (!Number.isFinite(n)) return `${label} must be a number.`;
  if (n < opts.min || n > opts.max) {
    return `${label} must be between ${opts.min} and ${opts.max}.`;
  }
  return null;
}