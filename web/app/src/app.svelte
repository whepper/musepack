<script lang="ts">
  import { router, session } from './lib/bootstrap';
  import AuthGate from './lib/ui/AuthGate.svelte';
  import NavBar from './lib/ui/NavBar.svelte';
  import AlbumShelf from './lib/ui/AlbumShelf.svelte';
  import AlbumPage from './lib/ui/AlbumPage.svelte';
  import ArtistList from './lib/ui/ArtistList.svelte';
  import ArtistPage from './lib/ui/ArtistPage.svelte';
  import QueuePage from './lib/ui/QueuePage.svelte';
  import QueueDrawer from './lib/ui/QueueDrawer.svelte';
  import PlayerBar from './lib/ui/PlayerBar.svelte';
  import MobilePlayer from './lib/ui/MobilePlayer.svelte';
  import ErrorView from './lib/ui/ErrorView.svelte';
  import { onMount } from 'svelte';

  const route = $derived(router.route);
  let queueOpen = $state(false);
  let pageKey = $state(0);

  onMount(() => {
    const unsub = router.route.subscribe((r) => {
      pageKey++;
      window.scrollTo({ top: 0 });
      void r;
    });
    const onQueueEvent = () => (queueOpen = true);
    window.addEventListener('musicpack:queue', onQueueEvent);
    return () => {
      unsub();
      window.removeEventListener('musicpack:queue', onQueueEvent);
    };
  });

  function onSignOut(): void {
    void session.logout();
  }
</script>

<AuthGate>
  <div class="shell">
    <NavBar />
    <main id="main" class="main" tabindex="-1">
      {#key pageKey}
        {#if route.get().name === 'albums'}
          <AlbumShelf />
        {:else if route.get().name === 'album'}
          <AlbumPage
            albumId={route.get().params.id ?? ''}
            releaseParam={route.get().query.get('release') ?? undefined}
          />
        {:else if route.get().name === 'artists'}
          <ArtistList />
        {:else if route.get().name === 'artist'}
          <ArtistPage artistId={route.get().params.id ?? ''} />
        {:else if route.get().name === 'queue'}
          <QueuePage />
        {:else}
          <ErrorView message="Page not found" detail="This address does not match anything in the collection." />
        {/if}
      {/key}
    </main>
    <PlayerBar />
    <MobilePlayer />
    <QueueDrawer open={queueOpen} onClose={() => (queueOpen = false)} />
  </div>
</AuthGate>
