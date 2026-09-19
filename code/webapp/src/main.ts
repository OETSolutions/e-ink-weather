import { emptyConfig } from './model/config';

const app = document.getElementById('app');
if (app) {
  app.textContent = 'E-Ink Weather layout config (scaffold)';
  console.log(emptyConfig());
}
