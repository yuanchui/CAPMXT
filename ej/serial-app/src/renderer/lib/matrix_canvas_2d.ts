/**
 * 2D 矩阵热力图 — Canvas 单次 drawImage，替代 256 次 DOM 写
 */

import { MATRIX_SIZE } from '../../shared/mode3_parser';
import type { MatrixComputeResult } from './matrix_compute';

export interface MatrixCanvas2DOptions {
  canvas: HTMLCanvasElement;
  anchorGrid: HTMLElement;
  getCellColor?: (value: number, min: number, max: number) => string;
}

export class MatrixCanvas2D {
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  private readonly anchorGrid: HTMLElement;
  private readonly getCellColor: (value: number, min: number, max: number) => string;
  private offscreen: HTMLCanvasElement;
  private offCtx: CanvasRenderingContext2D;
  private imageData: ImageData | null = null;

  constructor(options: MatrixCanvas2DOptions) {
    this.canvas = options.canvas;
    const ctx = this.canvas.getContext('2d', { alpha: false });
    if (!ctx) throw new Error('Canvas 2D 不可用');
    this.ctx = ctx;
    this.anchorGrid = options.anchorGrid;
    this.getCellColor = options.getCellColor ?? defaultHeatColor;
    this.offscreen = document.createElement('canvas');
    this.offscreen.width = MATRIX_SIZE;
    this.offscreen.height = MATRIX_SIZE;
    const offCtx = this.offscreen.getContext('2d');
    if (!offCtx) throw new Error('离屏 Canvas 不可用');
    this.offCtx = offCtx;
    this.imageData = offCtx.createImageData(MATRIX_SIZE, MATRIX_SIZE);
  }

  resize(): void {
    const container = this.anchorGrid.parentElement;
    if (!container) return;

    const content = this.measureContentRect(container);
    const w = Math.max(1, Math.round(content.width));
    const h = Math.max(1, Math.round(content.height));

    this.canvas.style.position = 'absolute';
    this.canvas.style.left = `${content.left}px`;
    this.canvas.style.top = `${content.top}px`;
    this.canvas.style.width = `${w}px`;
    this.canvas.style.height = `${h}px`;
    this.canvas.style.pointerEvents = 'none';
    this.canvas.style.zIndex = '5';

    this.canvas.width = w;
    this.canvas.height = h;
  }

  /** 16×16 数据区相对 matrix-container 的位置（与 DOM 标签格对齐） */
  private measureContentRect(container: HTMLElement): {
    left: number;
    top: number;
    width: number;
    height: number;
  } {
    const cRect = container.getBoundingClientRect();
    const lastIdx = MATRIX_SIZE - 1;
    const firstCell = this.anchorGrid.querySelector(
      `.matrix-cell[data-row="0"][data-col="0"]`
    ) as HTMLElement | null;
    const lastCell = this.anchorGrid.querySelector(
      `.matrix-cell[data-row="${lastIdx}"][data-col="${lastIdx}"]`
    ) as HTMLElement | null;

    if (firstCell && lastCell) {
      const firstRect = firstCell.getBoundingClientRect();
      const lastRect = lastCell.getBoundingClientRect();
      return {
        left: firstRect.left - cRect.left,
        top: firstRect.top - cRect.top,
        width: lastRect.right - firstRect.left,
        height: lastRect.bottom - firstRect.top,
      };
    }

    // 回退：grid 为 0.5fr 标签 + 16×1fr 数据，不能用 17 等分
    const gridStyle = getComputedStyle(this.anchorGrid);
    const gap =
      parseFloat(gridStyle.columnGap) ||
      parseFloat(gridStyle.gap) ||
      parseFloat(gridStyle.rowGap) ||
      0;
    const rect = this.anchorGrid.getBoundingClientRect();
    const totalFr = 0.5 + MATRIX_SIZE;
    const availW = rect.width - MATRIX_SIZE * gap;
    const availH = rect.height - MATRIX_SIZE * gap;
    const dataUnitW = availW / totalFr;
    const dataUnitH = availH / totalFr;
    const labelW = dataUnitW * 0.5;
    const labelH = dataUnitH * 0.5;

    return {
      left: rect.left - cRect.left + labelW + gap,
      top: rect.top - cRect.top + labelH + gap,
      width: dataUnitW * MATRIX_SIZE + (MATRIX_SIZE - 1) * gap,
      height: dataUnitH * MATRIX_SIZE + (MATRIX_SIZE - 1) * gap,
    };
  }

  /** 从 16×16 数值矩阵绘制 */
  drawMatrix(
    matrix: number[][],
    stats?: Pick<MatrixComputeResult, 'min' | 'max'>,
    opts: { showValues?: boolean } = {}
  ): void {
    if (!matrix || matrix.length !== MATRIX_SIZE) return;
    this.resize();

    let min = stats?.min ?? Number.POSITIVE_INFINITY;
    let max = stats?.max ?? Number.NEGATIVE_INFINITY;
    if (!Number.isFinite(min) || !Number.isFinite(max)) {
      for (let r = 0; r < MATRIX_SIZE; r++) {
        for (let c = 0; c < MATRIX_SIZE; c++) {
          const v = matrix[r][c];
          if (!Number.isFinite(v)) continue;
          if (v < min) min = v;
          if (v > max) max = v;
        }
      }
    }
    if (!Number.isFinite(min) || !Number.isFinite(max) || min === max) {
      min = 0;
      max = 1;
    }

    const img = this.imageData!;
    const data = img.data;
    for (let r = 0; r < MATRIX_SIZE; r++) {
      for (let c = 0; c < MATRIX_SIZE; c++) {
        const v = matrix[r][c] ?? 0;
        const color = this.getCellColor(v, min, max);
        const rgb = parseCssColor(color);
        const i = (r * MATRIX_SIZE + c) * 4;
        data[i] = rgb.r;
        data[i + 1] = rgb.g;
        data[i + 2] = rgb.b;
        data[i + 3] = 255;
      }
    }
    this.offCtx.putImageData(img, 0, 0);
    this.ctx.imageSmoothingEnabled = true;
    this.ctx.imageSmoothingQuality = 'high';
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
    this.ctx.drawImage(this.offscreen, 0, 0, this.canvas.width, this.canvas.height);

    if (opts.showValues !== false) {
      const cellW = this.canvas.width / MATRIX_SIZE;
      const cellH = this.canvas.height / MATRIX_SIZE;
      this.ctx.fillStyle = '#111827';
      this.ctx.font = `${Math.max(8, Math.floor(cellH * 0.32))}px sans-serif`;
      this.ctx.textAlign = 'center';
      this.ctx.textBaseline = 'middle';
      for (let r = 0; r < MATRIX_SIZE; r++) {
        for (let c = 0; c < MATRIX_SIZE; c++) {
          const v = matrix[r][c];
          if (v === null || v === undefined) continue;
          this.ctx.fillText(String(Math.round(v)), (c + 0.5) * cellW, (r + 0.5) * cellH);
        }
      }
    }
  }

  clear(): void {
    this.ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);
  }
}

function defaultHeatColor(value: number, min: number, max: number): string {
  const t = max > min ? (value - min) / (max - min) : 0.5;
  const hue = 220 - Math.max(0, Math.min(1, t)) * 220;
  return `hsl(${hue}, 78%, ${58 - t * 18}%)`;
}

function parseCssColor(css: string): { r: number; g: number; b: number } {
  if (css.startsWith('#')) {
    const hex = css.slice(1);
    const full = hex.length === 3 ? hex.split('').map((x) => x + x).join('') : hex;
    return {
      r: parseInt(full.slice(0, 2), 16),
      g: parseInt(full.slice(2, 4), 16),
      b: parseInt(full.slice(4, 6), 16),
    };
  }
  const m = css.match(/hsl\(\s*([\d.]+)\s*,\s*([\d.]+)%\s*,\s*([\d.]+)%\s*\)/i);
  if (m) return hslToRgb(parseFloat(m[1]), parseFloat(m[2]) / 100, parseFloat(m[3]) / 100);
  return { r: 255, g: 255, b: 255 };
}

function hslToRgb(h: number, s: number, l: number): { r: number; g: number; b: number } {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = l - c / 2;
  let r = 0;
  let g = 0;
  let b = 0;
  if (h < 60) { r = c; g = x; }
  else if (h < 120) { r = x; g = c; }
  else if (h < 180) { g = c; b = x; }
  else if (h < 240) { g = x; b = c; }
  else if (h < 300) { r = x; b = c; }
  else { r = c; b = x; }
  return {
    r: Math.round((r + m) * 255),
    g: Math.round((g + m) * 255),
    b: Math.round((b + m) * 255),
  };
}
