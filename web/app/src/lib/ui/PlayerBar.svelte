<script lang="ts">
  import { player, playerModel } from '../bootstrap';
  import { fmtTime } from '../format';
  import Artwork from './Artwork.svelte';

  const item = $derived($playerModel.current);
  let drag = $state<number | null>(null);

  const pos = $derived(drag ?? $playerModel.positionSeconds);
  const dur = $derived($playerModel.durationSeconds);

  const normLabel = $derived(
    $playerModel.normalizeMode === 'album' ? 'Album' : $playerModel.normalizeMode === 'track' ? 'Track' : 'Off',
  );
</script>

{#if item}
  <div class="playerbar" aria-label="Now playing">
    <div class="now">
      <img class="thumb" src={item.artworkUrl ?? '/placeholder.svg'} alt="" aria-hidden="true"
        onerror={(e) => ((e.currentTarget as HTMLImageElement).style.visibility = 'hidden')}>
      <div style="min-width:0">
        <div class="tt">{item.track.title}</div>
        <div class="art">{item.artist} — {item.albumTitle}{item.edition ? ` · ${item.edition}` : ''}</div>
      </div>
    </div>

    <div class="transport">
      <button aria-label="Previous track" onclick={() => void player.previous()}>
        <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M6 5h2v14H6zM20 5v14l-11-7z"/></svg>
      </button>
      <button aria-label={$playerModel.state === 'playing' ? 'Pause' : 'Play'} onclick={() => void player.togglePlay()}>
        {#if $playerModel.state === 'playing'}
          <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M7 5h4v14H7zM13 5h4v14h-4z"/></svg>
        {:else}
          <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M7 4l13 8-13 8z"/></svg>
        {/if}
      </button>
      <button aria-label="Next track" onclick={() => void player.next()}>
        <svg viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M16 5h2v14h-2zM4 5l11 7-11 7z"/></svg>
      </button>
      <div class="progress">
        <span class="time">{fmtTime(pos)}</span>
        <input
          type="range"
          min="0"
          max={dur > 0 ? dur : 0}
          step="0.5"
          value={pos}
          aria-label="Seek position"
          oninput={(e) => (drag = Number((e.currentTarget as HTMLInputElement).value))}
          onchange={(e) => {
            drag = null;
            void player.seek(Number((e.currentTarget as HTMLInputElement).value));
          }}
          disabled={!item}
        >
        <span class="time">{fmtTime(dur)}</span>
      </div>
    </div>

    <div class="right-zone">
      <button
        class="smallcaps"
        aria-label={`Normalization: ${normLabel}. Click to change.`}
        title={`Loudness normalization: ${normLabel}`}
        onclick={() =>
          player.setNormalizeMode(
            $playerModel.normalizeMode === 'album' ? 'track' : $playerModel.normalizeMode === 'track' ? 'off' : 'album',
          )
        }
      >
        {normLabel}{$playerModel.normDb ? ` ${$playerModel.normDb > 0 ? '+' : ''}${$playerModel.normDb.toFixed(1)} dB` : ''}
      </button>
      <label class="muted" style="display:flex;align-items:center;gap:var(--space-2)" aria-label="Volume">
        <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor" aria-hidden="true"><path d="M3 9v6h4l5 5V4L7 9H3zm13.6 3a4 4 0 0 0-2-3.5v7a4 4 0 0 0 2-3.5zM15 4v2.2a6 6 0 0 1 0 11.6V20a8 8 0 0 0 0-16z"/></svg>
        <input
          type="range" min="0" max="1" step="0.01"
          value={$playerModel.volume}
          aria-label="Volume"
          oninput={(e) => player.setVolume(Number((e.currentTarget as HTMLInputElement).value))}
          style="width:90px"
        >
      </label>
      <button class="smallcaps" aria-label="Open the queue" onclick={() => window.dispatchEvent(new CustomEvent('musicpack:queue'))}>
        Queue
      </button>
    </div>
  </div>
{/if}
