import * as net from 'net';

import type { SerialPort } from 'serialport';

import { BridgeLogSessionParser, type BridgeLogEntry } from './mxt_bridge_log';

export interface SerialProxyHandle {
  host: string;
  port: number;
  deviceString: string;
  close: () => Promise<void>;
}

export interface SerialProxyOptions {
  onBridgeLog?: (entry: BridgeLogEntry) => void;
}

/**
 * 将已打开的 SerialPort 暴露为本地 TCP 字节流，供 mxt-app 通过 serial:proxy:HOST:PORT 复用。
 * 可选 onBridgeLog：把收发字节解析为可读字符串（mode0/I2C OK 等）供终端显示。
 */
export function startSerialProxy(serialPort: SerialPort, options: SerialProxyOptions = {}): Promise<SerialProxyHandle> {
  const onBridgeLog = options.onBridgeLog;
  let activeSocket: net.Socket | null = null;
  let serialToSocket: ((chunk: Buffer) => void) | null = null;
  const bridgeParser = new BridgeLogSessionParser();

  const emitBridgeLogs = (buf: Buffer, direction: 'tx' | 'rx') => {
    if (!onBridgeLog || !buf.length) return;
    for (const entry of bridgeParser.push(buf, direction)) {
      onBridgeLog(entry);
    }
  };

  const server = net.createServer((socket) => {
    if (activeSocket && !activeSocket.destroyed) {
      activeSocket.destroy();
    }
    activeSocket = socket;
    bridgeParser.reset();

    serialToSocket = (chunk: Buffer) => {
      emitBridgeLogs(chunk, 'rx');
      if (!socket.destroyed) socket.write(chunk);
    };
    serialPort.on('data', serialToSocket);

    const onSocketData = (buf: Buffer) => {
      emitBridgeLogs(buf, 'tx');
      serialPort.write(buf, (err) => {
        if (err && !socket.destroyed) socket.destroy();
      });
    };
    socket.on('data', onSocketData);

    const cleanup = () => {
      if (serialToSocket) {
        serialPort.removeListener('data', serialToSocket);
        serialToSocket = null;
      }
      if (activeSocket === socket) activeSocket = null;
      bridgeParser.reset();
    };

    socket.on('close', cleanup);
    socket.on('error', cleanup);
  });

  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const addr = server.address();
      const port = typeof addr === 'object' && addr ? addr.port : 0;
      const host = '127.0.0.1';
      resolve({
        host,
        port,
        deviceString: `serial:proxy:${host}:${port}`,
        close: async () => {
          if (activeSocket && !activeSocket.destroyed) {
            await new Promise<void>((res) => {
              activeSocket!.once('close', () => res());
              activeSocket!.destroy();
            });
          }
          if (serialToSocket) {
            serialPort.removeListener('data', serialToSocket);
            serialToSocket = null;
          }
          bridgeParser.reset();
          await new Promise<void>((res, rej) => {
            server.close((err) => (err ? rej(err) : res()));
          });
        }
      });
    });
  });
}
