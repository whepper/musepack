// Application composition root: wires the API client, session, stores,
// playback controller and router into singletons the Svelte components use.
import { ApiClient } from './api/client';
import { bindSession, type SessionStore } from './auth/session';
import { createLibraryStore, type LibraryStore } from './state/library';
import { createQueueStore, type QueueStore } from './state/queue';
import { PlayerController } from './playback/controller';
import { createRouter, type Router } from './router';

export const api = new ApiClient({});
export const session: SessionStore = bindSession(api);
export const library: LibraryStore = createLibraryStore(api);
export const queue: QueueStore = createQueueStore();
export const player = new PlayerController(queue, {});
/** The player model as a store (subscribe via `$playerModel`). */
export const playerModel = player.model;
export const router: Router = createRouter();

/** Test/dev instrumentation exposed for Playwright + perf reporting. */
export interface MusicPackDebug {
  api: ApiClient;
  session: SessionStore;
  library: LibraryStore;
  queue: QueueStore;
  player: PlayerController;
  router: Router;
}

declare global {
  interface Window {
    __musicpack?: MusicPackDebug;
  }
}

export function exposeDebug(): void {
  window.__musicpack = { api, session, library, queue, player, router };
}
