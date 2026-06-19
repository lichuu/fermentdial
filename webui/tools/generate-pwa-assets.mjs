// Rasterize public/icon.svg -> public/icon-192.png for home-screen icons.
// This is a MANUAL regen step, not part of `npm run build`: the PNG is committed
// so the build needs no rasterizer. Re-run `npm run icons` only when icon.svg
// changes (requires rsvg-convert or ImageMagick), then commit the new PNG.
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..');
const iconSvg = join(root, 'public', 'icon.svg');
const outPng = join(root, 'public', 'icon-192.png');

if (!existsSync(iconSvg)) {
  console.error(`Missing ${iconSvg}`);
  process.exit(1);
}

for (const bin of ['rsvg-convert', 'magick', 'convert']) {
  try {
    if (bin === 'rsvg-convert') {
      execFileSync(bin, ['-w', '192', '-h', '192', iconSvg, '-o', outPng], {
        stdio: 'inherit',
      });
    } else {
      execFileSync(bin, [iconSvg, '-resize', '192x192', outPng], {
        stdio: 'inherit',
      });
    }
    console.log(`Wrote ${outPng}`);
    process.exit(0);
  } catch {
    // try next rasterizer
  }
}

console.error('No SVG rasterizer found (need rsvg-convert or ImageMagick)');
process.exit(1);