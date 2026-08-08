<script lang="ts">
  import type { ReleaseDetail } from '../api/types';
  import { countryName, formatDate, mediumLabel } from '../format';

  let { release }: { release: ReleaseDetail } = $props();

  const fields = $derived.by(() => {
    const r = release;
    const codec = r.media[0]?.tracks[0]?.codec;
    const track = r.media[0]?.tracks[0];
    const rows: Array<[string, string]> = [];
    const put = (label: string, value: string | number | undefined) => {
      if (value !== undefined && value !== '' && value !== 0) rows.push([label, String(value)]);
    };
    put('Edition', r.edition);
    put('Release date', formatDate(r.releaseDate));
    put('Original release', formatDate(r.album.originalReleaseDate));
    put('Country', countryName(r.country));
    put('Label', r.label);
    put('Catalogue number', r.catalogueNumber);
    put('Barcode', r.barcode);
    put('Medium', r.media.map((m) => `Disc ${m.disc}${m.format ? ` (${mediumLabel(m.format)})` : ''}`).join(', '));
    put('MusicBrainz release', r.mbid);
    put('MusicBrainz release group', r.album.mbid);
    put('Source', r.sourceType);
    put('Source store', r.sourceStore);
    put('Source id', r.sourceId);
    put('Identity source', r.identitySource);
    put('Identity confidence', r.identityConfidence);
    put('Provenance', r.provenanceTool);
    put('Provenance version', r.provenanceToolVersion);
    put('Codec', codec?.codec);
    put('Sample rate', codec?.sampleRate ? `${codec.sampleRate} Hz` : undefined);
    put('Channels', codec?.channels);
    put('BS.1770 loudness', r.loudness ? `${r.loudness.albumLufs.toFixed(2)} LUFS (true peak ${r.loudness.albumTruePeakDb.toFixed(2)} dBTP)` : undefined);
    put('Package status', r.packageStatus);
    put('Verification', r.verifyStatus);
    put('Notes', r.notes);
    void track;
    return rows;
  });
</script>

<details class="info-panel">
  <summary>Release information</summary>
  <dl class="info-grid">
    {#each fields as [label, value]}
      <div>
        <dt>{label}</dt>
        <dd>{value}</dd>
      </div>
    {/each}
  </dl>
</details>
