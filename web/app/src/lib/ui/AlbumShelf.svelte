<script lang="ts">
  import { library, router } from '../bootstrap';
  import AlbumCard from './AlbumCard.svelte';
  import SearchBox from './SearchBox.svelte';
  import ErrorView from './ErrorView.svelte';
  import { onMount } from 'svelte';

  const shelf = library.shelf;
  let q = $state('');
  let sort = $state('');

  let sentinel: HTMLDivElement | undefined = $state();

  onMount(() => {
    void library.browse({});
    const io = new IntersectionObserver(
      (entries) => {
        if (entries.some((e) => e.isIntersecting)) void library.loadMore();
      },
      { rootMargin: '600px' },
    );
    if (sentinel) io.observe(sentinel);
    return () => io.disconnect();
  });

  function onSearch(value: string): void {
    q = value;
    void library.browse({ q });
  }

  function onSort(value: string): void {
    sort = value;
    void library.browse({ q, sort });
  }
</script>

<div class="shelf-header">
  <h1 id="shelf-title">The shelf</h1>
  <span class="muted eyebrow" style="margin-left:auto">{`${$shelf.total} album${$shelf.total === 1 ? '' : 's'}`}</span>
</div>

<SearchBox value={q} onSearch={onSearch} onSort={onSort} sort={sort} />

{#if $shelf.error}
  <ErrorView message={$shelf.error} />
{:else if $shelf.albums.length === 0 && $shelf.loading}
  <div class="spinner" role="status" aria-label="Loading the shelf"></div>
{:else if $shelf.albums.length === 0}
  <p class="empty-state">No albums in the collection yet — scan a library to begin.</p>
{:else}
  <div class="shelf-grid" bind:this={sentinel} role="list" aria-labelledby="shelf-title">
    {#each $shelf.albums as album}
      <div role="listitem">
        <AlbumCard {album} count={1} />
      </div>
    {/each}
  </div>
  {#if $shelf.loading}
    <div class="spinner" role="status" aria-label="Loading more albums"></div>
  {/if}
{/if}
