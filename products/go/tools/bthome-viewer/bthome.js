/**
 * BTHome v2 decoder for the AirGradient Go broadcast (https://bthome.io/format/).
 *
 * Mirrors products/go/main/go_bthome.cpp. The Go sends its TVOC index as a
 * uint16 count (0x3D) and its NOx index as a uint32 count (0x3E); other BTHome
 * devices may use those ids for something else.
 */

/** Service data UUID 0xFCD2 in the 128-bit form Web Bluetooth uses. */
export const BTHOME_UUID = '0000fcd2-0000-1000-8000-00805f9b34fb';
const BTHOME_UUID16 = 0xfcd2;

const AD_TYPE_SERVICE_DATA_16 = 0x16;

const DEVICE_INFO_ENCRYPTED = 0x01;
const DEVICE_INFO_TRIGGER = 0x04;
const DEVICE_INFO_VERSION_SHIFT = 5;
const BTHOME_VERSION = 2;

// id -> [field, byte length, signed, scale]. Unknown ids end decoding because
// their length is unknown.
const OBJECTS = {
  0x00: ['packetId', 1, false, 1],
  0x01: ['battery', 1, false, 1],
  0x02: ['temperature', 2, true, 0.01],
  0x03: ['humidity', 2, false, 0.01],
  0x04: ['pressure', 3, false, 0.01],
  0x0c: ['voltage', 2, false, 0.001],
  0x0d: ['pm25', 2, false, 1],
  0x0e: ['pm10', 2, false, 1],
  0x12: ['co2', 2, false, 1],
  0x2e: ['humidity', 1, false, 1],
  0x3d: ['tvoc', 2, false, 1],
  0x3e: ['nox', 4, false, 1],
};

function readUint(bytes, at, len) {
  let v = 0;
  for (let i = len - 1; i >= 0; i--) {
    v = v * 256 + bytes[at + i];
  }
  return v;
}

/**
 * Decode BTHome service data: the bytes after the 0xFCD2 UUID, starting at the
 * device info byte. This is what Web Bluetooth hands over in
 * `event.serviceData.get(BTHOME_UUID)`.
 *
 * Returns null when the data is not unencrypted BTHome v2. Fields missing from
 * the packet are absent from the result.
 */
export function decodeServiceData(input) {
  const bytes = toBytes(input);
  if (bytes.length < 1) {
    return null;
  }
  const info = bytes[0];
  const version = info >> DEVICE_INFO_VERSION_SHIFT;
  if (version !== BTHOME_VERSION || info & DEVICE_INFO_ENCRYPTED) {
    return null;
  }

  const result = { trigger: Boolean(info & DEVICE_INFO_TRIGGER) };
  let at = 1;
  while (at < bytes.length) {
    const spec = OBJECTS[bytes[at]];
    if (!spec) {
      result.truncated = true;
      break;
    }
    const [field, len, signed, scale] = spec;
    if (at + 1 + len > bytes.length) {
      result.truncated = true;
      break;
    }
    let raw = readUint(bytes, at + 1, len);
    if (signed && raw >= 2 ** (8 * len - 1)) {
      raw -= 2 ** (8 * len);
    }
    result[field] = scale === 1 ? raw : Math.round(raw * scale * 1000) / 1000;
    at += 1 + len;
  }
  return result;
}

/**
 * Decode a complete advertisement payload (AD structures) by finding its BTHome
 * service data. Returns null when there is none.
 */
export function decodeAdvertisement(input) {
  const bytes = toBytes(input);
  let at = 0;
  while (at < bytes.length) {
    const len = bytes[at];
    if (len === 0 || at + 1 + len > bytes.length) {
      break;
    }
    const type = bytes[at + 1];
    if (type === AD_TYPE_SERVICE_DATA_16 && len >= 3) {
      const uuid = bytes[at + 2] | (bytes[at + 3] << 8);
      if (uuid === BTHOME_UUID16) {
        return decodeServiceData(bytes.subarray(at + 4, at + 1 + len));
      }
    }
    at += 1 + len;
  }
  return null;
}

function toBytes(input) {
  if (input instanceof Uint8Array) {
    return input;
  }
  if (input instanceof DataView) {
    return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  }
  return Uint8Array.from(input);
}
