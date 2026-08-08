import { writable } from '../store';
import { ApiClient } from '../api/client';

export type AuthState = 'checking' | 'authenticated' | 'unauthenticated';

export interface SessionModel {
  state: AuthState;
  /** Human-readable reason for an auth failure (shown on the sign-in screen). */
  message?: string;
}

const initial: SessionModel = { state: 'checking' };

function createSessionStore(api: ApiClient) {
  const store = writable<SessionModel>(initial);

  return {
    ...store,
    async boot(): Promise<void> {
      try {
        await api.session();
        store.set({ state: 'authenticated' });
      } catch (e) {
        store.set({
          state: 'unauthenticated',
          message:
            e instanceof Error && e.message.includes('expired')
              ? undefined
              : undefined,
        });
      }
    },
    async authenticate(token: string): Promise<void> {
      await api.createSession(token);
      store.set({ state: 'authenticated' });
    },
    async logout(): Promise<void> {
      try {
        await api.logout();
      } catch {
        /* best effort — the session may already be gone */
      } finally {
        store.set({ state: 'unauthenticated' });
      }
    },
    expire(reason = 'Your session has expired. Please sign in again.'): void {
      store.set({ state: 'unauthenticated', message: reason });
    },
  };
}

export type SessionStore = ReturnType<typeof createSessionStore>;

export { createSessionStore };

let sessionStore: SessionStore | null = null;

export function bindSession(api: ApiClient): SessionStore {
  sessionStore = createSessionStore(api);
  api.onUnauthorized = () => {
    // The auth check (GET /session) deliberately does not retrigger expiry
    // loops; guard against re-entrancy.
    if (sessionStore && sessionStore.get().state === 'authenticated') {
      sessionStore.expire();
    }
  };
  return sessionStore;
}

export function getSession(): SessionStore {
  if (!sessionStore) throw new Error('session not bound (call bindSession first)');
  return sessionStore;
}
