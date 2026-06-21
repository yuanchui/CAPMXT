import { computeMatrixAnalytics } from './matrix_compute';

export type WorkerRequest = {
  id: number;
  live: Int16Array;
  reference: Int16Array | null;
};

export type WorkerResponse = {
  id: number;
  result: ReturnType<typeof computeMatrixAnalytics>;
};

self.onmessage = (ev: MessageEvent<WorkerRequest>) => {
  const { id, live, reference } = ev.data;
  const result = computeMatrixAnalytics(live, reference);
  const payload: WorkerResponse = { id, result };
  const transfer: Transferable[] = [];
  if (result.diff) transfer.push(result.diff.buffer);
  (self as unknown as Worker).postMessage(payload, transfer);
};
