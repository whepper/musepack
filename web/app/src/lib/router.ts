// Minimal History-API router for the SPA (server provides SPA fallback for
// deep links). Routes:
//   /                     albums shelf
//   /albums/:id           album detail (editions)
//   /albums/:id?release=N deep-link to a specific edition
//   /artists              artist list
//   /artists/:id          artist detail
//   /queue                now playing / queue
import { writable, type Readable } from './store';

export interface Route {
  name: string;
  params: Record<string, string>;
  query: URLSearchParams;
}

interface RouteDef {
  pattern: RegExp;
  name: string;
  keys: string[];
}

const DEFINITIONS: RouteDef[] = [
  { pattern: /^\/$/, name: 'albums', keys: [] },
  { pattern: /^\/albums\/?$/, name: 'albums', keys: [] },
  { pattern: /^\/albums\/([0-9]+)$/, name: 'album', keys: ['id'] },
  { pattern: /^\/artists\/?$/, name: 'artists', keys: [] },
  { pattern: /^\/artists\/([0-9]+)$/, name: 'artist', keys: ['id'] },
  { pattern: /^\/queue\/?$/, name: 'queue', keys: [] },
];

export function parseRoute(path: string): Route | null {
  const qIndex = path.indexOf('?');
  const pathOnly = qIndex >= 0 ? path.slice(0, qIndex) : path;
  const query = new URLSearchParams(qIndex >= 0 ? path.slice(qIndex + 1) : '');
  for (const def of DEFINITIONS) {
    const m = def.pattern.exec(pathOnly);
    if (m) {
      const params: Record<string, string> = {};
      def.keys.forEach((k, i) => {
        params[k] = m[i + 1] ?? '';
      });
      return { name: def.name, params, query };
    }
  }
  return { name: 'notfound', params: {}, query };
}

export function createRouter() {
  const current = writable<Route>(parseRoute(location.pathname) ?? { name: 'notfound', params: {}, query: new URLSearchParams() });

  function go(path: string): void {
    history.pushState({}, '', path);
    current.set(parseRoute(path) ?? { name: 'notfound', params: {}, query: new URLSearchParams() });
  }

  window.addEventListener('popstate', () => {
    current.set(parseRoute(location.pathname) ?? { name: 'notfound', params: {}, query: new URLSearchParams() });
  });

  return {
    route: current as Readable<Route>,
    go,
    /** Navigate without pushing history (internal updates). */
    replace(path: string): void {
      history.replaceState({}, '', path);
      current.set(parseRoute(path) ?? { name: 'notfound', params: {}, query: new URLSearchParams() });
    },
  };
}

export type Router = ReturnType<typeof createRouter>;
