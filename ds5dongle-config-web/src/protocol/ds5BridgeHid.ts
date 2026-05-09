import {
  ConfigBody,
  FEATURE_REPORT_PAYLOAD_SIZE,
  decodeConfigBody,
  encodeConfigBody,
} from "./config";

export const SONY_VENDOR_ID = 0x054c;
export const HORI_VENDOR_ID = 0x0f0d;
export const SWITCH_PRO_VENDOR_ID = 0x057e;
export const SUPPORTED_PRODUCT_IDS = [0x0ce6, 0x0df2] as const;
export const HORI_PRODUCT_IDS = [0x0092] as const;
export const SWITCH_PRO_PRODUCT_IDS = [0x2009] as const;

const REPORT_SET_CONFIG = 0xf6;
const REPORT_GET_CONFIG = 0xf7;
const CMD_UPDATE_CONFIG = 0x01;
const CMD_SAVE_TO_FLASH = 0x02;
const CMD_RECONNECT_USB = 0x03;

export class Ds5BridgeHidClient {
  constructor(public readonly device: HIDDevice) {}

  /** HORI devices use Feature Report without Report ID (report_id=0) */
  private get isHori(): boolean {
    return this.device.vendorId === HORI_VENDOR_ID;
  }

  private get isSwitchPro(): boolean {
    return this.device.vendorId === SWITCH_PRO_VENDOR_ID;
  }

  private get reportSetConfig(): number {
    return this.isHori ? 0 : REPORT_SET_CONFIG;
  }

  private get reportGetConfig(): number {
    return this.isHori ? 0 : REPORT_GET_CONFIG;
  }

  static isSupportedDevice(device: HIDDevice): boolean {
    return (
      (device.vendorId === SONY_VENDOR_ID && SUPPORTED_PRODUCT_IDS.includes(device.productId as 0x0ce6 | 0x0df2)) ||
      (device.vendorId === HORI_VENDOR_ID && HORI_PRODUCT_IDS.includes(device.productId as 0x0092)) ||
      (device.vendorId === SWITCH_PRO_VENDOR_ID && SWITCH_PRO_PRODUCT_IDS.includes(device.productId as 0x2009))
    );
  }

  static async requestDevice(): Promise<Ds5BridgeHidClient> {
    const hid = getHid();
    const devices = await hid.requestDevice({
      filters: [
        ...SUPPORTED_PRODUCT_IDS.map((productId) => ({
          vendorId: SONY_VENDOR_ID,
          productId,
        })),
        ...HORI_PRODUCT_IDS.map((productId) => ({
          vendorId: HORI_VENDOR_ID,
          productId,
        })),
        ...SWITCH_PRO_PRODUCT_IDS.map((productId) => ({
          vendorId: SWITCH_PRO_VENDOR_ID,
          productId,
        })),
      ],
    });

    const device = devices.find(Ds5BridgeHidClient.isSupportedDevice);
    if (!device) {
      throw new Error("No DS5 Bridge device was selected");
    }

    return new Ds5BridgeHidClient(device);
  }

  static async authorizedDevices(): Promise<HIDDevice[]> {
    const devices = await getHid().getDevices();
    return devices.filter(Ds5BridgeHidClient.isSupportedDevice);
  }

  async open(): Promise<void> {
    if (!this.device.opened) {
      await this.device.open();
    }
  }

  async close(): Promise<void> {
    if (this.device.opened) {
      await this.device.close();
    }
  }

  async readConfig(): Promise<ConfigBody> {
    await this.open();
    const report = await this.device.receiveFeatureReport(this.reportGetConfig);
    return decodeConfigBody(report);
  }

  async applyConfig(config: ConfigBody): Promise<void> {
    await this.open();
    const body = encodeConfigBody(config);
    const report = commandReport(CMD_UPDATE_CONFIG);
    report.set(body, 1);
    await this.device.sendFeatureReport(this.reportSetConfig, report);
  }

  async saveToFlash(): Promise<void> {
    await this.open();
    await this.device.sendFeatureReport(this.reportSetConfig, commandReport(CMD_SAVE_TO_FLASH));
  }

  async reconnectUsb(): Promise<void> {
    await this.open();
    await this.device.sendFeatureReport(this.reportSetConfig, commandReport(CMD_RECONNECT_USB));
  }
}

export function webHidAvailable(): boolean {
  return typeof navigator !== "undefined" && Boolean(navigator.hid);
}

export function getDeviceLabel(device: HIDDevice | null): string {
  if (!device) {
    return "No device";
  }

  const vendorId = device.vendorId.toString(16).padStart(4, "0").toUpperCase();
  const productId = device.productId.toString(16).padStart(4, "0").toUpperCase();
  return `${device.productName || "DS5 Bridge"} · ${vendorId}:${productId}`;
}

function getHid(): HID {
  if (!navigator.hid) {
    throw new Error("WebHID is not available in this browser");
  }

  return navigator.hid;
}

function commandReport(command: number): Uint8Array<ArrayBuffer> {
  const report = new Uint8Array(new ArrayBuffer(FEATURE_REPORT_PAYLOAD_SIZE));
  report[0] = command;
  return report;
}
