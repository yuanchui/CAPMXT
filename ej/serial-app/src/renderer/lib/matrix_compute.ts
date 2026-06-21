/**
 * 16×16 矩阵高级统计 — 纯函数，主线程 / Worker 共用
 */

import { MATRIX_SIZE } from '../../shared/mode3_parser';

export interface MatrixComputeResult {
  min: number;
  max: number;
  mean: number;
  stddev: number;
  snrDb: number | null;
  errorRatePct: number | null;
  diffMin: number | null;
  diffMax: number | null;
  /** row-major 差分 (live - ref)，无参考时为 null */
  diff: Float32Array | null;
}

export function computeMatrixAnalytics(
  live: Int16Array,
  reference: Int16Array | null = null
): MatrixComputeResult {
  let min = Number.POSITIVE_INFINITY;
  let max = Number.NEGATIVE_INFINITY;
  let sum = 0;
  const n = live.length;

  for (let i = 0; i < n; i++) {
    const v = live[i];
    if (v < min) min = v;
    if (v > max) max = v;
    sum += v;
  }
  const mean = sum / n;
  let varSum = 0;
  for (let i = 0; i < n; i++) {
    const d = live[i] - mean;
    varSum += d * d;
  }
  const stddev = Math.sqrt(varSum / n);

  let snrDb: number | null = null;
  let errorRatePct: number | null = null;
  let diffMin: number | null = null;
  let diffMax: number | null = null;
  let diff: Float32Array | null = null;

  if (reference && reference.length === n) {
    diff = new Float32Array(n);
    let signalPower = 0;
    let noisePower = 0;
    let signalAbsSum = 0;
    let errorAbsSum = 0;
    let valid = 0;

    for (let i = 0; i < n; i++) {
      const signal = reference[i] / 1000;
      const received = live[i] / 1000;
      const noise = received - signal;
      diff[i] = live[i] - reference[i];
      if (diff[i] < (diffMin ?? Infinity)) diffMin = diff[i];
      if (diff[i] > (diffMax ?? -Infinity)) diffMax = diff[i];

      signalPower += signal * signal;
      noisePower += noise * noise;
      signalAbsSum += Math.abs(signal);
      errorAbsSum += Math.abs(noise);
      valid++;
    }

    if (valid > 0) {
      if (noisePower === 0) snrDb = Number.POSITIVE_INFINITY;
      else if (signalPower === 0) snrDb = Number.NEGATIVE_INFINITY;
      else snrDb = 10 * Math.log10(signalPower / noisePower);

      if (errorAbsSum === 0) errorRatePct = 0;
      else if (signalAbsSum === 0) errorRatePct = Number.POSITIVE_INFINITY;
      else errorRatePct = (errorAbsSum / signalAbsSum) * 100;
    }
  }

  return { min, max, mean, stddev, snrDb, errorRatePct, diffMin, diffMax, diff };
}

export function int16ToMatrixRows(values: Int16Array): number[][] {
  const rows: number[][] = [];
  for (let r = 0; r < MATRIX_SIZE; r++) {
    const row: number[] = [];
    for (let c = 0; c < MATRIX_SIZE; c++) {
      row.push(values[r * MATRIX_SIZE + c]);
    }
    rows.push(row);
  }
  return rows;
}
