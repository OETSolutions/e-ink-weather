/**
 * Ambient declarations for assets Vite resolves to URLs.
 *
 * WHY THIS EXISTS: the brand marks are imported (`import url from './assets/x.png'`) so Vite
 * fingerprints them and copies them into both builds. TypeScript does not know that a `.png` import
 * yields a string without Vite's client types, and the project does not include `vite/client`
 * (it brings in a lot this app does not use). So the one rule it needs is declared here.
 */
declare module '*.png' {
  const url: string;
  export default url;
}
