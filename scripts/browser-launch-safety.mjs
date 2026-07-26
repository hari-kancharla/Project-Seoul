// Refuse a macOS browser launch when Codex is still inside its restricted
// process sandbox. Chromium's headless platform registration aborts in that
// environment and macOS shows a desktop crash dialog. An approved process
// launch clears CODEX_SANDBOX before this code runs.

export function assertBrowserLaunchPermitted({
  env = process.env,
  platform = process.platform,
} = {}) {
  if (platform === 'darwin' && env.CODEX_SANDBOX) {
    throw new Error(
      'Refusing to launch Chromium from the restricted Codex sandbox. ' +
        'Approve the isolated browser-process launch first.',
    );
  }
}
