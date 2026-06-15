export function portPathToMxtDevice(portPath: string): string | undefined {
  const p = (portPath || '').trim();
  if (!p) return undefined;
  if (p.startsWith('winusb:')) {
    const raw = p.slice('winusb:'.length).trim();
    if (raw.startsWith('usb:')) return raw;
  }
  if (p.startsWith('usb:')) return p;
  if (p.startsWith('serial:')) return p;
  if (/^COM\d+$/i.test(p)) return `serial:${p.toUpperCase()}`;
  if (p.startsWith('/dev/')) return `serial:${p}`;
  return undefined;
}

export interface BridgeSessionSnapshot {
  portPath: string;
  mxtDevice: string;
  readActive: boolean;
  kind: 'winusb' | 'serial';
}

export interface MxtUsbCoordState {
  getBridgeSessions: () => BridgeSessionSnapshot[];
  disconnectBridge: (session: BridgeSessionSnapshot) => Promise<void>;
  connectBridge: (session: BridgeSessionSnapshot) => Promise<{ path: string }>;
  startBridgeRead: (session: BridgeSessionSnapshot) => void;
}

export function getDefaultMxtDeviceFromSessions(sessions: BridgeSessionSnapshot[]): string | undefined {
  return sessions[0]?.mxtDevice;
}

export async function suspendBridgeForMxtApp(
  state: MxtUsbCoordState,
  targetDevice?: string
): Promise<BridgeSessionSnapshot[]> {
  const normalizedTarget = (targetDevice || '').trim().toLowerCase();
  const suspended: BridgeSessionSnapshot[] = [];

  for (const session of state.getBridgeSessions()) {
    if (normalizedTarget && session.mxtDevice.toLowerCase() !== normalizedTarget) continue;
    await state.disconnectBridge(session);
    suspended.push(session);
  }

  if (suspended.length > 0) {
    await new Promise((resolve) => setTimeout(resolve, 250));
  }

  return suspended;
}

export async function restoreBridgeAfterMxtApp(
  state: MxtUsbCoordState,
  suspended: BridgeSessionSnapshot[]
): Promise<void> {
  for (const session of suspended) {
    try {
      await state.connectBridge(session);
      if (session.readActive) state.startBridgeRead(session);
    } catch (_) {}
  }
}

export function createCoordinatedMxtAppRunner(
  state: MxtUsbCoordState,
  runMxtApp: (
    args: string[],
    device?: string,
    timeout?: number
  ) => Promise<{ success: boolean; returncode: number; stdout: string; stderr: string }>
) {
  return async function runMxtAppCoordinated(
    args: string[],
    device?: string,
    timeout = 60000
  ): Promise<{ success: boolean; returncode: number; stdout: string; stderr: string; device?: string; resumed?: string[] }> {
    const sessions = state.getBridgeSessions();
    const resolvedDevice = (device || '').trim() || getDefaultMxtDeviceFromSessions(sessions);
    const suspended = await suspendBridgeForMxtApp(state, resolvedDevice || undefined);

    try {
      const result = await runMxtApp(args, resolvedDevice || undefined, timeout);
      return {
        ...result,
        device: resolvedDevice || undefined,
        resumed: suspended.map((s) => s.portPath)
      };
    } finally {
      await restoreBridgeAfterMxtApp(state, suspended);
    }
  };
}

// 兼容旧命名
export type WinUsbSessionSnapshot = BridgeSessionSnapshot;
export const suspendWinUsbForMxtApp = suspendBridgeForMxtApp;
export const restoreWinUsbAfterMxtApp = restoreBridgeAfterMxtApp;
