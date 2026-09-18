// Local exploration of browser-owned relationships. No inferred semantic links.
import {html, nothing} from '//resources/lit/v3_0/lit.rollup.js';

export interface ContextNode {
  id: string; kind: string; title: string; subtitle: string; state: string;
}
export interface ContextGraph {
  revision: string; root_id: string; nodes: ContextNode[];
  edges: Array<{source: string, target: string, type: string}>;
  truncated: boolean; total_nodes: number;
}
export interface ContextGraphView {
  graph?: ContextGraph;
  selected: string; query: string; zoom: number; busy: boolean; error: string;
  kind?: string; local?: boolean; depth?: number;
}
interface GraphActions {
  change: (patch: Partial<ContextGraphView>) => void;
  refresh: () => void;
  open: (node: ContextNode) => void;
}
interface Point {x: number; y: number; vx: number; vy: number;}
const NS = 'http://www.w3.org/2000/svg';
const KIND_NAMES: Record<string, string> = {
  profile: 'Profile', window: 'Windows', space: 'Spaces', tab: 'Tabs',
  thread: 'Projects', task: 'Tasks', board: 'Boards',
};
const titleFor = (node: ContextNode) => node.title || 'Untitled';
const readable = (value: string) => value.replaceAll('_', ' ');

// Deterministic, bounded force settling. A spatial grid bounds nearby repulsion;
// selection, zoom and filtering reuse these positions and the existing SVG.
function layout(graph: ContextGraph): Map<string, Point> {
  const points = new Map<string, Point>();
  [...graph.nodes].sort((a, b) => a.id.localeCompare(b.id)).forEach((node, i) => {
    const radius = 32 * Math.sqrt(i + 1), angle = i * 2.3999632297;
    points.set(node.id, {x: Math.cos(angle) * radius, y: Math.sin(angle) * radius, vx: 0, vy: 0});
  });
  for (let iteration = 0; iteration < 100; iteration++) {
    const grid = new Map<string, Point[]>();
    for (const p of points.values()) {
      const key = `${Math.floor(p.x / 100)},${Math.floor(p.y / 100)}`;
      const cell = grid.get(key) ?? []; cell.push(p); grid.set(key, cell);
    }
    for (const p of points.values()) {
      const gx = Math.floor(p.x / 100), gy = Math.floor(p.y / 100);
      for (let x = gx - 1; x <= gx + 1; x++) for (let y = gy - 1; y <= gy + 1; y++) {
        for (const q of grid.get(`${x},${y}`) ?? []) {
          if (p === q) continue;
          const dx = p.x - q.x, dy = p.y - q.y;
          const d2 = Math.max(16, dx * dx + dy * dy);
          if (d2 > 16000) continue;
          const force = 1100 / d2;
          p.vx += dx * force; p.vy += dy * force;
        }
      }
      p.vx -= p.x * .005; p.vy -= p.y * .005;
    }
    for (const edge of graph.edges) {
      const a = points.get(edge.source), b = points.get(edge.target);
      if (!a || !b || a === b) continue;
      const dx = b.x - a.x, dy = b.y - a.y;
      const distance = Math.max(1, Math.hypot(dx, dy));
      const force = (distance - 96) * .025 / distance;
      a.vx += dx * force; a.vy += dy * force;
      b.vx -= dx * force; b.vy -= dy * force;
    }
    for (const p of points.values()) {
      p.vx *= .55; p.vy *= .55;
      p.x += Math.max(-12, Math.min(12, p.vx));
      p.y += Math.max(-12, Math.min(12, p.vy));
    }
  }
  return points;
}
function svg<K extends keyof SVGElementTagNameMap>(tag: K, attrs: Record<string, string> = {}) {
  const node = document.createElementNS(NS, tag);
  for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, value);
  return node;
}
class GraphScene {
  readonly element = svg('svg', {class: 'context-graph-drawing', role: 'group', tabindex: '0',
    'aria-label': 'Context connections. Drag or arrow keys to pan. Scroll to zoom. Select an item to inspect; double-click to open.'});
  private points: Map<string, Point>;
  private nodes = new Map<string, SVGGElement>();
  private edges: Array<{element: SVGLineElement, source: string, target: string}> = [];
  private neighbors = new Map<string, Set<string>>();
  private visible = new Set<string>();
  private extent = 600;
  private x = 0; private y = 0;
  private drag?: {id: number, x: number, y: number};
  private hovered = '';
  private state!: ContextGraphView;
  private actions!: GraphActions;
  constructor(readonly graph: ContextGraph) {
    this.points = layout(graph);
    for (const node of graph.nodes) this.neighbors.set(node.id, new Set());
    for (const edge of graph.edges) {
      const a = this.points.get(edge.source), b = this.points.get(edge.target);
      if (!a || !b) continue;
      this.neighbors.get(edge.source)!.add(edge.target);
      this.neighbors.get(edge.target)!.add(edge.source);
      const line = svg('line', {class: 'context-graph-edge', x1: `${a.x}`, y1: `${a.y}`, x2: `${b.x}`, y2: `${b.y}`});
      this.edges.push({element: line, ...edge}); this.element.append(line);
    }
    for (const node of graph.nodes) {
      const p = this.points.get(node.id)!;
      const degree = this.neighbors.get(node.id)!.size;
      const group = svg('g', {class: 'context-graph-node', transform: `translate(${p.x},${p.y})`,
        role: 'button', tabindex: '-1', 'aria-label': `${titleFor(node)}, ${readable(node.kind)}, ${degree} connections`});
      group.dataset['id'] = node.id;
      group.dataset['kind'] = KIND_NAMES[node.kind] ? node.kind : 'other';
      const hit = svg('circle', {r: '20', class: 'context-graph-hit'});
      const dot = svg('circle', {r: `${Math.min(10, 5 + Math.sqrt(degree))}`, class: 'context-graph-dot'});
      const label = svg('text', {y: '25', 'text-anchor': 'middle'});
      label.textContent = titleFor(node).length > 28 ? `${titleFor(node).slice(0, 27)}…` : titleFor(node);
      const title = svg('title'); title.textContent = `${titleFor(node)}\n${node.subtitle}`;
      group.append(hit, dot, label, title);
      group.addEventListener('click', () => this.actions.change({selected: node.id}));
      group.addEventListener('dblclick', () => this.actions.open(node));
      group.addEventListener('pointerenter', () => { this.hovered = node.id; this.highlight(); });
      group.addEventListener('pointerleave', () => { this.hovered = ''; this.highlight(); });
      group.addEventListener('focus', () => { this.hovered = node.id; this.highlight(); });
      group.addEventListener('blur', () => { this.hovered = ''; this.highlight(); });
      group.addEventListener('keydown', event => {
        if (event.key === 'Enter' || event.key === ' ') {
          event.preventDefault(); event.stopPropagation(); this.actions.change({selected: node.id});
        }
      });
      this.nodes.set(node.id, group); this.element.append(group);
    }
    this.element.addEventListener('pointerdown', event => {
      if (event.button !== 0 || (event.target as Element).closest('.context-graph-node')) return;
      this.drag = {id: event.pointerId, x: event.clientX, y: event.clientY};
      this.element.setPointerCapture(event.pointerId);
    });
    this.element.addEventListener('pointermove', event => {
      if (this.drag?.id !== event.pointerId) return;
      const bounds = this.element.getBoundingClientRect();
      const scale = Math.min(bounds.width, bounds.height) / (this.extent / this.state.zoom);
      if (!scale) return;
      this.x -= (event.clientX - this.drag.x) / scale;
      this.y -= (event.clientY - this.drag.y) / scale;
      this.drag.x = event.clientX; this.drag.y = event.clientY; this.camera();
    });
    this.element.addEventListener('pointerup', () => { this.drag = undefined; });
    this.element.addEventListener('pointercancel', () => { this.drag = undefined; });
    this.element.addEventListener('wheel', event => {
      event.preventDefault();
      this.actions.change({zoom: Math.max(.35, Math.min(5, this.state.zoom * Math.exp(-event.deltaY * .002)))});
    }, {passive: false});
    this.element.addEventListener('keydown', event => {
      if (event.target !== this.element) return;
      const step = 40 / this.state.zoom;
      if (event.key === 'ArrowLeft') this.x -= step;
      else if (event.key === 'ArrowRight') this.x += step;
      else if (event.key === 'ArrowUp') this.y -= step;
      else if (event.key === 'ArrowDown') this.y += step;
      else if (event.key === '+' || event.key === '=') this.actions.change({zoom: Math.min(5, this.state.zoom * 1.2)});
      else if (event.key === '-') this.actions.change({zoom: Math.max(.35, this.state.zoom / 1.2)});
      else if (event.key === 'Home') this.fit();
      else return;
      event.preventDefault(); this.camera();
    });
  }
  update(state: ContextGraphView, actions: GraphActions) {
    const first = !this.state;
    this.state = state; this.actions = actions;
    const query = state.query.trim().toLocaleLowerCase();
    let local: Set<string>|undefined;
    if (state.local && state.selected) {
      local = new Set([state.selected]);
      for (let depth = 0; depth < (state.depth ?? 1); depth++) {
        const next = [...local].flatMap(id => [...this.neighbors.get(id) ?? []]);
        next.forEach(id => local!.add(id));
      }
    }
    const matched = new Set(this.graph.nodes.filter(node =>
      (!state.kind || node.kind === state.kind) && (!local || local.has(node.id)) &&
      (!query || `${node.title} ${node.subtitle}`.toLocaleLowerCase().includes(query))).map(node => node.id));
    this.visible = new Set(matched);
    // Keep immediate relationship anchors as quiet context. Filtering to Tabs
    // should still explain which Space they belong to, rather than erase edges.
    if (query || state.kind) for (const id of matched) {
      for (const related of this.neighbors.get(id) ?? []) {
        if (!local || local.has(related)) this.visible.add(related);
      }
    }
    for (const node of this.graph.nodes) {
      const match = this.visible.has(node.id);
      const element = this.nodes.get(node.id)!;
      element.style.display = match ? '' : 'none';
      element.classList.toggle('context', !matched.has(node.id));
      element.classList.toggle('selected', node.id === state.selected);
      element.setAttribute('aria-pressed', `${node.id === state.selected}`);
      element.setAttribute('tabindex', match && (node.id === state.selected || (!state.selected && node === this.graph.nodes[0])) ? '0' : '-1');
    }
    for (const edge of this.edges) edge.element.style.display = this.visible.has(edge.source) && this.visible.has(edge.target) ? '' : 'none';
    this.element.classList.toggle('sparse', this.visible.size <= 24);
    this.element.classList.toggle('zoomed', state.zoom >= 2);
    if (first) this.fit(false);
    this.highlight(); this.camera();
  }
  fit(notify = true) {
    const points = [...this.points].filter(([id]) => this.visible.has(id)).map(([, point]) => point);
    if (!points.length) return;
    const xs = points.map(p => p.x), ys = points.map(p => p.y);
    this.x = (Math.min(...xs) + Math.max(...xs)) / 2;
    this.y = (Math.min(...ys) + Math.max(...ys)) / 2;
    this.extent = Math.max(260, Math.max(...xs) - Math.min(...xs) + 130, Math.max(...ys) - Math.min(...ys) + 130);
    if (notify) this.actions.change({zoom: 1});
    this.camera();
  }
  private camera() {
    const size = this.extent / this.state.zoom;
    this.element.setAttribute('viewBox', `${this.x - size / 2} ${this.y - size / 2} ${size} ${size}`);
  }
  private highlight() {
    const focus = this.hovered || this.state.selected;
    const connected = this.neighbors.get(focus);
    const labels = new Set([focus, ...[...(connected ?? [])].filter(id => this.visible.has(id)).slice(0, 10)]);
    for (const [id, element] of this.nodes) {
      element.classList.toggle('labelled', labels.has(id));
      element.classList.toggle('dimmed', !!focus && id !== focus && !connected?.has(id));
      element.classList.toggle('neighbor', id === focus || !!connected?.has(id));
    }
    for (const edge of this.edges) {
      const active = edge.source === focus || edge.target === focus;
      edge.element.classList.toggle('highlighted', active);
      edge.element.classList.toggle('dimmed', !!focus && !active);
    }
  }
}
const scenes = new WeakMap<ContextGraph, GraphScene>();

export function renderContextGraph(state: ContextGraphView, actions: GraphActions) {
  const graph = state.graph;
  let scene = graph ? scenes.get(graph) : undefined;
  if (graph && !scene) { scene = new GraphScene(graph); scenes.set(graph, scene); }
  scene?.update(state, actions);
  const selected = graph?.nodes.find(node => node.id === state.selected);
  const query = state.query.trim().toLocaleLowerCase();
  const matches = graph?.nodes.filter(node => (!state.kind || node.kind === state.kind) &&
    `${node.title} ${node.subtitle}`.toLocaleLowerCase().includes(query)) ?? [];
  const links = graph?.edges.filter(edge => edge.source === selected?.id || edge.target === selected?.id) ?? [];
  const kinds = [...new Set(graph?.nodes.map(node => node.kind) ?? [])];
  const expand = (event: Event) => {
    const section = (event.currentTarget as Element).closest('.context-map') as HTMLElement;
    if (document.fullscreenElement) void document.exitFullscreen();
    else void section.requestFullscreen().catch(() => actions.change({error: 'The graph could not expand. You can still pan and zoom here.'}));
  };
  return html`<section class="context-map" aria-label="Context graph">
    <div class="context-map-intro"><div><h2>Your workspace</h2><p>Your tabs and projects, connected.</p></div>
      <div class="context-map-tools"><button type="button" aria-label="Expand or restore context graph" @click="${expand}"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><path d="M8 3H3v5m13-5h5v5M3 16v5h5m13-5v5h-5"/></svg></button>
        <button type="button" ?disabled="${state.busy}" @click="${actions.refresh}">${state.busy ? 'Refreshing…' : 'Refresh'}</button></div></div>
    ${state.error ? html`<p class="status-error" role="alert">${state.error}</p>` : nothing}
    ${graph ? html`
      <label class="context-map-search"><span class="sr-only">Search context</span>
        <input type="search" aria-label="Search context" placeholder="Find a tab, Space or project…"
          .value="${state.query}" @input="${(event: Event) => actions.change({query: (event.target as HTMLInputElement).value})}"></label>
      <div class="context-map-filters" aria-label="Context types">
        <button type="button" aria-pressed="${!state.kind}" @click="${() => actions.change({kind: ''})}">All</button>
        ${kinds.map(kind => html`<button type="button" aria-pressed="${state.kind === kind}"
          @click="${() => actions.change({kind: state.kind === kind ? '' : kind})}"><i data-kind="${kind}"></i>${KIND_NAMES[kind] ?? readable(kind)}</button>`)}
      </div>
      <div class="context-map-workspace">
        <div class="context-map-stage">${scene!.element}
          <div class="context-map-controls"><span class="context-map-zoom">
            <button type="button" aria-label="Zoom out" ?disabled="${state.zoom <= .35}"
              @click="${() => actions.change({zoom: Math.max(.35, state.zoom / 1.25)})}">−</button>
            <button type="button" aria-label="Fit visible context" @click="${() => scene!.fit()}">Fit</button>
            <button type="button" aria-label="Zoom in" ?disabled="${state.zoom >= 5}"
              @click="${() => actions.change({zoom: Math.min(5, state.zoom * 1.25)})}">+</button></span>
            <span>${graph.nodes.length} items · ${graph.edges.length} links</span>
          </div>
        </div>
        <aside class="context-map-inspector" aria-label="Context details">
          ${selected ? html`<section class="context-map-detail" aria-label="Selected context">
            <span class="eyebrow">${readable(selected.kind)}${selected.state ? ` · ${selected.state}` : ''}</span>
            <h3 tabindex="-1">${titleFor(selected)}</h3><p>${selected.subtitle}</p>
            <div class="context-map-detail-actions">
              ${['tab', 'thread', 'board'].includes(selected.kind) ? html`<button type="button" class="primary"
                @click="${() => actions.open(selected)}">Open ${selected.kind === 'thread' ? 'project' : selected.kind} ↗</button>` : nothing}
              <button type="button" aria-pressed="${!!state.local}"
                @click="${() => { const next = {...state, local: !state.local}; actions.change({local: next.local}); scene!.update(next, actions); scene!.fit(); }}">${state.local ? 'Show all context' : 'Focus connections'}</button>
              <button type="button" aria-label="Clear selection" @click="${() => actions.change({selected: '', local: false})}">×</button>
            </div>
            ${state.local ? html`<label class="context-map-depth">Connection depth <select aria-label="Connection depth"
              .value="${`${state.depth ?? 1}`}" @change="${(event: Event) => actions.change({depth: Number((event.target as HTMLSelectElement).value)})}">
              <option value="1">1 step</option><option value="2">2 steps</option><option value="3">3 steps</option></select></label>` : nothing}
            <h4>${links.length} connections</h4>
            <ul class="context-map-links">${links.slice(0, 80).map(edge => {
              const other = graph.nodes.find(node => node.id === (edge.source === selected.id ? edge.target : edge.source));
              return other ? html`<li><button type="button" @click="${() => actions.change({selected: other.id})}">
                <span>${titleFor(other)}</span><small>${readable(edge.type)}</small></button></li>` : nothing;
            })}</ul>
            ${links.length > 80 ? html`<p>Showing 80 connections. Search to find more.</p>` : nothing}
          </section>` : html`<div class="context-map-hint"><strong>Follow a connection</strong><p>Select an item to see what it belongs to and open the work behind it. Drag to explore; scroll to zoom.</p></div>`}
          ${query ? html`<section class="context-map-matches"><h4>${matches.length} search results</h4>
            <ul class="context-map-results">${matches.slice(0, 30).map(node => html`<li><button type="button" aria-pressed="${node.id === state.selected}"
              @click="${() => actions.change({selected: node.id})}"><span>${titleFor(node)}</span><small>${readable(node.kind)}</small></button></li>`)}</ul>
            ${matches.length > 30 ? html`<p>Showing 30 results. Refine your search to narrow them.</p>` : nothing}
          </section>` : nothing}
        </aside>
      </div>
      ${graph.truncated ? html`<p class="context-map-caption">Showing ${graph.nodes.length} of ${graph.total_nodes} items in this snapshot.</p>` : nothing}
    ` : html`<p>${state.busy ? 'Loading your context…' : 'Refresh to load your context.'}</p>`}
  </section>`;
}
