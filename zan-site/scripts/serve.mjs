import { createServer } from 'node:http';
import { readFile, realpath, stat } from 'node:fs/promises';
import { extname, isAbsolute, relative, resolve, sep } from 'node:path';
import { fileURLToPath } from 'node:url';

const preview = process.argv.includes('--preview');
const root = resolve(fileURLToPath(new URL(preview ? '../../build/zan-site/' : '../', import.meta.url)));
const realRoot = await realpath(root);
const host = process.env.HOST || '127.0.0.1';
const port = Number(process.env.PORT || 4173);
const contentTypes = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8', '.md': 'text/markdown; charset=utf-8', '.txt': 'text/plain; charset=utf-8', '.svg': 'image/svg+xml', '.json': 'application/json; charset=utf-8', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg', '.webp': 'image/webp', '.ico': 'image/x-icon', '.woff2': 'font/woff2' };

function isWithin(base, target) {
  const path = relative(base, target);
  return path !== '..' && !path.startsWith(`..${sep}`) && !isAbsolute(path);
}

function httpError(status) { return Object.assign(new Error(), { status }); }

function safePath(requestPath) {
  if (!requestPath || !requestPath.startsWith('/') || requestPath.startsWith('//') || /[\u0000-\u0020\u007f#]/.test(requestPath)) throw httpError(400);
  let pathname;
  try {
    decodeURIComponent(requestPath);
    pathname = decodeURIComponent(requestPath.split('?')[0]);
  } catch { throw httpError(400); }
  if (/[\u0000-\u001f\u007f:]/.test(pathname)) throw httpError(400);
  if (pathname.includes('\\')) throw httpError(403);
  const candidate = resolve(root, `.${pathname}`);
  if (!isWithin(root, candidate)) throw httpError(403);
  return candidate;
}

async function resolveFile(candidate) {
  let target = await realpath(candidate);
  if (!isWithin(realRoot, target)) throw httpError(403);
  let info = await stat(target);
  if (info.isDirectory()) {
    target = await realpath(resolve(target, 'index.html'));
    if (!isWithin(realRoot, target)) throw httpError(403);
    info = await stat(target);
  }
  if (!info.isFile()) throw httpError(404);
  return { target, info };
}

const server = createServer(async (request, response) => {
  const sendError = (status, message, headers = {}) => {
    response.writeHead(status, { 'Content-Type': 'text/plain; charset=utf-8', 'Content-Length': Buffer.byteLength(message), ...headers });
    response.end(request.method === 'HEAD' ? undefined : message);
  };
  if (!['GET', 'HEAD'].includes(request.method)) { sendError(405, 'Method not allowed', { Allow: 'GET, HEAD' }); return; }
  try {
    const { target, info } = await resolveFile(safePath(request.url));
    const body = request.method === 'HEAD' ? null : await readFile(target);
    response.writeHead(200, { 'Content-Type': contentTypes[extname(target).toLowerCase()] || 'application/octet-stream', 'Content-Length': body ? body.length : info.size, 'Cache-Control': 'no-cache', 'X-Content-Type-Options': 'nosniff' });
    response.end(body);
  } catch (error) {
    const status = error.status || (['ENOENT', 'ENOTDIR', 'ELOOP'].includes(error.code) ? 404 : ['EACCES', 'EPERM'].includes(error.code) ? 403 : 500);
    sendError(status, { 400: 'Bad request', 403: 'Forbidden', 404: 'Not found', 500: 'Internal server error' }[status]);
  }
});

server.listen(port, host, () => {
  const address = server.address();
  console.log(`Zan site ${preview ? 'preview' : 'development'} server running at http://${host.includes(':') ? `[${host}]` : host}:${address.port}`);
});
