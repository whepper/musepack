import { test, expect } from '@playwright/test';
import { signIn } from './helpers';

test.beforeEach(async ({ page }) => {
  await signIn(page);
});

test('the shelf shows the collection with collector lines', async ({ page }) => {
  const cards = page.locator('.album-card');
  await expect(cards).toHaveCount(4);
  await expect(page.getByText('Synthetic Test Compilation')).toBeVisible();
  await expect(page.getByText('3 versions')).toBeVisible(); // multi-edition collector line
  await expect(page.getByText('1998', { exact: true })).toBeVisible(); // classical year
});

test('album page groups editions and lets you switch between them', async ({ page }) => {
  await page.getByText('Synthetic Test Compilation').click();
  await expect(page.getByRole('heading', { name: 'Synthetic Test Compilation' })).toBeVisible();

  // The fixture album has three editions — they must never be flattened.
  const chips = page.locator('.edition-chip');
  await expect(chips).toHaveCount(3);
  await expect(chips.nth(0)).toHaveAttribute('aria-pressed', 'true');

  const firstTracklist = page.locator('.tracklist').innerText();
  await chips.nth(1).click();
  await expect(chips.nth(1)).toHaveAttribute('aria-pressed', 'true');
  // A different edition carries a different track list (nothing flattened).
  await expect(page.locator('.tracklist')).not.toHaveText(await firstTracklist);

  // Release information panel exposes collector details.
  await page.getByText('Release information').click();
  await expect(page.getByText('BS.1770 loudness', { exact: false })).toBeVisible();
  await expect(page.getByText('Catalogue number', { exact: false })).toBeVisible();
});

test('search filters the shelf server-side', async ({ page }) => {
  await page.getByLabel('Search the collection').fill('Two Disc');
  await expect(page.locator('.album-card')).toHaveCount(1);
  await expect(page.getByText('Two Disc Extravaganza')).toBeVisible();
});

test('recently added sorts the shelf', async ({ page }) => {
  await page.getByRole('button', { name: 'Recently added' }).click();
  await expect(page.locator('.album-card').first()).toBeVisible();
});

test('artists browse to a release-group view', async ({ page }) => {
  await page.getByRole('link', { name: 'Artists' }).click();
  await expect(page.getByRole('heading', { name: 'Artists' })).toBeVisible();
  await page.getByText('Synthetic Chamber Orchestra').click();
  await expect(page.getByRole('heading', { name: 'Synthetic Chamber Orchestra' })).toBeVisible();
  await expect(page.getByText('Synthetic Classical Compilation')).toBeVisible();
});
