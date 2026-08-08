import { describe, it, expect, beforeAll } from 'vitest';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { readFileSync } from 'node:fs';
import { RingBuffer } from '../../app/src/lib/playback/ring-buffer';

const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

let wasm: (() => Promise<Record<string, any>>) | null = null;
let fixtureA: string | null = null;
let fixtureB: string | null = null;

beforeAll(() => {
  const candidates = [
    process.env.MUSICPACK_WASM_JS,
    path.join(ROOT, 'build-wasm/wasm/musepack.js'),
  ].filter(Boolean) as string[];
  const mod = candidates.find((p) => {
    try {
      require(p);
      return true;
    } catch {
      return false;
    }
  });
  const fa = path.join(ROOT, 'tests/fixtures/sine44-q5.mpc');
  const fb = path.join(ROOT, 'tests/fixtures/sine44-q7.mpc');
  if (mod && readFileSync(fa).length && readFileSync(fb).length) {
    wasm = require(mod) as () => Promise<Record<string, any>>;
    fixtureA = fa;
    fixtureB = fb;
  }
});

async function decodeAll(Module: Record<string, any>, h: number, channels: number): Promise<Float32Array> {
  const pcmPtr = Module._malloc(1152 * channels * 4);
  const out: number[] = [];
  for (;;) {
    const frames = (await Module._mpc_wasm_read(h, pcmPtr, 1152)) as number;
    if (frames < 0) break; // EOF
    if (frames === 0) break;
    const view = new Float32Array(Module.HEAPF32.buffer, pcmPtr, frames * channels);
    for (let i = 0; i < view.length; i++) out.push(view[i] ?? 0);
  }
  Module._free(pcmPtr);
  return Float32Array.from(out);
}

async function decode(Module: Record<string, any>, file: string) {
  const bytes = readFileSync(file);
  const h = Module._mpc_wasm_create();
  const memPtr = Module._malloc(bytes.length);
  Module.HEAPU8.set(bytes, memPtr);
  const err = await Module._mpc_wasm_open(h, memPtr, bytes.length);
  if (err !== 0) throw new Error(`open: ${err}`);
  const channels = Module._mpc_wasm_channels(h);
  const length = Module._mpc_wasm_length_samples(h);
  const rate = Module._mpc_wasm_sample_rate(h);
  const pcm = await decodeAll(Module, h, channels);
  Module._free(memPtr);
  Module._mpc_wasm_destroy(h);
  return { pcm, channels, length, rate };
}

describe('wasm + ring (gapless feed)', () => {
  it('needs the wasm module built (skip otherwise)', () => {
    expect(fixtureA).toBeTruthy();
    expect(fixtureB).toBeTruthy();
  });

  it('feeds two decoded tracks through the ring losslessly at the boundary', async () => {
    if (!wasm || !fixtureA || !fixtureB) return;
    const Module = await wasm();
    const A = await decode(Module, fixtureA);
    const B = await decode(Module, fixtureB);
    expect(A.channels).toBe(B.channels);
    expect(A.rate).toBe(B.rate);

    // The controller's gapless handoff: track A PCM followed immediately by
    // track B PCM into the same ring. Exactly like the AudioWorklet path.
    const ring = new RingBuffer(A.rate * 8, A.channels);
    expect(ring.writeInterleaved(A.pcm)).toBe(A.pcm.length / A.channels);
    expect(ring.writeInterleaved(B.pcm)).toBe(B.pcm.length / A.channels);

    const drained = new Float32Array(A.pcm.length + B.pcm.length);
    expect(ring.readInterleaved(drained, drained.length / A.channels)).toBe(
      (A.pcm.length + B.pcm.length) / A.channels,
    );

    let same = true;
    for (let i = 0; i < drained.length; i++) {
      const expectVal = i < A.pcm.length ? A.pcm[i] : B.pcm[i - A.pcm.length];
      if (drained[i] !== expectVal) {
        same = false;
        break;
      }
    }
    expect(same).toBe(true);
    // The exact boundary sample adjacency.
    expect(drained[A.pcm.length - 1]).toBe(A.pcm[A.pcm.length - 1]);
    expect(drained[A.pcm.length]).toBe(B.pcm[0]);
  });
});
