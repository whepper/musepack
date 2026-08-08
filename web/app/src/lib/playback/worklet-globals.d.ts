// Ambient types for the AudioWorklet global scope (bundled worklet entry).
// The standard DOM/WebWorker libs do not model AudioWorkletProcessor.

declare class AudioWorkletProcessor {
  readonly port: MessagePort;
  constructor();
  process(
    inputs: Float32Array[][],
    outputs: Float32Array[][],
    parameters: Record<string, Float32Array>,
  ): boolean;
}

declare function registerProcessor(
  name: string,
  processorCtor: typeof AudioWorkletProcessor,
): void;

declare const currentTime: number;
declare const sampleRate: number;
