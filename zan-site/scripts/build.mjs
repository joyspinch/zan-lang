import { copyFile, lstat, mkdir, readdir } from 'node:fs/promises';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const output = resolve(root, '../build/zan-site');
const entries = ['index.html', 'src', 'assets', 'agent-guide.md', 'llms.txt'];

async function existingInfo(path) {
  try { return await lstat(path); }
  catch (error) { if (error.code === 'ENOENT') return null; throw error; }
}

async function ensureDirectory(path) {
  const info = await existingInfo(path);
  if (info && (!info.isDirectory() || info.isSymbolicLink())) throw new Error(`Refusing to replace an existing non-directory: ${path}`);
  await mkdir(path, { recursive: true });
}

async function copyKnown(source, target) {
  const info = await lstat(source);
  if (info.isDirectory()) {
    await ensureDirectory(target);
    for (const entry of await readdir(source)) await copyKnown(join(source, entry), join(target, entry));
  } else if (info.isFile()) {
    const existing = await existingInfo(target);
    if (existing && (!existing.isFile() || existing.isSymbolicLink())) throw new Error(`Refusing to replace an existing non-file: ${target}`);
    await copyFile(source, target);
  } else throw new Error(`Unsupported build input: ${source}`);
}

await ensureDirectory(resolve(output, '..'));
await ensureDirectory(output);
for (const entry of entries) await copyKnown(join(root, entry), join(output, entry));
console.log(`Built Zan site to ${output}`);
