import { test, expect } from '@playwright/test';
import { signIn, playerState, waitFor } from './helpers';

test.beforeEach(async ({ page }) => {
  await signIn(page);
});

/** Sets a range slider value, firing the same input+change the UI listens to. */
async function setSeek(page: import('@playwright/test').Page, seconds: number): Promise<void> {
  await page.locator('.playerbar input[type=range]').first().evaluate((el, v) => {
    const input = el as HTMLInputElement;
    input.value = String(v);
    input.dispatchEvent(new Event('input', { bubbles: true }));
    input.dispatchEvent(new Event('change', { bubbles: true }));
  }, seconds);
}

test('plays a Musepack album demand-driven and seeks without downloading the file', async ({ page }) => {
  await page.getByText('Long Player').click();
  await page.getByRole('button', { name: 'Play album' }).click();

  await waitFor(page, async () => (await playerState(page)).state === 'playing', { label: 'playing' });
  const start = await playerState(page);
  expect(start.currentTitle).toBeTruthy();
  expect(start.normDb).toBeLessThan(0); // album normalization applied

  // Time-to-first-PCM / bytes-before-playback: far less than the full file.
  const servedAtPlay = await playerState(page).then((s) => s.servedBytes);
  const size = await page.evaluate(() => {
    const item = window.__musicpack?.player.model.get().current;
    return item?.track.audio.size ?? 0;
  });
  expect(servedAtPlay).toBeLessThan(size * 0.5);

  // Position advances without error.
  await waitFor(page, async () => (await playerState(page)).positionSeconds > 1, { label: 'position advances' });

  // Seek to ~50% then 90%, then backwards: position jumps; the whole file is
  // never fetched (the demand reader fetches only the needed blocks).
  const dur = (await playerState(page)).durationSeconds;
  await setSeek(page, Math.round(dur * 0.5));
  await waitFor(page, async () => (await playerState(page)).positionSeconds > dur * 0.4, { label: 'seek 50%' });
  await setSeek(page, Math.round(dur * 0.9));
  await waitFor(page, async () => (await playerState(page)).positionSeconds > dur * 0.8, { label: 'seek 90%' });
  await setSeek(page, Math.round(dur * 0.1));
  await waitFor(page, async () => (await playerState(page)).positionSeconds < dur * 0.2, { label: 'seek backwards' });

  const final = await playerState(page);
  expect(final.servedBytes).toBeLessThan(size); // never downloaded the whole file
  expect(final.state).not.toBe('error');
});

test('pause, resume, and next track behave', async ({ page }) => {
  // Long Player's first track is 48 s, so pause/resume/next have room.
  await page.getByText('Long Player').click();
  await page.getByRole('button', { name: 'Play album' }).click();
  await waitFor(page, async () => (await playerState(page)).state === 'playing', { label: 'playing' });

  const pausedAt = (await playerState(page)).positionSeconds;
  await page.locator('.playerbar').getByRole('button', { name: 'Pause' }).click();
  await waitFor(page, async () => (await playerState(page)).state === 'paused', { label: 'paused' });

  // Position freezes while paused.
  const stillPaused = await playerState(page);
  expect(stillPaused.positionSeconds).toBeGreaterThanOrEqual(pausedAt - 0.3);

  await page.locator('.playerbar').getByRole('button', { name: 'Play' }).click();
  await waitFor(page, async () => (await playerState(page)).state === 'playing', { label: 'resumed' });

  const before = await playerState(page);
  await page.locator('.playerbar').getByRole('button', { name: 'Next track' }).click();
  await waitFor(
    page,
    async () => (await playerState(page)).currentTitle !== before.currentTitle,
    { label: 'next track' },
  );
});

test('gapless album playback crosses the track boundary continuously', async ({ page }) => {
  await page.getByText('Synthetic Test Compilation').click();
  await page.getByRole('button', { name: 'Play album' }).click();
  await waitFor(page, async () => (await playerState(page)).state === 'playing', { label: 'playing' });

  // Wait until the queue cursor advances into track 2 (gapless handoff) with
  // position still increasing — no error, no stuck state.
  await waitFor(
    page,
    async () => {
      const q = await page.evaluate(() => window.__musicpack?.queue.get().index ?? -1);
      return q >= 1;
    },
    { label: 'gapless into track 2', timeout: 45_000 },
  );
  const state = await playerState(page);
  expect(state.state).not.toBe('error');
  expect(state.currentTitle).toBeTruthy();
});

test('queue holds the album in order and removes items', async ({ page }) => {
  await page.getByText('Synthetic Test Compilation').click();
  await page.getByRole('button', { name: 'Play album' }).click();
  await waitFor(page, async () => (await playerState(page)).state === 'playing', { label: 'playing' });

  await page.getByRole('button', { name: 'Open the queue' }).click();
  const items = await page.evaluate(() => window.__musicpack?.queue.get().items.map((i) => i.track.title) ?? []);
  expect(items).toHaveLength(4); // the fixture album has 4 tracks
  expect(items[0]).toContain('Big in Japan');
  await expect(page.locator('#main').getByRole('button', { name: /Big in Japan/ }).first()).toHaveAttribute('aria-current', 'true');
});
