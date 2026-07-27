/**
 * Turns the live page into the candidate shape elementResolver expects.
 *
 * Runs INSIDE THE PAGE and stays there. The harvest is never sent across the
 * bridge — a query goes out, rects come back. This file exists so the resolver
 * has something to think with, not so the native side can inspect the DOM.
 */

const SKIP_TAGS = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'TEMPLATE', 'HEAD', 'META', 'LINK']);

const INPUT_ROLE = {
  text: 'textbox', email: 'textbox', tel: 'textbox', url: 'textbox',
  password: 'textbox', date: 'textbox', month: 'textbox', week: 'textbox',
  time: 'textbox', 'datetime-local': 'textbox',
  search: 'searchbox',
  number: 'spinbutton',
  range: 'slider',
  checkbox: 'checkbox',
  radio: 'radio',
  submit: 'button', button: 'button', reset: 'button', image: 'button',
};

function roleOf(el) {
  const explicit = el.getAttribute && el.getAttribute('role');
  if (explicit) return explicit.trim().toLowerCase();

  const tag = el.tagName;
  switch (tag) {
    case 'INPUT': {
      const type = (el.getAttribute('type') || 'text').toLowerCase();
      return INPUT_ROLE[type] || 'textbox';
    }
    case 'TEXTAREA': return 'textbox';
    case 'SELECT': return el.multiple || el.size > 1 ? 'listbox' : 'combobox';
    case 'BUTTON': return 'button';
    case 'A': return el.hasAttribute('href') ? 'link' : '';
    case 'SUMMARY': return 'button';
    case 'IMG': return 'img';
    case 'H1': case 'H2': case 'H3': case 'H4': case 'H5': case 'H6': return 'heading';
    default:
      if (el.isContentEditable) return 'textbox';
      return '';
  }
}

function textOf(node) {
  return (node && node.textContent ? node.textContent : '').replace(/\s+/g, ' ').trim();
}

/**
 * Accessible name, in roughly the order the accname spec resolves it. Not a
 * complete implementation — enough to name the things a person actually asks
 * for by name.
 */
function accessibleName(el, doc) {
  const labelledBy = el.getAttribute && el.getAttribute('aria-labelledby');
  if (labelledBy) {
    const parts = labelledBy.split(/\s+/)
      .map((id) => doc.getElementById(id))
      .filter(Boolean)
      .map(textOf)
      .filter(Boolean);
    if (parts.length) return parts.join(' ');
  }

  const ariaLabel = el.getAttribute && el.getAttribute('aria-label');
  if (ariaLabel && ariaLabel.trim()) return ariaLabel.trim();

  if (el.id) {
    const explicit = doc.querySelector(`label[for="${CSS.escape(el.id)}"]`);
    if (explicit) {
      const t = textOf(explicit);
      if (t) return t;
    }
  }
  const wrapping = el.closest && el.closest('label');
  if (wrapping) {
    const t = textOf(wrapping);
    if (t) return t;
  }

  const placeholder = el.getAttribute && el.getAttribute('placeholder');
  if (placeholder && placeholder.trim()) return placeholder.trim();

  const tag = el.tagName;
  if (tag === 'INPUT') {
    const type = (el.getAttribute('type') || 'text').toLowerCase();
    // A submit button's visible text IS its value attribute.
    if (type === 'submit' || type === 'button' || type === 'reset') {
      const value = el.getAttribute('value');
      if (value && value.trim()) return value.trim();
    }
    if (type === 'image') {
      const alt = el.getAttribute('alt');
      if (alt && alt.trim()) return alt.trim();
    }
  }
  if (tag === 'IMG') {
    const alt = el.getAttribute('alt');
    if (alt && alt.trim()) return alt.trim();
  }

  const title = el.getAttribute && el.getAttribute('title');
  if (title && title.trim()) return title.trim();

  // Content-bearing roles are named by what they say.
  if (['BUTTON', 'A', 'SUMMARY', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6'].includes(tag)) {
    const t = textOf(el);
    if (t && t.length <= 120) return t;
  }

  const name = el.getAttribute && el.getAttribute('name');
  if (name && name.trim()) return name.trim().replace(/[_-]+/g, ' ');

  return '';
}

/**
 * Ancestor context, outermost first.
 *
 * This is what separates "Shipping address / Address line 1" from
 * "Billing address / Address line 1", which is the whole reason the resolver
 * takes groupPath as a first-class input.
 */
function groupPathOf(el) {
  const path = [];
  let node = el.parentElement;
  while (node && node !== document.body) {
    let label = '';
    if (node.tagName === 'FIELDSET') {
      const legend = node.querySelector('legend');
      label = textOf(legend);
    }
    if (!label) {
      const aria = node.getAttribute('aria-label');
      if (aria && aria.trim()) label = aria.trim();
    }
    if (!label) {
      const labelledBy = node.getAttribute('aria-labelledby');
      if (labelledBy) {
        const ref = document.getElementById(labelledBy.split(/\s+/)[0]);
        label = textOf(ref);
      }
    }
    if (!label && ['SECTION', 'FORM', 'ARTICLE', 'ASIDE', 'NAV', 'MAIN'].includes(node.tagName)) {
      const heading = node.querySelector('h1, h2, h3, h4, h5, h6, legend');
      // Only a heading that actually belongs to this section, not one scooped
      // out of a nested subsection.
      if (heading && heading.closest('section, form, article, aside, nav, main') === node) {
        label = textOf(heading);
      }
    }
    if (label && label.length <= 80) path.unshift(label);
    node = node.parentElement;
  }
  return path;
}

/** Deterministic id, so the same element keeps its identity across queries. */
function identityOf(role, name, groupPath) {
  const key = `${role}|${name}|${groupPath.join('/')}`.toLowerCase();
  let hash = 0x811c9dc5;
  for (let i = 0; i < key.length; i += 1) {
    hash ^= key.charCodeAt(i);
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return `el-${hash.toString(16)}`;
}

function isVisible(el, style, rect) {
  if (!rect || rect.width <= 0 || rect.height <= 0) return false;
  if (style.visibility === 'hidden' || style.display === 'none') return false;
  if (Number(style.opacity) === 0) return false;
  if (el.getAttribute && el.getAttribute('aria-hidden') === 'true') return false;
  return true;
}

/**
 * @returns {Array<{id,role,name,groupPath,rect,visible,inViewport,disabled,el}>}
 *   `el` is the live node and is stripped before anything leaves the page.
 */
export function harvest(doc = document) {
  const out = [];
  const seen = new Set();
  const viewportWidth = window.innerWidth;
  const viewportHeight = window.innerHeight;

  const candidates = doc.querySelectorAll(
    'input, textarea, select, button, a[href], summary, img, ' +
    'h1, h2, h3, h4, h5, h6, [role], [contenteditable=""], [contenteditable="true"]',
  );

  for (const el of candidates) {
    if (SKIP_TAGS.has(el.tagName)) continue;
    if (el.type === 'hidden') continue;

    const role = roleOf(el);
    if (!role) continue;

    const rect = el.getBoundingClientRect();
    const style = window.getComputedStyle(el);
    const visible = isVisible(el, style, rect);
    const name = accessibleName(el, doc);
    if (!name) continue;

    const groupPath = groupPathOf(el);
    const id = identityOf(role, name, groupPath);
    // Two nodes that are indistinguishable by role, name and group are the same
    // thing as far as a spoken query is concerned; keep the first.
    if (seen.has(id)) continue;
    seen.add(id);

    out.push({
      id,
      role,
      name,
      groupPath,
      rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
      visible,
      inViewport: rect.bottom > 0 && rect.right > 0
        && rect.top < viewportHeight && rect.left < viewportWidth,
      disabled: Boolean(el.disabled) || el.getAttribute('aria-disabled') === 'true',
      el,
    });
  }
  return out;
}
