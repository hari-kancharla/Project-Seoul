/**
 * Background service worker.
 *
 * ONE native-messaging connection for the whole browser, not one per tab.
 *
 * Chrome spawns a separate SeoulHost PROCESS for every connectNative() call.
 * Connecting per tab would leave one relay process per open tab, all of them
 * racing to answer the same request, and would make the overlay's idea of "the
 * active document" depend on which host happened to reply first. The port is
 * held here, at the one place in the extension that outlives individual tabs.
 */

const HOST_NAME = 'com.seoul.host';

let port = null;

function connect() {
  if (port) return port;
  try {
    port = chrome.runtime.connectNative(HOST_NAME);
  } catch (e) {
    console.error('[seoul] connectNative failed:', e);
    port = null;
    return null;
  }
  port.onMessage.addListener(onNativeMessage);
  port.onDisconnect.addListener(() => {
    const reason = chrome.runtime.lastError && chrome.runtime.lastError.message;
    console.warn('[seoul] native host disconnected:', reason || '(clean exit)');
    port = null;
  });
  console.log('[seoul] connected to', HOST_NAME);
  return port;
}

function sendNative(payload) {
  const p = connect();
  if (!p) return;
  try {
    p.postMessage(payload);
  } catch (e) {
    console.warn('[seoul] postMessage failed, dropping port:', e);
    port = null;
  }
}

async function onNativeMessage(message) {
  if (!message || message.op !== 'find') return;
  const requestId = message.requestId;

  let tab;
  try {
    [tab] = await chrome.tabs.query({ active: true, lastFocusedWindow: true });
  } catch (e) {
    sendNative({ requestId, status: 'error', error: `tabs.query failed: ${e.message}` });
    return;
  }
  if (!tab || typeof tab.id !== 'number') {
    sendNative({ requestId, status: 'error', error: 'no active tab' });
    return;
  }

  try {
    const reply = await chrome.tabs.sendMessage(tab.id, {
      type: 'seoul-find',
      query: message.query,
      requestId,
    });
    sendNative(reply || { requestId, status: 'error', error: 'content script returned nothing' });
  } catch (e) {
    // The usual cause is a page the content script cannot run on: the Chrome
    // Web Store, a chrome:// page, or a PDF viewer.
    sendNative({
      requestId,
      status: 'error',
      error: `content script unreachable: ${e && e.message ? e.message : e}`,
    });
  }
}

// Scroll updates pushed by the content script.
chrome.runtime.onMessage.addListener((message) => {
  if (message && message.type === 'seoul-update') sendNative(message.payload);
  return false;
});

chrome.runtime.onStartup.addListener(() => connect());
chrome.runtime.onInstalled.addListener(() => connect());

// And once at worker start, so a recycled worker re-establishes the link
// without waiting for a browser restart.
connect();
