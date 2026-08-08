<script lang="ts">
  import QueueList from './QueueList.svelte';

  let { open, onClose }: { open: boolean; onClose: () => void } = $props();
</script>

<div
  class="queue-panel"
  class:open
  role="dialog"
  aria-label="Playback queue"
  aria-hidden={!open}
>
  <header>
    <h2 class="smallcaps" style="margin:0">Queue</h2>
    <button aria-label="Close the queue" onclick={onClose}>✕</button>
  </header>
  <QueueList />
</div>
