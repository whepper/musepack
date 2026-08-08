<script lang="ts">
  // Album artwork is content, not decoration: meaningful alt text, graceful
  // placeholder with a deterministically derived monogram tile when the
  // artwork is missing or fails to load.
  let { src, alt, label }: { src?: string; alt: string; label?: string } = $props();
  let failed = $state(false);

  const PALETTE = [
    '#6b4f3a', '#7a5c3e', '#55705a', '#5a6b72', '#71547a', '#7a5448',
    '#4f6d6d', '#6d5a4f', '#7a4f2a', '#5c5f7a', '#687a55', '#7a4f5c',
  ];

  const hue = $derived.by(() => {
    const s = label ?? alt;
    let h = 0;
    for (let i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0;
    return PALETTE[h % PALETTE.length] ?? '#7a4f2a';
  });

  const initials = $derived.by(() => {
    const s = label ?? alt;
    const words = s.split(/\s+/).filter(Boolean).slice(0, 2);
    return words.map((w) => w[0]?.toUpperCase() ?? '').join('');
  });
</script>

{#if src && !failed}
  <img {src} {alt} loading="lazy" decoding="async" onerror={() => (failed = true)}>
{:else}
  <div class="artwork-fallback" style="background:{hue}" role="img" aria-label={alt}>
    <span style="font-size:1.6em;letter-spacing:.05em">{initials}</span>
  </div>
{/if}
