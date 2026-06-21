/**
 * 生成加密有效期文件并 SCP 到服务器 /var/www/mxt-runtime/
 *
 * 用法:
 *   node build/deploy-runtime-window/upload.js
 *   node build/deploy-runtime-window/upload.js --setup-remote   # 仅推送 setup-server.sh 并在远端执行
 *
 * 需配置 build/deploy-runtime-window/runtime-deploy.env
 */
const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const DEPLOY_DIR = __dirname;
const LOCAL_JSON = path.join(ROOT, 'dist', 'main', 'runtime-window.generated.json');
const ENV_FILE = path.join(DEPLOY_DIR, 'runtime-deploy.env');

function loadEnv() {
  const out = {
    REMOTE_HOST: '175.24.71.193',
    REMOTE_USER: 'root',
    SSH_PORT: '22',
    REMOTE_HTTP_PORT: '19876',
    REMOTE_DIR: '/var/www/mxt-runtime',
    REMOTE_FILE: 'runtime-window.generated.json',
    SSH_KEY: ''
  };
  if (!fs.existsSync(ENV_FILE)) return out;
  const text = fs.readFileSync(ENV_FILE, 'utf-8');
  for (const line of text.split(/\r?\n/)) {
    const t = line.trim();
    if (!t || t.startsWith('#')) continue;
    const i = t.indexOf('=');
    if (i < 0) continue;
    const k = t.slice(0, i).trim();
    let v = t.slice(i + 1).trim();
    if (k === 'REMOTE_PORT') {
      out.SSH_PORT = v;
      continue;
    }
    if (k in out) out[k] = v;
  }
  syncHttpPortFromRuntimeConfig(out);
  return out;
}

function syncHttpPortFromRuntimeConfig(env) {
  try {
    const cfgPath = path.join(ROOT, 'build', 'runtime-window.config.json');
    if (!fs.existsSync(cfgPath)) return;
    const obj = JSON.parse(fs.readFileSync(cfgPath, 'utf-8'));
    const p = Number(obj?.remoteHttpPort);
    if (Number.isFinite(p) && p > 0) env.REMOTE_HTTP_PORT = String(Math.floor(p));
    if (obj?.remoteHost) env.REMOTE_HOST = String(obj.remoteHost).trim();
  } catch {
    /* ignore */
  }
}

function run(cmd, args, opts = {}) {
  const r = spawnSync(cmd, args, { stdio: 'inherit', shell: false, ...opts });
  if (r.error) throw r.error;
  if (r.status !== 0) process.exit(r.status || 1);
}

function scpBaseArgs(env) {
  const args = ['-P', env.SSH_PORT, '-o', 'StrictHostKeyChecking=accept-new'];
  if (env.SSH_KEY) args.push('-i', env.SSH_KEY);
  return args;
}

function scpUpload(localPath, remoteSpec, env) {
  run('scp', [...scpBaseArgs(env), localPath, remoteSpec]);
}

function sshExec(remoteCmd, env) {
  const target = `${env.REMOTE_USER}@${env.REMOTE_HOST}`;
  run('ssh', [...scpBaseArgs(env), target, remoteCmd]);
}

function setupRemoteServer(env) {
  const setupSh = path.join(DEPLOY_DIR, 'setup-server.sh');
  const servePy = path.join(DEPLOY_DIR, 'serve_runtime.py');
  const serviceFile = path.join(DEPLOY_DIR, 'mxt-runtime.service');
  if (!fs.existsSync(setupSh) || !fs.existsSync(servePy)) {
    console.error('缺少 setup-server.sh 或 serve_runtime.py');
    process.exit(1);
  }
  const remoteTmp = '/tmp/mxt-runtime-deploy';
  const remote = `${env.REMOTE_USER}@${env.REMOTE_HOST}`;
  sshExec(`mkdir -p ${remoteTmp}`, env);
  scpUpload(setupSh, `${remote}:${remoteTmp}/setup-server.sh`, env);
  scpUpload(servePy, `${remote}:${remoteTmp}/serve_runtime.py`, env);
  scpUpload(serviceFile, `${remote}:${remoteTmp}/mxt-runtime.service`, env);
  sshExec(`HTTP_PORT=${env.REMOTE_HTTP_PORT} bash ${remoteTmp}/setup-server.sh`, env);
  console.log('[deploy] 远端 Python 服务初始化完成');
}

function uploadRuntimeJson(env) {
  if (!fs.existsSync(LOCAL_JSON)) {
    console.error(`未找到 ${LOCAL_JSON}，请先运行 node build/generate-runtime-window.js`);
    process.exit(1);
  }
  const remotePath = `${env.REMOTE_DIR}/${env.REMOTE_FILE}`;
  const remoteSpec = `${env.REMOTE_USER}@${env.REMOTE_HOST}:${remotePath}`;
  sshExec(`mkdir -p ${env.REMOTE_DIR}`, env);
  scpUpload(LOCAL_JSON, remoteSpec, env);
  sshExec(`chmod 644 ${remotePath}`, env);

  const url = `http://${env.REMOTE_HOST}:${env.REMOTE_HTTP_PORT}/${env.REMOTE_FILE}`;
  console.log('');
  console.log('[deploy] 上传完成');
  console.log(`  本地: ${LOCAL_JSON}`);
  console.log(`  远端: ${remotePath}`);
  console.log(`  访问: ${url}`);
  console.log('');
}

function main() {
  const env = loadEnv();
  const setupOnly = process.argv.includes('--setup-remote');

  if (!fs.existsSync(ENV_FILE)) {
    console.warn(`[deploy] 未找到 ${ENV_FILE}`);
    console.warn('       请复制 runtime-deploy.env.example 为 runtime-deploy.env 并填写 SSH 用户');
  }

  if (setupOnly) {
    setupRemoteServer(env);
    return;
  }

  const genScript = path.join(ROOT, 'build', 'generate-runtime-window.js');
  console.log('[deploy] 生成本地加密配置...');
  run('node', [genScript], { cwd: ROOT });

  uploadRuntimeJson(env);
}

main();
