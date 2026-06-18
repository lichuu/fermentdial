// Generates ../src/web_assets.h from dist/app.js + dist/app.css.
// Run via `npm run build` (postbuild step after `vite build`). Do not hand-edit
// the generated header — re-run the build instead.
import { readFileSync, writeFileSync } from 'node:fs';
import { gzipSync, constants as zlibConstants } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

const here = dirname(fileURLToPath(import.meta.url));
const distDir = join(here, '..', 'dist');
const outPath = join(here, '..', '..', 'src', 'web_assets.h');

function gzipBytes(path) {
  const raw = readFileSync(path);
  return gzipSync(raw, { level: zlibConstants.Z_BEST_COMPRESSION });
}

function toByteArray(buf) {
  const lines = [];
  for (let i = 0; i < buf.length; i += 20) {
    lines.push(
      Array.from(buf.subarray(i, i + 20))
        .map((b) => '0x' + b.toString(16).padStart(2, '0'))
        .join(',')
    );
  }
  return lines.join(',\n');
}

const jsGz = gzipBytes(join(distDir, 'app.js'));
const cssGz = gzipBytes(join(distDir, 'app.css'));

const header = `// GENERATED FILE - do not hand-edit.
// Produced by webui/tools/build-header.mjs from webui/dist/app.js and
// webui/dist/app.css. Run \`npm run build\` from webui/ to regenerate.
#pragma once

#include <pgmspace.h>
#include <stddef.h>
#include <stdint.h>

static const uint8_t APP_JS_GZ[] PROGMEM = {
${toByteArray(jsGz)}
};
static const size_t APP_JS_GZ_LEN = sizeof(APP_JS_GZ);

static const uint8_t APP_CSS_GZ[] PROGMEM = {
${toByteArray(cssGz)}
};
static const size_t APP_CSS_GZ_LEN = sizeof(APP_CSS_GZ);
`;

writeFileSync(outPath, header);
console.log(
  `Wrote ${outPath} (app.js ${jsGz.length}B gz, app.css ${cssGz.length}B gz)`
);
