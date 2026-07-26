/**
 * Seoul element resolver.
 *
 * Runs INSIDE THE PAGE. Never ships the accessibility tree across the bridge.
 * Takes a spoken query, returns one element (or a short disambiguation list).
 *
 * The hard case this is built for is a checkout page containing both
 * "Shipping address / Address line 1" and "Billing address / Address line 1".
 * Naive name matching ties on both. Ancestor group context breaks the tie,
 * which is why `groupPath` is a first-class input rather than an afterthought.
 */

const STOP = new Set([
  'the', 'a', 'an', 'my', 'your', 'this', 'that', 'to', 'for', 'of', 'on', 'in',
  'at', 'is', 'are', 'where', 'what', 'which', 'i', 'me', 'can', 'do', 'does',
  'please', 'show', 'find', 'click', 'go', 'take', 'put', 'change', 'edit',
  'update', 'enter', 'type', 'fill', 'and', 'or', 'it', 'with', 'from',
  // Voice-specific: spoken queries carry far more filler than typed ones.
  'uh', 'um', 'er', 'hmm', 'you', 'we', 'hey', 'ok', 'okay', 'so', 'just',
  'like', 'want', 'need', 'get', 'see', 'look', 'tell', 'let', 'would',
  'could', 'should', 'will', 'now', 'there', 'here', 'thing', 'stuff', 'again',
]);

// Words in the query that imply a role, mapped to the ARIA roles they allow.
const ROLE_HINTS = [
  [['button', 'submit', 'press', 'tap'], ['button']],
  [['field', 'input', 'box', 'textbox', 'blank'], ['textbox', 'searchbox', 'combobox', 'spinbutton']],
  [['link', 'anchor'], ['link']],
  [['checkbox', 'check', 'tick', 'toggle'], ['checkbox', 'switch']],
  [['dropdown', 'select', 'menu', 'picker'], ['combobox', 'listbox', 'menu']],
  [['tab'], ['tab']],
  [['radio', 'option'], ['radio']],
  [['heading', 'title', 'header'], ['heading']],
  [['image', 'picture', 'photo', 'logo'], ['img']],
];

const INTERACTIVE = new Set([
  'button', 'link', 'textbox', 'searchbox', 'combobox', 'listbox', 'checkbox',
  'radio', 'switch', 'slider', 'spinbutton', 'tab', 'menuitem', 'option',
]);

export function normalize(s) {
  return (s || '')
    .toLowerCase()
    .replace(/[\u2018\u2019]/g, "'")
    .replace(/[^a-z0-9'\s]/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

export function tokenize(s) {
  return normalize(s).split(' ').filter(Boolean);
}

/** Content tokens: stopwords removed, light singularisation. */
export function contentTokens(s) {
  return tokenize(s)
    .filter((t) => !STOP.has(t))
    .map((t) => (t.length > 3 && t.endsWith('s') && !t.endsWith('ss') ? t.slice(0, -1) : t));
}

function extractRoleHints(queryTokens) {
  const allowed = new Set();
  const consumed = new Set();
  for (const [words, roles] of ROLE_HINTS) {
    for (const w of words) {
      if (queryTokens.includes(w)) {
        roles.forEach((r) => allowed.add(r));
        consumed.add(w);
      }
    }
  }
  return { allowed, consumed };
}

/**
 * Coverage of the query by a piece of text: what fraction of the query's
 * content tokens appear. Asymmetric on purpose. A long name that contains
 * every query token should score highly; a short name that contains only
 * one of four should not.
 */
function coverage(queryTokens, textTokens) {
  if (queryTokens.length === 0) return 0;
  const set = new Set(textTokens);
  let hit = 0;
  for (const q of queryTokens) {
    if (set.has(q)) { hit += 1; continue; }
    // prefix match catches "ship" vs "shipping", "addr" vs "address"
    if (textTokens.some((t) => t.length >= 4 && q.length >= 4 && (t.startsWith(q) || q.startsWith(t)))) {
      hit += 0.75;
    }
  }
  return hit / queryTokens.length;
}

/** Bonus when the query appears as a contiguous phrase in the text. */
function phraseBonus(queryTokens, textTokens) {
  if (queryTokens.length < 2) return 0;
  const q = queryTokens.join(' ');
  const t = textTokens.join(' ');
  return t.includes(q) ? 0.25 : 0;
}

/**
 * @typedef {object} AXCandidate
 * @property {string} id            stable handle the caller uses later
 * @property {string} role          computed ARIA role
 * @property {string} name          computed accessible name
 * @property {string[]} groupPath   ancestor group/fieldset/region/landmark names, outermost first
 * @property {{x,y,width,height}} rect  getBoundingClientRect()
 * @property {boolean} visible      not display:none, not visibility:hidden, not aria-hidden
 * @property {boolean} inViewport
 * @property {boolean} [disabled]
 */

export const WEIGHTS = {
  name: 1.0,
  group: 0.55,      // strong enough to break shipping/billing ties, weak enough not to dominate
  phrase: 1.0,
  roleMatch: 0.30,
  roleMismatch: -0.45,
  interactive: 0.08,
  inViewport: 0.06,
  disabled: -0.20,
};

/** Precompute per-candidate tokens once at harvest time, not per query. */
export function prepare(candidates) {
  for (const c of candidates) {
    if (!c._nt) c._nt = contentTokens(c.name);
    if (!c._gt) c._gt = contentTokens(c.groupPath.join(' '));
  }
  return candidates;
}

export function scoreCandidate(candidate, queryTokens, roleHints) {
  const nameTokens = candidate._nt || contentTokens(candidate.name);
  const groupTokens = candidate._gt || contentTokens(candidate.groupPath.join(' '));

  const nameCov = coverage(queryTokens, nameTokens);
  const groupCov = coverage(queryTokens, groupTokens);

  // Tokens the name already explains should not be double counted by the group.
  const nameSet = new Set(nameTokens);
  const residual = queryTokens.filter((q) => !nameSet.has(q));
  // NOT diluted by residual.length/queryTokens.length. When two elements share
  // an identical accessible name, the residual token is the ONLY discriminator,
  // so it must carry full weight or the pair never clears the confidence margin.
  const residualGroupCov = residual.length ? coverage(residual, groupTokens) : 0;

  let score = 0;
  score += WEIGHTS.name * nameCov;
  score += WEIGHTS.group * (residual.length ? residualGroupCov : groupCov * 0.35);
  score += WEIGHTS.phrase * phraseBonus(queryTokens, nameTokens);

  if (roleHints.allowed.size > 0) {
    score += roleHints.allowed.has(candidate.role) ? WEIGHTS.roleMatch : WEIGHTS.roleMismatch;
  }
  if (INTERACTIVE.has(candidate.role)) score += WEIGHTS.interactive;
  if (candidate.inViewport) score += WEIGHTS.inViewport;
  if (candidate.disabled) score += WEIGHTS.disabled;

  return score;
}

/**
 * Enumerated field series: "Address line 1" / "Address line 2", "Phone 1" /
 * "Phone 2". These are not real ambiguity. A person who says "the shipping
 * address field" means the first one, and asking them to choose is annoying.
 * Two candidates are series siblings when they share a role and a group and
 * their names are identical once trailing ordinals are stripped.
 */
function isSeriesSibling(a, b) {
  if (a.role !== b.role) return false;
  if ((a.groupPath[a.groupPath.length - 1] || '') !== (b.groupPath[b.groupPath.length - 1] || '')) return false;
  const strip = (n) => normalize(n).replace(/\s*\d+$/, '').trim();
  const sa = strip(a.name), sb = strip(b.name);
  return sa.length > 0 && sa === sb && normalize(a.name) !== normalize(b.name);
}

/**
 * @returns {{status:'confident'|'ambiguous'|'none', best?:object, candidates:object[]}}
 */
export function resolve(query, candidates, opts = {}) {
  const { minScore = 0.55, margin = 0.18, maxCandidates = 3 } = opts;

  const rawTokens = tokenize(query);
  const roleHints = extractRoleHints(rawTokens);
  // Role words are instructions about *what kind* of thing, not part of the label.
  const queryTokens = contentTokens(query).filter((t) => !roleHints.consumed.has(t));
  const effective = queryTokens.length ? queryTokens : contentTokens(query);

  prepare(candidates);
  const scored = candidates
    .filter((c) => c.visible && c.rect.width > 0 && c.rect.height > 0)
    .map((c) => ({ ...c, score: scoreCandidate(c, effective, roleHints) }))
    .sort((a, b) => b.score - a.score);

  if (scored.length === 0 || scored[0].score < minScore) {
    return { status: 'none', candidates: scored.slice(0, maxCandidates) };
  }

  const top = scored[0];
  // Compare against the best genuinely different alternative, not against a
  // sibling in the same enumerated series.
  const rival = scored.slice(1).find((c) => !isSeriesSibling(top, c));
  if (rival && top.score - rival.score < margin) {
    return { status: 'ambiguous', candidates: scored.slice(0, maxCandidates) };
  }

  return { status: 'confident', best: top, candidates: scored.slice(0, maxCandidates) };
}

/** Payload actually sent across the bridge. Small on purpose. */
export function toWirePayload(requestId, result, viewportContext) {
  const slim = (c) => ({
    id: c.id, role: c.role, name: c.name,
    group: c.groupPath[c.groupPath.length - 1] || null,
    rect: c.rect,
    score: Math.round(c.score * 1000) / 1000,
  });
  return {
    requestId,
    status: result.status,
    best: result.best ? slim(result.best) : null,
    candidates: result.candidates.map(slim),
    ctx: viewportContext,
  };
}
