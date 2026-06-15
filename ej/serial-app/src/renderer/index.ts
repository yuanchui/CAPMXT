import Chart from 'chart.js/auto';
import type { DataFormat, ModeButton, TrendModal } from './types';

// 全局状态（使用类型注解）
let port: SerialPort | null = null;
let reader: ReadableStreamDefaultReader<Uint8Array> | null = null;
let writer: WritableStreamDefaultWriter<Uint8Array> | null = null;
let readLoopRunning: boolean = false;

const MATRIX_SIZE: number = 16;
const MAX_DISPLAY_CHARS: number = 20000;

// DOM 元素（使用严格类型）
const connectBtn = document.getElementById("connectBtn") as HTMLButtonElement;
const disconnectBtn = document.getElementById("disconnectBtn") as HTMLButtonElement;
const sendBtn = document.getElementById("sendBtn") as HTMLButtonElement;
const sendText = document.getElementById("sendText") as HTMLTextAreaElement;
const recvText = document.getElementById("recvText") as HTMLDivElement;

// 矩阵缓存
const matrixCellCache = new Map<string, HTMLDivElement>();
let currentMatrixData: (number | null)[][] | null = null;

// 示例：连接设备函数
async function connectDevice(): Promise<boolean> {
  try {
    if (!('serial' in navigator)) {
      console.error('浏览器不支持 Web Serial API');
      return false;
    }

    port = await navigator.serial.requestPort({
      filters: [{ usbVendorId: 0x0483, usbProductId: 0x5740 }]
    });

    await port.open({ baudRate: 115200 });
    writer = port.writable.getWriter();
    reader = port.readable.getReader();
    readLoopRunning = true;

    startReadLoop();
    return true;
  } catch (error) {
    // 处理用户取消选择端口的情况
    if (error instanceof DOMException && error.message.includes('No port selected by the user')) {
      console.log('用户取消了端口选择');
      return false;
    }
    console.error('连接失败:', error);
    return false;
  }
}

// 示例：读取循环
async function startReadLoop(): Promise<void> {
  if (!reader) return;

  try {
    while (readLoopRunning) {
      const { value, done } = await reader.read();
      if (done) break;
      if (!value) continue;

      // 处理数据...
      processReceivedData(value);
    }
  } catch (error) {
    console.error('读取错误:', error);
  }
}

function processReceivedData(data: Uint8Array): void {
  // 你的数据处理逻辑
  const decoder = new TextDecoder();
  const text = decoder.decode(data);
  recvText.textContent += text;
}

// 事件监听器
connectBtn.addEventListener('click', connectDevice);
disconnectBtn.addEventListener('click', async () => {
  readLoopRunning = false;
  await reader?.cancel();
  await port?.close();
  port = null;
});

sendBtn.addEventListener('click', async () => {
  if (!writer) return;
  const encoder = new TextEncoder();
  await writer.write(encoder.encode(sendText.value + '\n'));
});

// 初始化
document.addEventListener('DOMContentLoaded', () => {
  console.log('应用已启动');
  // 加载本地存储配置...
});
