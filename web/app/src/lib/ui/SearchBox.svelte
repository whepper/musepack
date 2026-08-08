<script lang="ts">
  let {
    value,
    onSearch,
    onSort,
    sort,
  }: {
    value: string;
    sort: string;
    onSearch: (q: string) => void;
    onSort: (sort: string) => void;
  } = $props();
</script>

<div class="search-row" style="display:flex;gap:var(--space-4);align-items:center;margin-bottom:var(--space-5);flex-wrap:wrap">
  <label class="searchbox" style="display:flex;align-items:center;gap:var(--space-2);border:1px solid var(--hairline);border-radius:999px;padding:6px 16px;background:var(--paper-raised);flex:1;min-width:220px">
    <span aria-hidden="true" style="color:var(--ink-faint)">⌕</span>
    <input
      style="border:none;background:transparent;font:inherit;outline:none;width:100%"
      type="search"
      placeholder="Search title or artist"
      value={value}
      aria-label="Search the collection"
      oninput={(e) => onSearch((e.currentTarget as HTMLInputElement).value)}
    >
  </label>
  <div class="sort" role="group" aria-label="Sort the shelf">
    <button
      class="edition-chip"
      aria-pressed={sort === ''}
      onclick={() => onSort('')}>All</button>
    <button
      class="edition-chip"
      aria-pressed={sort === 'recent'}
      onclick={() => onSort('recent')}>Recently added</button>
  </div>
</div>
