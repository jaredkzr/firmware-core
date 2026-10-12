// Run with: node --test products/go/tools/bthome-viewer/bthome.test.mjs
// Vectors match products/go/tests/go_bthome.tests.cpp.

import assert from 'node:assert/strict';
import test from 'node:test';

import { decodeAdvertisement, decodeServiceData } from './bthome.js';

test('decodes a full Go advertisement', () => {
  const adv = [
    0x02, 0x01, 0x06,
    0x1b, 0x16, 0xd2, 0xfc, 0x40,
    0x00, 0x07,
    0x01, 0x57,
    0x02, 0x2a, 0x09,
    0x0d, 0x0d, 0x00,
    0x12, 0x67, 0x02,
    0x2e, 0x2e,
    0x3d, 0x78, 0x00,
    0x3e, 0x01, 0x00, 0x00, 0x00,
  ];
  assert.equal(adv.length, 31);
  assert.deepEqual(decodeAdvertisement(adv), {
    trigger: false,
    packetId: 7,
    battery: 87,
    temperature: 23.46,
    pm25: 13,
    co2: 615,
    humidity: 46,
    tvoc: 120,
    nox: 1,
  });
});

test('decodes a packet with only the packet id', () => {
  const adv = [0x02, 0x01, 0x06, 0x06, 0x16, 0xd2, 0xfc, 0x40, 0x00, 0xff];
  assert.deepEqual(decodeAdvertisement(adv), { trigger: false, packetId: 255 });
});

test('decodes negative temperatures', () => {
  // -5.25 °C = -525 = 0xFDF3
  assert.equal(decodeServiceData([0x40, 0x02, 0xf3, 0xfd]).temperature, -5.25);
});

test('decodes Web Bluetooth DataView service data', () => {
  const view = new DataView(new Uint8Array([0x40, 0x00, 0x03, 0x12, 0x20, 0x03]).buffer);
  assert.deepEqual(decodeServiceData(view), { trigger: false, packetId: 3, co2: 800 });
});

test('rejects encrypted and non-v2 data', () => {
  assert.equal(decodeServiceData([0x41, 0x00, 0x01]), null);
  assert.equal(decodeServiceData([0x20, 0x00, 0x01]), null);
  assert.equal(decodeServiceData([]), null);
});

test('stops at unknown or truncated objects', () => {
  assert.deepEqual(decodeServiceData([0x40, 0x00, 0x01, 0xf0, 0x12, 0x20, 0x03]), {
    trigger: false,
    packetId: 1,
    truncated: true,
  });
  assert.deepEqual(decodeServiceData([0x40, 0x00, 0x01, 0x12, 0x20]), {
    trigger: false,
    packetId: 1,
    truncated: true,
  });
});

test('ignores advertisements without BTHome service data', () => {
  assert.equal(decodeAdvertisement([0x02, 0x01, 0x06, 0x03, 0x16, 0x0f, 0x18]), null);
});
