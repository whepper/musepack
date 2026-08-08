<script lang="ts">
  import { player, playerModel, queue } from '../bootstrap';
  import { fmtTime } from '../format';

  const currentTrackId = $derived($playerModel.current?.track.id);
  const currentIndex = $derived(queue.get().index);
</script>

{#if $queue.items.length === 0}
  <p class="empty-state" style="padding:var(--space-6) 0;font-size:var(--fs-md);font-family:var(--sans)">
    Nothing in the queue. Play an album from the shelf.
  </p>
{:else}
  <div style="display:flex;justify-content:space-between;align-items:center;padding:0 var(--space-4) var(--space-2)">
    <span class="smallcaps">{`${$queue.items.length} item${$queue.items.length === 1 ? '' : 's'}`}</span>
    <button class="smallcaps" style="color:var(--danger)" onclick={() => queue.clear()}>
      Clear queue
    </button>
  </div>
  <div style="overflow-y:auto;flex:1">
    {#each $queue.items as item, i (item.track.id)}
      <div class="queue-item" aria-current={i === currentIndex ? 'true' : undefined}>
        <div style="min-width:0">
          <button
            style="text-align:left;width:100%;display:block"
            aria-current={i === currentIndex ? 'true' : undefined}
            onclick={() => {
              if (i === currentIndex) return;
              void player.playItem(item);
            }}
          >
            <div class="queue-tt">
              <span class="num" style="color:var(--ink-faint);font-size:var(--fs-xs)">{i + 1}.</span>
              {' '}{item.track.title}
              {#if i === currentIndex}
                <span class="muted" style="font-size:var(--fs-xs)"> · {fmtTime($playerModel.positionSeconds)}</span>
              {/if}
            </div>
            <div class="queue-art">{item.artist} — {item.albumTitle}{item.edition ? ` · ${item.edition}` : ''}</div>
          </button>
        </div>
        <button
          aria-label={`Remove ${item.track.title} from the queue`}
          style="color:var(--ink-faint);font-size:var(--fs-md)"
          onclick={() => queue.removeAt(i)}>✕</button>
      </div>
    {/each}
  </div>
{/if}
