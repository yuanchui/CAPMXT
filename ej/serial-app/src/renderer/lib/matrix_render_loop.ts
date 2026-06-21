/**
 * 矩阵渲染循环：数据接收与 rAF 解耦，每 vsync 只画最新完整帧（Latest-Frame-Wins）
 */

export const MATRIX_SIZE = 16;

export type Matrix16 = (number | null)[][];

export function int16BufferToMatrix16(values: Int16Array): Matrix16 {
  const matrix: Matrix16 = [];
  for (let r = 0; r < MATRIX_SIZE; r++) {
    const row: (number | null)[] = [];
    for (let c = 0; c < MATRIX_SIZE; c++) {
      row.push(values[r * MATRIX_SIZE + c]);
    }
    matrix.push(row);
  }
  return matrix;
}

export interface MatrixRenderLoopOptions {
  onFrame: (matrix: Matrix16, meta: { frameId: number; ts: number }) => void;
}

/**
 * 双缓冲 + 合并调度：100Hz 入帧 / 60Hz 出图，不丢最新帧、不渲染半帧。
 */
export class MatrixRenderLoop {
  private pending: Int16Array | null = null;
  private pendingMeta: { frameId: number; ts: number } | null = null;
  private scheduled = false;
  private rafId = 0;
  private stopped = true;
  private readonly onFrame: MatrixRenderLoopOptions['onFrame'];

  constructor(options: MatrixRenderLoopOptions) {
    this.onFrame = options.onFrame;
  }

  start(): void {
    this.stopped = false;
  }

  /** 提交一帧完整 16×16 数据（Int16 row-major ×256） */
  submitFrame(values: Int16Array, meta: { frameId: number; ts: number }): void {
    if (this.stopped) return;
    this.pending = values;
    this.pendingMeta = meta;
    this.schedule();
  }

  private schedule(): void {
    if (this.scheduled || this.stopped) return;
    this.scheduled = true;
    this.rafId = requestAnimationFrame(() => {
      this.scheduled = false;
      if (this.stopped || !this.pending || !this.pendingMeta) return;
      const matrix = int16BufferToMatrix16(this.pending);
      const meta = this.pendingMeta;
      this.pending = null;
      this.pendingMeta = null;
      this.onFrame(matrix, meta);
    });
  }

  /** STOP：取消待渲染、丢弃队列 */
  stop(): void {
    this.stopped = true;
    if (this.rafId) {
      cancelAnimationFrame(this.rafId);
      this.rafId = 0;
    }
    this.scheduled = false;
    this.pending = null;
    this.pendingMeta = null;
  }

  resume(): void {
    this.stopped = false;
  }
}
