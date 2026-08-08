<script lang="ts">
  import type { ReleaseSummary } from '../api/types';
  import { formatDate, mediumLabel, yearOf } from '../format';

  let {
    releases,
    selectedId,
    onSelect,
  }: {
    releases: ReleaseSummary[];
    selectedId: number;
    onSelect: (releaseId: number) => void;
  } = $props();
</script>

{#if releases.length > 1}
  <div class="editions" role="group" aria-label="Choose an edition">
    {#each releases as release (release.id)}
      <button
        class="edition-chip"
        aria-pressed={release.id === selectedId}
        title={release.edition ?? `Release ${release.id}`}
        onclick={() => onSelect(release.id)}
      >
        {mediumLabel(release.media[0])}
        {release.releaseDate ? `· ${yearOf(release.releaseDate)}` : ''}
        {release.edition ? `· ${release.edition}` : ''}
      </button>
    {/each}
  </div>
{:else}
  <p class="smallcaps">Single edition in the collection</p>
{/if}
