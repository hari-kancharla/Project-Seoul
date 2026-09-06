// Resolved only by tsconfig.local.json. The Chromium build supplies the real
// //resources/lit/v3_0/lit.rollup.js declarations.
export const nothing: unknown;
export function html(strings: TemplateStringsArray, ...values: unknown[]): unknown;
// No `svg` tag is declared here on purpose. Chromium's bundle
// (third_party/lit/v3_0/lit.ts) exports css, CSSResultGroup, html, LitElement,
// nothing, render, PropertyValues, TemplateResult, directive, PartInfo,
// PartType, AsyncDirective and CrLitElement - and no svg. Declaring one here
// let the local typecheck accept an import that the real build then rejected,
// which is the worst shape a stub can take: a gate that passes and a build
// that fails. Build SVG inside an html`` template instead.

export class CrLitElement extends HTMLElement {
  readonly updateComplete: Promise<unknown>;
  static get properties(): Record<string, unknown>;
  static get styles(): unknown;
  render(): unknown;
  connectedCallback(): void;
  disconnectedCallback(): void;
}
