import { mount } from 'svelte';
import Dashboard from './dashboard/Dashboard.svelte';
import Settings from './settings/Settings.svelte';
import './shared/theme.css';

const page = document.body.dataset.page;
const target = document.getElementById('app');

mount(page === 'settings' ? Settings : Dashboard, { target });
