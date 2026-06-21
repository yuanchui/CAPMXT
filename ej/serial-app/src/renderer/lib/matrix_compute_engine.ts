/**
 * 矩阵计算引擎：Web Worker 异步 + 主线程同步回退
 */

import { computeMatrixAnalytics, type MatrixComputeResult } from './matrix_compute';

export class MatrixComputeEngine {
  private worker: Worker | null = null;
  private seq = 0;
  private pending = new Map<number, {
    resolve: (r: MatrixComputeResult) => void;
    reject: (e: unknown) => void;
  }>();
  private reference: Int16Array | null = null;
  private workerFailed = false;

  constructor() {
    try {
      this.worker = new Worker(new URL('./matrix_compute.worker.ts', import.meta.url), { type: 'module' });
      this.worker.onmessage = (ev: MessageEvent<{ id: number; result: MatrixComputeResult }>) => {
        const job = this.pending.get(ev.data.id);
        if (job) {
          this.pending.delete(ev.data.id);
          job.resolve(ev.data.result);
        }
      };
      this.worker.onerror = () => {
        this.workerFailed = true;
        this.worker?.terminate();
        this.worker = null;
      };
    } catch {
      this.workerFailed = true;
    }
  }

  setReferenceMatrix(matrix: number[][] | null): void {
    if (!matrix || matrix.length !== 16) {
      this.reference = null;
      return;
    }
    const ref = new Int16Array(256);
    for (let r = 0; r < 16; r++) {
      for (let c = 0; c < 16; c++) {
        const v = matrix[r]?.[c];
        ref[r * 16 + c] = Number.isFinite(v) ? Math.round(v as number) : 0;
      }
    }
    this.reference = ref;
  }

  compute(live: Int16Array): Promise<MatrixComputeResult> {
    const copy = new Int16Array(live);
    if (this.worker && !this.workerFailed) {
      const id = ++this.seq;
      return new Promise((resolve, reject) => {
        this.pending.set(id, { resolve, reject });
        const ref = this.reference ? new Int16Array(this.reference) : null;
        const transfer: Transferable[] = [copy.buffer];
        if (ref) transfer.push(ref.buffer);
        this.worker!.postMessage({ id, live: copy, reference: ref }, transfer);
      });
    }
    return Promise.resolve(computeMatrixAnalytics(copy, this.reference));
  }

  dispose(): void {
    this.worker?.terminate();
    this.worker = null;
    this.pending.clear();
  }
}
