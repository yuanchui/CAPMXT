/**
 * 串口流 Hub：主进程侧 Mode3 解析 + 原始字节转发（Buffer IPC，无 Array.from）
 */

import type { BrowserWindow } from 'electron';

import { Mode3FrameAssembler, type MatrixFramePayload } from './mode3_parser';

export interface SerialStreamHubOptions {
  broadcast: (channel: string, ...args: unknown[]) => void;
}

interface PortStreamState {
  assembler: Mode3FrameAssembler;
  /** false = STOP 后丢弃 USB 残留字节，不再转发到渲染进程 */
  streamActive: boolean;
}

const portStreams = new Map<string, PortStreamState>();

function getOrCreateState(portPath: string, broadcast: SerialStreamHubOptions['broadcast']): PortStreamState {
  let state = portStreams.get(portPath);
  if (!state) {
    const assembler = new Mode3FrameAssembler({
      onFrame: (frame: MatrixFramePayload) => {
        const buf = Buffer.from(frame.values.buffer, frame.values.byteOffset, frame.values.byteLength);
        broadcast('serial-matrix-frame', portPath, {
          frameId: frame.frameId,
          ts: frame.timestamp,
          min: frame.stats.min,
          max: frame.stats.max,
          mean: frame.stats.mean,
          data: buf,
        });
      },
    });
    state = { assembler, streamActive: false };
    portStreams.set(portPath, state);
  }
  return state;
}

/** 串口 data 事件入口（UI 常规读取路径） */
export function handleSerialDataChunk(
  portPath: string,
  data: Buffer,
  broadcast: SerialStreamHubOptions['broadcast']
): void {
  const state = getOrCreateState(portPath, broadcast);

  if (!state.streamActive) {
    return;
  }

  state.assembler.push(new Uint8Array(data));
  broadcast('serial-data', portPath, data);
}

/** 非 streaming 场景（欢迎语、命令响应）：仅转发原始字节，不解析 Mode3 */
export function forwardSerialDataRaw(
  portPath: string,
  data: Buffer,
  broadcast: SerialStreamHubOptions['broadcast']
): void {
  if (!data.length) return;
  broadcast('serial-data', portPath, data);
}

/** STOP / 断开：暂停解析与接收区追加，清空重组缓冲 */
export function flushSerialStream(portPath: string): void {
  const state = portStreams.get(portPath);
  if (!state) return;
  state.streamActive = false;
  state.assembler.pauseAndFlush();
}

/** START 等 streaming 命令：恢复接收 */
export function resumeSerialStream(portPath: string): void {
  const state = getOrCreateState(portPath, () => {});
  state.streamActive = true;
  state.assembler.resume();
}

/** 统一串口 data 路由：streaming 时解析+转发，否则仅转发文本/欢迎语 */
export function routeSerialPortData(
  portPath: string,
  data: Buffer,
  broadcast: SerialStreamHubOptions['broadcast']
): void {
  const state = portStreams.get(portPath);
  if (state?.streamActive) {
    handleSerialDataChunk(portPath, data, broadcast);
  } else {
    forwardSerialDataRaw(portPath, data, broadcast);
  }
}

export function removeSerialStream(portPath: string): void {
  portStreams.delete(portPath);
}

/** 检测写入命令是否为 STOP */
export function isStopCommand(data: string | Uint8Array): boolean {
  if (typeof data === 'string') {
    return data.trim().toUpperCase() === 'STOP';
  }
  const text = Buffer.from(data).toString('utf8').trim().toUpperCase();
  return text === 'STOP';
}

/** 检测是否为 streaming 启动命令 */
export function isStreamStartCommand(data: string | Uint8Array): boolean {
  const text = (typeof data === 'string' ? data : Buffer.from(data).toString('utf8')).trim().toUpperCase();
  return (
    text.startsWith('START ') ||
    text.startsWith('START1 ') ||
    text === 'SPISTART3' ||
    text.startsWith('SPISTART')
  );
}

export function createBroadcast(winProvider: () => BrowserWindow[]): SerialStreamHubOptions['broadcast'] {
  return (channel: string, ...args: unknown[]) => {
    for (const win of winProvider()) {
      if (!win || win.isDestroyed()) continue;
      try {
        win.webContents.send(channel, ...args);
      } catch {
        /* ignore */
      }
    }
  };
}
