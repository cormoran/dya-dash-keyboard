// @ts-check
import { defineConfig } from "astro/config";
import starlight from "@astrojs/starlight";
import starlightThemeNova from "starlight-theme-nova";

// https://astro.build/config
export default defineConfig({
  site: "https://cormoran.github.io",
  base: "dya-dash-keyboard",
  integrations: [
    starlight({
      plugins: [starlightThemeNova()],
      title: "DYA Dash",
      logo: {
        src: "./src/assets/dya.svg",
      },
      head: [
        {
          tag: "script",
          attrs: {
            src: "https://www.googletagmanager.com/gtag/js?id=G-4X7C3VY7W6",
            async: true,
          },
        },
        {
          tag: "script",
          content: `
  window.dataLayer = window.dataLayer || [];
  function gtag(){dataLayer.push(arguments);}
  gtag('js', new Date());

  gtag('config', 'G-4X7C3VY7W6');
  `,
        },
      ],
      social: [
        {
          icon: "github",
          label: "GitHub",
          href: "https://github.com/cormoran/dya-dash-keyboard",
        },
      ],
      sidebar: [
        {
          label: "ビルドガイド V2 (2025/06)",
          autogenerate: { directory: "build-guide/v2" },
        },
        {
          label: "ビルドガイド V1 (2025/05)",
          autogenerate: { directory: "build-guide/v1" },
        },
        {
          label: "使い方ガイド",
          autogenerate: { directory: "feature-guide" },
        },
      ],
    }),
  ],
});
