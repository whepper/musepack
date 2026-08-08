<script lang="ts">
  import type { AlbumSummary } from '../api/types';
  import Artwork from './Artwork.svelte';
  import { collectorLine, yearOf } from '../format';

  let { album, count }: { album: AlbumSummary; count: number } = $props();
  const artist = $derived(album.artists[0]?.name ?? '');
  const title = $derived(album.title);
</script>

<a class="album-card" href={`/albums/${album.id}`}>
  <div class="cover">
    <Artwork src={album.artwork?.url} alt={`${title} by ${artist}`} label={title} />
  </div>
  <div class="title">{title}</div>
  <div class="artist">{artist}</div>
  {#if count > 0}
    <div class="collector">
      {collectorLine({ year: yearOf(album.originalReleaseDate), releaseCount: album.releaseCount })}
    </div>
  {/if}
</a>
