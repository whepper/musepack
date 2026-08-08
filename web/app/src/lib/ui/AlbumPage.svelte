<script lang="ts">
  import { library, player, playerModel, router } from '../bootstrap';
  import Artwork from './Artwork.svelte';
  import EditionSelector from './EditionSelector.svelte';
  import TrackList from './TrackList.svelte';
  import ReleaseInfo from './ReleaseInfo.svelte';
  import ErrorView from './ErrorView.svelte';
  import { formatDate, mediumLabel, yearOf } from '../format';
  import { onMount } from 'svelte';

  let { albumId, releaseParam }: { albumId: string; releaseParam?: string } = $props();

  let detail = $state<Awaited<ReturnType<typeof library.albumDetail>> | null>(null);
  let rel = $state<Awaited<ReturnType<typeof library.releaseDetail>> | null>(null);
  let error = $state<string | null>(null);
  let loading = $state(true);
  let selectedId = $state(-1);
  let viewerOpen = $state(false);

  const currentTrackId = $derived($playerModel.current?.track.id);

  onMount(() => {
    void load();
  });

  async function load(): Promise<void> {
    loading = true;
    error = null;
    detail = null;
    rel = null;
    try {
      detail = await library.albumDetail(albumId);
      const releases = detail.releases;
      if (releases.length === 0) throw new Error('This album has no playable releases in the collection.');
      const paramId = releaseParam ? Number(releaseParam) : NaN;
      selectedId =
        !Number.isNaN(paramId) && releases.some((r) => r.id === paramId)
          ? paramId
          : library.selectedRelease(detail.album.id, releases);
      library.selectRelease(detail.album.id, selectedId);
      await loadRelease(selectedId);
    } catch (e) {
      error = e instanceof Error ? e.message : 'Could not open this album.';
    } finally {
      loading = false;
    }
  }

  async function loadRelease(id: number): Promise<void> {
    rel = await library.releaseDetail(id);
  }

  async function selectEdition(id: number): Promise<void> {
    selectedId = id;
    library.selectRelease(detail?.album.id ?? 0, id);
    router.replace(`/albums/${albumId}?release=${id}`);
    await loadRelease(id);
  }

  function playFrom(index: number): void {
    if (!rel || !detail) return;
    const artist = detail.album.artists[0]?.name ?? '';
    void player.playAlbum(rel, detail.album.title, artist, index);
    router.go('/queue');
  }

  const heroArt = $derived(rel?.artwork.find((a) => a.role === 'front') ?? rel?.artwork[0]);
  const title = $derived(detail?.album.title ?? '');
  const artist = $derived(detail?.album.artists.map((a) => a.name).join(', ') ?? '');
</script>

{#if loading}
  <div class="spinner" role="status" aria-label="Loading album"></div>
{:else if error}
  <ErrorView message={error} detail="It may have been removed from the library, or the server is unreachable." />
{:else if detail && rel}
  <article>
    <div class="album-hero">
      <div class="cover">
        <button style="width:100%;padding:0;display:block" onclick={() => (viewerOpen = true)}
          aria-label={`View artwork for ${title}`}>
          <Artwork src={heroArt?.url} alt={`${title} — front cover`} label={title} />
        </button>
      </div>
      <div class="album-heading">
        <p class="eyebrow">{detail.album.releaseType ?? 'album'}</p>
        <h1>{title}</h1>
        <p class="artist">{artist}</p>
        <p class="edition-line">
          {#if rel.releaseDate}{yearOf(rel.releaseDate)}{/if}
          {#if rel.media[0]?.format} · {mediumLabel(rel.media[0].format)}{/if}
          {#if rel.country} · {rel.country}{/if}
          {#if rel.label} · {rel.label}{/if}
          {#if rel.catalogueNumber} · {rel.catalogueNumber}{/if}
        </p>
        <div style="display:flex;gap:var(--space-3);margin-top:var(--space-4)">
          <button class="btn" onclick={() => playFrom(0)}>Play album</button>
          <button class="btn ghost" onclick={() => (viewerOpen = true)}>View artwork</button>
        </div>
      </div>
    </div>

    <EditionSelector releases={detail.releases} selectedId={selectedId} onSelect={selectEdition} />

    <h2 class="smallcaps" style="margin-bottom:var(--space-2)">
      {rel.media.length} medium · {rel.media.reduce((n, m) => n + m.tracks.length, 0)} tracks
    </h2>

    <TrackList release={rel} currentTrackId={currentTrackId} onPlay={playFrom} />

    <ReleaseInfo release={rel} />

    {#if viewerOpen}
      <div
        class="viewer"
        role="dialog"
        aria-modal="true"
        aria-label="Artwork viewer"
        tabindex="-1"
        style="position:fixed;inset:0;background:rgba(20,18,15,0.92);z-index:40;display:flex;align-items:center;justify-content:center;flex-direction:column;gap:var(--space-4);padding:var(--space-5)"
        onclick={(e) => {
          if (e.target === e.currentTarget) viewerOpen = false;
        }}
        onkeydown={(e) => {
          if (e.key === 'Escape') viewerOpen = false;
        }}
      >
        <button
          style="position:absolute;top:var(--space-4);right:var(--space-5);color:var(--paper);font-size:var(--fs-xl)"
          aria-label="Close artwork viewer"
          onclick={() => (viewerOpen = false)}>✕</button>
        {#each rel.artwork as art, i (art.id)}
          <div style="max-width:min(720px, 90vw);text-align:center">
            <img src={art.url} alt={`${title} — ${art.role ?? 'artwork'} ${i + 1}`} style="max-width:100%;max-height:70vh;object-fit:contain;border-radius:4px">
            <p class="smallcaps" style="color:var(--paper)">{art.role ?? 'artwork'}</p>
          </div>
        {/each}
      </div>
    {/if}
  </article>
{:else}
  <ErrorView message="Album not found" detail="It may have been removed from the collection." />
{/if}
