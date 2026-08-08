<script lang="ts">
  import type { ReleaseDetail } from '../api/types';
  import { fmtTime, mediumLabel } from '../format';

  let {
    release,
    currentTrackId,
    onPlay,
  }: {
    release: ReleaseDetail;
    currentTrackId?: number;
    onPlay: (index: number) => void;
  } = $props();

  const discs = $derived([...release.media].sort((a, b) => a.disc - b.disc));
</script>

<div class="tracklist">
  {#each discs as disc, di (disc.disc)}
    {#if discs.length > 1}
      <h3 class="disc-title">
        Disc {disc.disc}
        {#if disc.title}<span class="muted">— {disc.title}</span>{/if}
      </h3>
    {/if}
    <div role="list" aria-label={`Disc ${disc.disc} tracks`}>
      {#each disc.tracks as track, ti (track.id)}
        <button
          class="track"
          aria-current={track.id === currentTrackId ? 'true' : undefined}
          onclick={() => onPlay(ti)}
        >
          <span class="num">{track.number}</span>
          <span class="tt">{track.title}</span>
          <span class="dur">{track.duration ? fmtTime(track.duration) : '—'}</span>
        </button>
      {/each}
    </div>
  {/each}
</div>
