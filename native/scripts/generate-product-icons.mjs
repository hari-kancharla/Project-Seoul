#!/usr/bin/env node

// Deterministically rasterize Seoul's simple product mark without relying on a
// checked-in image editor or platform rendering quirks. The 2x master is
// downsampled into every Chromium resource size and the macOS iconset.

import {
  mkdirSync,
  writeFileSync,
} from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const outputRoot = path.resolve(here, '..', 'seoul', 'resources', 'branding');
const masterSize = 2048;
const logicalSize = 1024;

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; ++bit) {
      crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function pngChunk(type, data) {
  const typeBytes = Buffer.from(type, 'ascii');
  const chunk = Buffer.alloc(12 + data.length);
  chunk.writeUInt32BE(data.length, 0);
  typeBytes.copy(chunk, 4);
  data.copy(chunk, 8);
  chunk.writeUInt32BE(crc32(Buffer.concat([typeBytes, data])),
                      8 + data.length);
  return chunk;
}

function encodePng(width, height, rgba) {
  const signature = Buffer.from([
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  ]);
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header[8] = 8;
  header[9] = 6;
  const rows = Buffer.alloc((width * 4 + 1) * height);
  for (let y = 0; y < height; ++y) {
    const rowOffset = y * (width * 4 + 1);
    rows[rowOffset] = 0;
    rgba.copy(rows, rowOffset + 1, y * width * 4, (y + 1) * width * 4);
  }
  return Buffer.concat([
    signature,
    pngChunk('IHDR', header),
    pngChunk('IDAT', zlib.deflateSync(rows, {level: 9})),
    pngChunk('IEND', Buffer.alloc(0)),
  ]);
}

function encodeIcns(images) {
  const chunks = [];
  for (const [type, png] of images) {
    const chunk = Buffer.alloc(8 + png.length);
    chunk.write(type, 0, 4, 'ascii');
    chunk.writeUInt32BE(chunk.length, 4);
    png.copy(chunk, 8);
    chunks.push(chunk);
  }
  const length = 8 + chunks.reduce((total, chunk) => total + chunk.length, 0);
  const header = Buffer.alloc(8);
  header.write('icns', 0, 4, 'ascii');
  header.writeUInt32BE(length, 4);
  return Buffer.concat([header, ...chunks], length);
}

function roundedRectContains(x, y, left, top, width, height, radius) {
  const right = left + width;
  const bottom = top + height;
  const innerLeft = left + radius;
  const innerRight = right - radius;
  const innerTop = top + radius;
  const innerBottom = bottom - radius;
  if ((x >= innerLeft && x <= innerRight && y >= top && y <= bottom) ||
      (y >= innerTop && y <= innerBottom && x >= left && x <= right)) {
    return true;
  }
  const cx = x < innerLeft ? innerLeft : innerRight;
  const cy = y < innerTop ? innerTop : innerBottom;
  return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2;
}

function blendPixel(rgba, index, red, green, blue, alpha) {
  const destinationAlpha = rgba[index + 3] / 255;
  const sourceAlpha = alpha / 255;
  const resultAlpha = sourceAlpha + destinationAlpha * (1 - sourceAlpha);
  if (resultAlpha <= 0) {
    return;
  }
  rgba[index] = Math.round(
      (red * sourceAlpha +
       rgba[index] * destinationAlpha * (1 - sourceAlpha)) / resultAlpha);
  rgba[index + 1] = Math.round(
      (green * sourceAlpha +
       rgba[index + 1] * destinationAlpha * (1 - sourceAlpha)) / resultAlpha);
  rgba[index + 2] = Math.round(
      (blue * sourceAlpha +
       rgba[index + 2] * destinationAlpha * (1 - sourceAlpha)) / resultAlpha);
  rgba[index + 3] = Math.round(resultAlpha * 255);
}

function cubicPoint(start, control1, control2, end, t) {
  const oneMinusT = 1 - t;
  return {
    x: oneMinusT ** 3 * start.x +
       3 * oneMinusT ** 2 * t * control1.x +
       3 * oneMinusT * t ** 2 * control2.x +
       t ** 3 * end.x,
    y: oneMinusT ** 3 * start.y +
       3 * oneMinusT ** 2 * t * control1.y +
       3 * oneMinusT * t ** 2 * control2.y +
       t ** 3 * end.y,
  };
}

function buildMarkPoints() {
  const points = [];
  const line = (start, end, steps) => {
    for (let step = points.length ? 1 : 0; step <= steps; ++step) {
      const t = step / steps;
      points.push({
        x: start.x + (end.x - start.x) * t,
        y: start.y + (end.y - start.y) * t,
      });
    }
  };
  const curve = (start, control1, control2, end, steps) => {
    for (let step = 1; step <= steps; ++step) {
      points.push(cubicPoint(start, control1, control2, end, step / steps));
    }
  };

  const p0 = {x: 732, y: 302};
  const p1 = {x: 420, y: 302};
  const p2 = {x: 250, y: 432};
  const p3 = {x: 420, y: 550};
  const p4 = {x: 604, y: 550};
  const p5 = {x: 774, y: 673};
  const p6 = {x: 600, y: 798};
  const p7 = {x: 292, y: 798};
  line(p0, p1, 48);
  curve(p1, {x: 315, y: 302}, {x: 250, y: 357}, p2, 44);
  curve(p2, {x: 250, y: 507}, {x: 315, y: 550}, p3, 44);
  line(p3, p4, 28);
  curve(p4, {x: 709, y: 550}, {x: 774, y: 593}, p5, 44);
  curve(p5, {x: 774, y: 752}, {x: 700, y: 798}, p6, 44);
  line(p6, p7, 48);
  return points;
}

function drawStrokeMask(mask, points, radius, yOffset = 0) {
  const scale = masterSize / logicalSize;
  const radiusPixels = radius * scale;
  for (let segment = 1; segment < points.length; ++segment) {
    const start = {
      x: points[segment - 1].x * scale,
      y: (points[segment - 1].y + yOffset) * scale,
    };
    const end = {
      x: points[segment].x * scale,
      y: (points[segment].y + yOffset) * scale,
    };
    const minX = Math.max(0, Math.floor(Math.min(start.x, end.x) - radiusPixels));
    const maxX = Math.min(masterSize - 1,
                          Math.ceil(Math.max(start.x, end.x) + radiusPixels));
    const minY = Math.max(0, Math.floor(Math.min(start.y, end.y) - radiusPixels));
    const maxY = Math.min(masterSize - 1,
                          Math.ceil(Math.max(start.y, end.y) + radiusPixels));
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    const lengthSquared = dx * dx + dy * dy;
    for (let y = minY; y <= maxY; ++y) {
      for (let x = minX; x <= maxX; ++x) {
        const projection = lengthSquared === 0 ? 0 :
            Math.max(0, Math.min(1,
                ((x - start.x) * dx + (y - start.y) * dy) / lengthSquared));
        const closestX = start.x + projection * dx;
        const closestY = start.y + projection * dy;
        if ((x - closestX) ** 2 + (y - closestY) ** 2 <=
            radiusPixels ** 2) {
          mask[y * masterSize + x] = 255;
        }
      }
    }
  }
}

function renderMaster() {
  const rgba = Buffer.alloc(masterSize * masterSize * 4);
  const scale = masterSize / logicalSize;
  const gradientStart = {x: 184, y: 128};
  const gradientEnd = {x: 838, y: 904};
  const gradientDx = gradientEnd.x - gradientStart.x;
  const gradientDy = gradientEnd.y - gradientStart.y;
  const gradientLengthSquared =
      gradientDx * gradientDx + gradientDy * gradientDy;
  for (let y = 0; y < masterSize; ++y) {
    for (let x = 0; x < masterSize; ++x) {
      const logicalX = (x + 0.5) / scale;
      const logicalY = (y + 0.5) / scale;
      if (!roundedRectContains(logicalX, logicalY, 72, 58, 880, 880, 220)) {
        continue;
      }
      const t = Math.max(0, Math.min(1,
          ((logicalX - gradientStart.x) * gradientDx +
           (logicalY - gradientStart.y) * gradientDy) /
          gradientLengthSquared));
      const midpoint = Math.min(1, t / 0.48);
      const tail = Math.max(0, (t - 0.48) / 0.52);
      const red = t <= 0.48 ?
          Math.round(0x82 + (0x5f - 0x82) * midpoint) :
          Math.round(0x5f + (0x34 - 0x5f) * tail);
      const green = t <= 0.48 ?
          Math.round(0xaa + (0x8d - 0xaa) * midpoint) :
          Math.round(0x8d + (0x5f - 0x8d) * tail);
      const blue = t <= 0.48 ?
          Math.round(0x8e + (0x6e - 0x8e) * midpoint) :
          Math.round(0x6e + (0x47 - 0x6e) * tail);
      const index = (y * masterSize + x) * 4;
      rgba[index] = red;
      rgba[index + 1] = green;
      rgba[index + 2] = blue;
      rgba[index + 3] = 255;
      if (logicalY < 410) {
        const highlightAlpha = Math.round(36 * (1 - logicalY / 410));
        blendPixel(rgba, index, 255, 255, 255, highlightAlpha);
      }
      const inInner =
          roundedRectContains(logicalX, logicalY, 86, 72, 852, 852, 206);
      if (!inInner) {
        blendPixel(rgba, index, 233, 241, 232, 46);
      }
    }
  }

  const points = buildMarkPoints();
  const shadowMask = new Uint8Array(masterSize * masterSize);
  drawStrokeMask(shadowMask, points, 58, 14);
  const markMask = new Uint8Array(masterSize * masterSize);
  drawStrokeMask(markMask, points, 52);
  for (let pixel = 0; pixel < shadowMask.length; ++pixel) {
    const index = pixel * 4;
    if (shadowMask[pixel]) {
      blendPixel(rgba, index, 23, 54, 36, 74);
    }
    if (markMask[pixel]) {
      blendPixel(rgba, index, 247, 244, 234, 255);
    }
  }
  return rgba;
}

function renderFaviconMaster() {
  const rgba = Buffer.alloc(masterSize * masterSize * 4);
  const markMask = new Uint8Array(masterSize * masterSize);
  drawStrokeMask(markMask, buildMarkPoints(), 52);
  for (let pixel = 0; pixel < markMask.length; ++pixel) {
    if (!markMask[pixel]) {
      continue;
    }
    const index = pixel * 4;
    rgba[index] = 255;
    rgba[index + 1] = 255;
    rgba[index + 2] = 255;
    rgba[index + 3] = markMask[pixel];
  }
  return rgba;
}

function downsample(master, outputSize) {
  const factor = masterSize / outputSize;
  const output = Buffer.alloc(outputSize * outputSize * 4);
  for (let outputY = 0; outputY < outputSize; ++outputY) {
    for (let outputX = 0; outputX < outputSize; ++outputX) {
      const startX = Math.floor(outputX * factor);
      const endX = Math.floor((outputX + 1) * factor);
      const startY = Math.floor(outputY * factor);
      const endY = Math.floor((outputY + 1) * factor);
      const totals = [0, 0, 0, 0];
      let count = 0;
      for (let y = startY; y < endY; ++y) {
        for (let x = startX; x < endX; ++x) {
          const sourceIndex = (y * masterSize + x) * 4;
          for (let channel = 0; channel < 4; ++channel) {
            totals[channel] += master[sourceIndex + channel];
          }
          ++count;
        }
      }
      const destinationIndex = (outputY * outputSize + outputX) * 4;
      for (let channel = 0; channel < 4; ++channel) {
        output[destinationIndex + channel] =
            Math.round(totals[channel] / count);
      }
    }
  }
  return output;
}

function writePng(relativePath, size, master) {
  const destination = path.join(outputRoot, relativePath);
  mkdirSync(path.dirname(destination), {recursive: true});
  writeFileSync(destination, encodePng(size, size, downsample(master, size)));
}

const master = renderMaster();
const faviconMaster = renderFaviconMaster();
for (const size of [16, 24, 32, 48, 64, 128, 192, 256, 512, 1024]) {
  writePng(`generated/seoul-product-logo-${size}.png`, size, master);
}
for (const size of [16, 24, 32, 48, 64, 128, 256]) {
  writePng(`product_logo_${size}.png`, size, master);
}
writePng('ntp_favicon_32.png', 32, faviconMaster);
for (const size of [24, 32, 48, 64, 128, 256]) {
  writePng(`linux/product_logo_${size}.png`, size, master);
}
writePng('chromeos/chrome_app_icon_32.png', 32, master);
writePng('chromeos/chrome_app_icon_192.png', 192, master);

const icnsImages = [
  ['ic04', encodePng(16, 16, downsample(master, 16))],
  ['ic11', encodePng(32, 32, downsample(master, 32))],
  ['ic05', encodePng(32, 32, downsample(master, 32))],
  ['ic12', encodePng(64, 64, downsample(master, 64))],
  ['ic07', encodePng(128, 128, downsample(master, 128))],
  ['ic13', encodePng(256, 256, downsample(master, 256))],
  ['ic08', encodePng(256, 256, downsample(master, 256))],
  ['ic14', encodePng(512, 512, downsample(master, 512))],
  ['ic09', encodePng(512, 512, downsample(master, 512))],
  ['ic10', encodePng(1024, 1024, downsample(master, 1024))],
];
mkdirSync(path.join(outputRoot, 'mac'), {recursive: true});
writeFileSync(path.join(outputRoot, 'mac', 'app.icns'),
              encodeIcns(icnsImages));

console.log(`Generated Seoul product icons in ${outputRoot}`);
