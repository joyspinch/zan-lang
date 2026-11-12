import { readdir } from 'node:fs/promises';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';

const root = fileURLToPath(new URL('../', import.meta.url));
for (const folder of ['src', 'scripts']) {
  const files = (await readdir(join(root, folder))).filter(file => /\.m?js$/.test(file)).sort();
  for (const file of files) {
    const result = spawnSync(process.execPath, ['--check', join(root, folder, file)], { encoding: 'utf8' });
    if (result.status !== 0) {
      process.stderr.write(result.stderr || String(result.error));
      process.exit(result.status || 1);
    }
  }
}
console.log('Site JavaScript syntax checks passed');
