import { test } from 'node:test';
import assert from 'node:assert/strict';
import { resolve, toWirePayload, contentTokens } from './elementResolver.mjs';

const el = (id, role, name, groupPath, extra = {}) => ({
  id, role, name, groupPath,
  rect: { x: 100, y: 200, width: 300, height: 36 },
  visible: true, inViewport: true, ...extra,
});

// A realistic checkout page. Shipping and billing carry IDENTICAL field names.
const CHECKOUT = [
  el('e1', 'heading', 'Checkout', []),
  el('e2', 'textbox', 'Email', ['Contact']),
  el('e3', 'heading', 'Shipping address', ['Shipping address']),
  el('e4', 'textbox', 'First name', ['Shipping address']),
  el('e5', 'textbox', 'Last name', ['Shipping address']),
  el('e6', 'textbox', 'Address line 1', ['Shipping address']),
  el('e7', 'textbox', 'Address line 2', ['Shipping address']),
  el('e8', 'textbox', 'City', ['Shipping address']),
  el('e9', 'combobox', 'Country', ['Shipping address']),
  el('e10', 'textbox', 'ZIP code', ['Shipping address']),
  el('e11', 'checkbox', 'Billing address is the same as shipping', ['Billing address']),
  el('e12', 'textbox', 'First name', ['Billing address']),
  el('e13', 'textbox', 'Address line 1', ['Billing address']),
  el('e14', 'textbox', 'City', ['Billing address']),
  el('e15', 'combobox', 'Country', ['Billing address']),
  el('e16', 'textbox', 'ZIP code', ['Billing address']),
  el('e17', 'button', 'Continue to payment', []),
  el('e18', 'link', 'Return to cart', []),
  el('e19', 'button', 'Apply', ['Discount code']),
  el('e20', 'textbox', 'Discount code', ['Discount code']),
];

const r = (q, list = CHECKOUT, opts) => resolve(q, list, opts);

test('group context breaks the shipping / billing tie', () => {
  const res = r('where do I change my shipping address');
  assert.equal(res.status, 'confident', `status was ${res.status}`);
  assert.equal(res.best.groupPath[0], 'Shipping address');
});

test('the mirrored billing query resolves to the billing group', () => {
  const res = r('billing address line 1');
  assert.equal(res.status, 'confident', `status was ${res.status}`);
  assert.equal(res.best.id, 'e13');
});

test('role hint routes "field" away from the heading with the same name', () => {
  const res = r('shipping address field');
  assert.equal(res.best.role, 'textbox');
  assert.notEqual(res.best.id, 'e3'); // must not pick the <h2> "Shipping address"
});

test('role hint routes "button" to the button', () => {
  const res = r('the continue button');
  assert.equal(res.status, 'confident');
  assert.equal(res.best.id, 'e17');
});

test('distinguishes the Apply button from the Discount code textbox', () => {
  assert.equal(r('discount code field').best.id, 'e20');
  assert.equal(r('apply button').best.id, 'e19');
});

test('a genuinely ambiguous query returns candidates instead of guessing', () => {
  const res = r('country');
  assert.equal(res.status, 'ambiguous', `status was ${res.status}`);
  assert.equal(res.candidates.length >= 2, true);
  const groups = res.candidates.map((c) => c.groupPath[0]);
  assert.ok(groups.includes('Shipping address') && groups.includes('Billing address'));
});

test('nonsense query returns none rather than a bad guess', () => {
  const res = r('launch the nuclear submarine');
  assert.equal(res.status, 'none');
});

test('invisible and zero-size elements are excluded entirely', () => {
  const list = [
    el('hidden', 'button', 'Continue to payment', [], { visible: false }),
    el('zero', 'button', 'Continue to payment', [], { rect: { x: 0, y: 0, width: 0, height: 0 } }),
    el('real', 'button', 'Continue to payment', []),
  ];
  assert.equal(resolve('continue to payment', list).best.id, 'real');
});

test('disabled controls lose to enabled equivalents', () => {
  const list = [
    el('off', 'button', 'Place order', [], { disabled: true }),
    el('on', 'button', 'Place order', []),
  ];
  assert.equal(resolve('place order button', list).best.id, 'on');
});

test('prefix matching survives spoken abbreviation ("ship" for "shipping")', () => {
  const res = r('ship address line 1');
  assert.equal(res.best.groupPath[0], 'Shipping address');
});

test('filler words in natural speech do not change the answer', () => {
  const a = r('shipping address line 1');
  const b = r('uh can you show me the shipping address line 1 please');
  assert.equal(a.best.id, b.best.id);
});

test('contentTokens strips stopwords and singularises', () => {
  assert.deepEqual(contentTokens('where are my shipping addresses'), ['shipping', 'addresse']);
});

test('wire payload stays tiny', () => {
  const res = r('shipping address line 1');
  const payload = toWirePayload('req-1', res, { screenX: 0, screenY: 38 });
  const bytes = Buffer.byteLength(JSON.stringify(payload), 'utf8');
  assert.ok(bytes < 1200, `payload was ${bytes} bytes`);
  assert.ok(bytes < 1024 * 1024, 'must never approach the 1MB native messaging limit');
});

test('resolver is fast enough for the hot path', () => {
  const big = [];
  for (let i = 0; i < 3000; i++) big.push(el(`x${i}`, 'link', `Item number ${i}`, ['Results']));
  const list = [...CHECKOUT, ...big];
  const t0 = performance.now();
  for (let i = 0; i < 50; i++) resolve('shipping address line 1', list);
  const perCall = (performance.now() - t0) / 50;
  assert.ok(perCall < 12, `resolver took ${perCall.toFixed(2)}ms per call on 3020 nodes`);
  console.log(`    resolver: ${perCall.toFixed(2)}ms per call over 3020 candidates`);
});
