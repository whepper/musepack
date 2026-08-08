import { mount } from 'svelte';
import App from './app.svelte';
import './lib/ui/theme.css';
import { exposeDebug, player, session } from './lib/bootstrap';

void session.boot();
player.init();
exposeDebug();

const target = document.getElementById('app');
if (!target) throw new Error('missing #app mount point');

mount(App, { target });
