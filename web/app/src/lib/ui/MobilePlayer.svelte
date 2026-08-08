<script lang="ts">
  import { player, playerModel } from '../bootstrap';
  import { fmtTime } from '../format';

  const item = $derived($playerModel.current);
  let drag = $state<number | null>(null);
  const pos = $derived(drag ?? $playerModel.positionSeconds);
  const dur = $derived($playerModel.durationSeconds);
</script>

{#if item}
  <div class="mobile-player">
    <input
      type="range"
      min="0"
      max={dur > 0 ? dur : 0}
      step="0.5"
      value={pos}
      aria-label="Seek position"
      style="display:block;width:100%;height:6px;border-radius:0;border:none;background:var(--hairline)"
      oninput={(e) => (drag = Number((e.currentTarget as HTMLInputElement).value))}
      onchange={(e) => {
        drag = null;
        void player.seek(Number((e.currentTarget as HTMLInputElement).value));
      }}
    >
    <div class="row">
      <img class="thumb" src={item.artworkUrl ?? '/placeholder.svg'} alt="" aria-hidden="true"
        onerror={(e) => ((e.currentTarget as HTMLImageElement).style.visibility = 'hidden')}>
      <button style="min-width:0;text-align:left" onclick={() => window.dispatchEvent(new CustomEvent('musicpack:queue'))}>
        <div class="tt">{item.track.title}</div>
        <div class="art">{item.artist}</div>
      </button>
      <button aria-label="Previous track" onclick={() => void player.previous()}>
        <svg viewBox="0 0 24 24" width="22" height="22" fill="currentColor"><path d="M6 5h2v14H6zM20 5v14l-11-7z"/></svg>
      </button>
      <button aria-label={$playerModel.state === 'playing' ? 'Pause' : 'Play'} onclick={() => void player.togglePlay()}>
        {#if $playerModel.state === 'playing'}
          <svg viewBox="0 0 24 24" width="26" height="26" fill="currentColor"><path d="M7 5h4v14H7zM13 5h4v14h-4z"/></svg>
        {:else}
          <svg viewBox="0 0 24 24" width="26" height="26" fill="currentColor"><path d="M7 4l13 8-13 8z"/></svg>
        {/if}
      </button>
      <button aria-label="Next track" onclick={() => void player.next()}>
        <svg viewBox="0 0 24 24" width="22" height="22" fill="currentColor"><path d="M16 5h2v14h-2zM4 5l11 7-11 7z"/></svg>
      </button>
    </div>
    <div style="display:flex;justify-content:space-between;padding:0 var(--space-4) var(--space-2)">
      <span class="time smallcaps">{fmtTime(pos)}</span>
      <span class="time smallcaps">{fmtTime(dur)}</span>
    </div>
  </div>
{/if}
