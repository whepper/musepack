<script lang="ts">
  import { player, playerModel } from '../bootstrap';
  import { fmtTime } from '../format';
  import QueueList from './QueueList.svelte';

  const item = $derived($playerModel.current);
</script>

<div class="shelf-header">
  <h1>Now playing</h1>
</div>

{#if item}
  <div style="display:flex;gap:var(--space-5);align-items:center;margin-bottom:var(--space-6);flex-wrap:wrap">
    <img
      src={item.artworkUrl ?? '/placeholder.svg'}
      alt=""
      width="180" height="180"
      style="border-radius:4px;object-fit:cover;border:1px solid var(--hairline)"
      onerror={(e) => ((e.currentTarget as HTMLImageElement).style.visibility = 'hidden')}
    >
    <div>
      <p class="eyebrow">{$playerModel.state}</p>
      <h2 style="font-size:var(--fs-xl)">{item.track.title}</h2>
      <p class="muted">{item.artist} — {item.albumTitle}{item.edition ? ` · ${item.edition}` : ''}</p>
      <p class="muted">{fmtTime($playerModel.positionSeconds)} / {fmtTime($playerModel.durationSeconds)}</p>
      <div style="display:flex;gap:var(--space-3);margin-top:var(--space-3)">
        <button class="btn ghost" onclick={() => void player.previous()}>Previous</button>
        <button class="btn" onclick={() => void player.togglePlay()}>
          {$playerModel.state === 'playing' ? 'Pause' : 'Play'}
        </button>
        <button class="btn ghost" onclick={() => void player.next()}>Next</button>
      </div>
    </div>
  </div>
{:else}
  <p class="empty-state">Nothing playing yet — choose an album from the shelf.</p>
{/if}

<div class="shelf-header" style="margin-top:var(--space-5)">
  <h2 style="font-size:var(--fs-lg)">Queue</h2>
</div>
<QueueList />
