/**
 * 渲染端串口矩阵管线：Mode3 组帧 → 计算引擎 → 回调
 */

import { Mode3FrameAssembler, type MatrixFramePayload } from '../../shared/mode3_parser';
import { MatrixComputeEngine } from './matrix_compute_engine';
import type { MatrixComputeResult } from './matrix_compute';

export type FrameReadyHandler = (
  values: Int16Array,
  meta: { frameId: number; ts: number },
  compute: MatrixComputeResult
) => void;

export class MatrixStreamPipeline {
  private assembler: Mode3FrameAssembler;
  private computeEngine: MatrixComputeEngine;
  private onFrameReady: FrameReadyHandler;
  private paused = false;
  private lastFrameKey = '';

  constructor(onFrameReady: FrameReadyHandler) {
    this.onFrameReady = onFrameReady;
    this.computeEngine = new MatrixComputeEngine();
    this.assembler = new Mode3FrameAssembler({
      onFrame: (frame) => this.handleFrame(frame),
    });
  }

  setReferenceMatrix(matrix: number[][] | null): void {
    this.computeEngine.setReferenceMatrix(matrix);
  }

  pushBytes(chunk: Uint8Array): void {
    if (!this.paused) this.assembler.push(chunk);
  }

  /** 主进程 IPC 已组好的帧（与本地解析去重） */
  ingestIpcFrame(values: Int16Array, meta: { frameId: number; ts: number }): void {
    if (this.paused) return;
    const key = `${meta.frameId}:${meta.ts}`;
    if (key === this.lastFrameKey) return;
    this.lastFrameKey = key;
    void this.dispatchFrame(values, meta);
  }

  pause(): void {
    this.paused = true;
    this.assembler.pauseAndFlush();
    this.lastFrameKey = '';
  }

  resume(): void {
    this.paused = false;
    this.assembler.resume();
    this.lastFrameKey = '';
  }

  dispose(): void {
    this.computeEngine.dispose();
  }

  private handleFrame(frame: MatrixFramePayload): void {
    if (this.paused) return;
    const key = `${frame.frameId}:${frame.timestamp}`;
    if (key === this.lastFrameKey) return;
    this.lastFrameKey = key;
    void this.dispatchFrame(frame.values, { frameId: frame.frameId, ts: frame.timestamp });
  }

  private async dispatchFrame(
    values: Int16Array,
    meta: { frameId: number; ts: number }
  ): Promise<void> {
    const copy = new Int16Array(values);
    try {
      const compute = await this.computeEngine.compute(copy);
      this.onFrameReady(copy, meta, compute);
    } catch (err) {
      console.error('[matrix-pipeline] compute failed', err);
    }
  }
}
