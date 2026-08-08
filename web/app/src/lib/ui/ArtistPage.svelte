<script lang="ts">
  import { library } from '../bootstrap';
  import ErrorView from './ErrorView.svelte';
  import { onMount } from 'svelte';

  let { artistId }: { artistId: string } = $props();

  let detail = $state<Awaited<ReturnType<typeof library.artistDetail>> | null>(null);
  let error = $state<string | null>(null);

  onMount(() => {
    library
      .artistDetail(artistId)
      .then((d) => (detail = d))
      .catch((e) => (error = e instanceof Error ? e.message : 'Could not load artist.'));
  });
</script>

{#if error}
  <ErrorView message={error} />
{:else if !detail}
  <div class="spinner" role="status" aria-label="Loading artist"></div>
{:else}
  <div class="shelf-header">
    <div>
      <p class="eyebrow">Artist</p>
      <h1>{detail.name}</h1>
    </div>
  </div>
  <h2 class="smallcaps" style="margin:var(--space-4) 0">Albums</h2>
  <div class="shelf-grid">
    {#each detail.albums as album}
      <div>
        <a class="album-card" href={`/albums/${album.id}`}>
          <div class="cover">
            <div class="artwork-fallback" style="background:#7a5c3e" role="img" aria-label={`${album.title} — ${detail.name}`}>
              <span style="font-size:1.6em;letter-spacing:.05em">{album.title.slice(0, 2).toUpperCase()}</span>
            </div>
          </div>
          <div class="title">{album.title}</div>
          <div class="artist">{detail.name}</div>
          <div class="collector">{album.originalReleaseDate?.slice(0, 4) ?? ''}</div>
        </a>
      </div>
    {/each}
  </div>
{/if}
