# libmusepack WASM playback demo

A minimal proof-of-concept that plays a `.mpc` file in the browser:

```text
.mpc bytes
    ↓
libmusepack.wasm (decoder core + wrapper, in a Worker)
    ↓
decoded float PCM (message chunks)
    ↓
Web Audio (AudioBufferSourceNodes)
    ↓
audible playback
```

Decoding runs off the UI thread in `worker.js`. The main thread forwards PCM
to a `PcmSink`; the active sink uses `AudioBufferSourceNode`s (the approved
first proof-of-concept path). The intended production architecture — worker
decoder → `AudioWorkletNode` — is sketched in `audio-worklet.js` and needs no
changes to the worker's message protocol.

## Build the WASM module

Requires [Emscripten](https://emscripten.org/) (`emcc`/`emcmake` on PATH).

```sh
./build.sh
```

This configures and builds the decoder for wasm and copies `musepack.js` +
`musepack.wasm` into this directory. (Those two generated files are
gitignored.)

Alternatively build manually and copy:

```sh
emcmake cmake -S .. -B ../build-wasm
cmake --build ../build-wasm --target musepack_wasm -j
cp ../build-wasm/wasm/musepack.js ../build-wasm/wasm/musepack.wasm .
```

## Run

Serve the demo over HTTP (fetch of the `.wasm` binary needs a server; a plain
`file://` open will not work):

```sh
python3 -m http.server 8000
# or: npx serve .
```

Then open http://localhost:8000/, choose a `.mpc` file, press Play.

No encoder fixtures ship with the demo; encode any WAV first with the native
`mpcenc` tool (e.g. `mpcenc in.wav out.mpc`), or pick an existing SV8 `.mpc`.

## What it exercises

- module init and fixture open
- stream info (sample rate, channels, length)
- chunked decode off the UI thread
- play / pause / stop
- seeking (scrubber + seek button)
- playback position readout
- end-of-stream handling (replays from the start on Play)

## Intended architecture (next step)

```text
browser UI  →  Worker (decode)  →  AudioWorklet (playback)  →  speakers
```

The `AudioWorkletSink` stub in `main.js` and the processor in
`audio-worklet.js` mark where that lands; the worker stays a pure decoder.
