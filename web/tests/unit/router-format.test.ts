import { describe, it, expect } from 'vitest';
import { parseRoute } from '../../app/src/lib/router';
import { fmtTime, yearOf, formatDate, countryName, mediumLabel, collectorLine } from '../../app/src/lib/format';

describe('router', () => {
  it('parses all route shapes', () => {
    expect(parseRoute('/').name).toBe('albums');
    expect(parseRoute('/albums').name).toBe('albums');
    expect(parseRoute('/albums/42')).toMatchObject({ name: 'album', params: { id: '42' } });
    expect(parseRoute('/albums/42?release=7').query.get('release')).toBe('7');
    expect(parseRoute('/artists').name).toBe('artists');
    expect(parseRoute('/artists/3')).toMatchObject({ name: 'artist', params: { id: '3' } });
    expect(parseRoute('/queue').name).toBe('queue');
  });

  it('rejects non-numeric ids and unknown routes', () => {
    expect(parseRoute('/albums/abc')?.name).toBe('notfound');
    expect(parseRoute('/admin').name).toBe('notfound');
  });
});

describe('format', () => {
  it('formats times', () => {
    expect(fmtTime(0)).toBe('0:00');
    expect(fmtTime(65)).toBe('1:05');
    expect(fmtTime(3675)).toBe('1:01:15');
    expect(fmtTime(-3)).toBe('0:00');
  });

  it('extracts years from partial dates', () => {
    expect(yearOf('1986-06-16')).toBe('1986');
    expect(yearOf('1986')).toBe('1986');
    expect(yearOf(undefined)).toBe('');
  });

  it('formats dates and countries', () => {
    expect(formatDate('1986-06-16')).toBe('16 Jun 1986');
    expect(countryName('XE')).toBe('Europe');
    expect(countryName('XX')).toBe('XX');
  });

  it('maps medium formats', () => {
    expect(mediumLabel('CD')).toBe('CD');
    expect(mediumLabel('SACD')).toBe('SACD');
    expect(mediumLabel('Digital')).toBe('Digital');
    expect(mediumLabel('Vinyl, LP')).toBe('Vinyl');
  });

  it('builds collector lines', () => {
    expect(collectorLine({ year: '1990', media: ['CD'], releaseCount: 3 })).toBe('1990 · CD · 3 versions');
    expect(collectorLine({ year: '1986' })).toBe('1986');
    expect(collectorLine({})).toBe('');
  });
});
