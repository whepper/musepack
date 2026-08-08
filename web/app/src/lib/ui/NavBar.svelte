<script lang="ts">
  import { session, router } from '../bootstrap';

  const route = $derived(router.route);
</script>

<header class="topnav">
  <a class="brand" href="/" aria-label="MusicPack home">Music<em>Pack</em></a>
  <nav class="navlinks" aria-label="Primary">
    <a
      class="navlink"
      href="/albums"
      aria-current={route.get().name === 'albums' || route.get().name === 'album' ? 'page' : undefined}>Albums</a>
    <a
      class="navlink"
      href="/artists"
      aria-current={route.get().name === 'artists' || route.get().name === 'artist' ? 'page' : undefined}>Artists</a>
    <a
      class="navlink"
      href="/queue"
      aria-current={route.get().name === 'queue' ? 'page' : undefined}>Now Playing</a>
  </nav>
  <div style="margin-left:auto;display:flex;align-items:center;gap:var(--space-3)">
    <button class="smallcaps" onclick={() => void session.logout()}>Sign out</button>
  </div>
</header>
