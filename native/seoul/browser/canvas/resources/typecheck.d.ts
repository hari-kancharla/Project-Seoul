// Host-only declarations for checking the shipping WebUI without a generated
// Chromium out directory. Chromium's real build uses the authoritative Lit and
// Mojo declarations; this file is excluded from build_webui.

declare module '*canvas.mojom-webui.js' {
  export const ComponentEventKind: {
    kActivate: number;
    kValueChanged: number;
    kSubmit: number;
    kSelect: number;
    kDismiss: number;
  };

  interface Listener {
    addListener(callback: (...args: never[]) => void): void;
  }

  export class PageCallbackRouter {
    $: {bindNewPipeAndPassRemote(): unknown};
    pushSurface: Listener;
    applySurfacePatch: Listener;
    setStatus: Listener;
    setPageContext: Listener;
    pushTaskSnapshot: Listener;
    pushThreadSnapshot: Listener;
    pushLibrarySnapshot: Listener;
    openBoostEditor: Listener;
  }

  export class PageHandlerRemote {
    $: {bindNewPipeAndPassReceiver(): unknown};
    requestInitialState(): void;
    notifyComponentEvent(event: unknown): void;
    submitTurn(input: {text: string, threadId: string}): void;
    getThreadSnapshot(
        threadId: string): Promise<{snapshotJson: string}>;
    pauseTask(taskId: string): void;
    resumeTask(taskId: string): void;
    cancelActiveTask(taskId: string): void;
    approveStep(taskId: string, stepId: string, approved: boolean): void;
    provideTaskInput(taskId: string, stepId: string, input: string): void;
    getLibrarySnapshot(): Promise<{snapshotJson: string}>;
    getSiteLayerSnapshot(): Promise<{snapshotJson: string}>;
    upsertSiteLayer(
        layerId: string, name: string, originPattern: string,
        sceneScope: string, enabled: boolean,
        adjustments: Array<{
          kind: string;
          selectors: string[];
          textValue: string;
          numericValue: number;
          density: string;
        }>): Promise<{snapshotJson: string}>;
    setSiteLayerEnabled(
        layerId: string, enabled: boolean): Promise<{snapshotJson: string}>;
    deleteSiteLayer(layerId: string): Promise<{snapshotJson: string}>;
    zapSiteLayer(
        layerId: string): Promise<{snapshotJson: string, changed: boolean}>;
    cancelSiteLayerZap(): void;
    getStudioSnapshot(): Promise<{snapshotJson: string}>;
    saveLocalProvider(
        endpointUrl: string,
        modelId: string): Promise<{snapshotJson: string}>;
    clearLocalProvider(): Promise<{snapshotJson: string}>;
    checkLocalProvider(): Promise<{snapshotJson: string}>;
    saveCloudProvider(
        modelId: string, enabled: boolean, reasoningSecret: string,
        voiceSecret: string): Promise<{snapshotJson: string}>;
    clearCloudProvider(): Promise<{snapshotJson: string}>;
    upsertEssential(
        essentialId: string, name: string,
        rootUrl: string): Promise<{snapshotJson: string}>;
    deleteEssential(essentialId: string): Promise<{snapshotJson: string}>;
    upsertTheme(theme: {
      id: string;
      name: string;
      scheme: string;
      background: string;
      surface: string;
      text: string;
      mutedText: string;
      accent: string;
      accentText: string;
      border: string;
      error: string;
      fontFamily: string;
      baseSizePx: number;
      scaleRatio: number;
      lineHeightPermille: number;
      reducedMotion: boolean;
      reducedTransparency: boolean;
      baseDurationMs: number;
      cornerRadiusPx: number;
    }): Promise<{snapshotJson: string}>;
    deleteTheme(themeId: string): Promise<{snapshotJson: string}>;
    activateTheme(themeId: string): Promise<{snapshotJson: string}>;
    upsertScene(scene: {
      id: string;
      name: string;
      workspaceId: string;
      themeId: string;
      siteLayerIds: string[];
      routingRuleIds: string[];
      workflowShortcutIds: string[];
      archiveTemporaryTabs: boolean;
      idleArchiveMinutes: number;
      restoreOnActivation: boolean;
      allowNetwork: boolean;
      allowCloudModels: boolean;
      maxSensitivity: string;
      defaultConnectors: string[];
      preferCompact: boolean;
    }): Promise<{snapshotJson: string}>;
    deleteScene(sceneId: string): Promise<{snapshotJson: string}>;
    activateScene(sceneId: string): Promise<{snapshotJson: string}>;
    upsertRoutingRule(rule: {
      id: string;
      priority: number;
      matchType: string;
      pattern: string;
      sourceWorkspaceId: string;
      requireUserGesture: boolean;
      disposition: string;
      targetWorkspaceId: string;
      enabled: boolean;
    }): Promise<{snapshotJson: string}>;
    deleteRoutingRule(ruleId: string): Promise<{snapshotJson: string}>;
    upsertWorkflow(workflowJson: string): Promise<{snapshotJson: string}>;
    deleteWorkflow(workflowId: string): Promise<{snapshotJson: string}>;
    duplicateWorkflow(workflowId: string): Promise<{snapshotJson: string}>;
    runWorkflow(workflowId: string): Promise<{taskId: string}>;
    createBoard(name: string): Promise<{snapshotJson: string}>;
    renameBoard(boardId: string, name: string): Promise<{snapshotJson: string}>;
    setBoardArchived(boardId: string, archived: boolean): Promise<{snapshotJson: string}>;
    deleteBoard(boardId: string): Promise<{snapshotJson: string}>;
    addBoardElement(
        boardId: string, elementId: string, kind: string, title: string,
        text: string, reference: string, origin: string, x: number, y: number,
        width: number, height: number,
        zIndex: number): Promise<{snapshotJson: string}>;
    updateBoardElement(
        boardId: string, elementId: string, kind: string, title: string,
        text: string, reference: string, origin: string, x: number, y: number,
        width: number, height: number,
        zIndex: number): Promise<{snapshotJson: string}>;
    removeBoardElement(
        boardId: string, elementId: string): Promise<{snapshotJson: string}>;
    upsertLiveCollection(
        collectionId: string, name: string, refreshCapability: string,
        sourceLocator: string, refreshIntervalMinutes: number,
        enabled: boolean): Promise<{snapshotJson: string}>;
    setLiveCollectionEnabled(
        collectionId: string,
        enabled: boolean): Promise<{snapshotJson: string}>;
    refreshLiveCollection(
        collectionId: string): Promise<{snapshotJson: string}>;
    deleteLiveCollection(
        collectionId: string): Promise<{snapshotJson: string}>;
    openLiveCollectionItem(
        collectionId: string,
        stableKey: string): Promise<{taskId: string}>;
    createRealtimeVoiceSession(): Promise<{sessionJson: string}>;
    submitRealtimeToolCall(call: {
      callId: string;
      name: string;
      argumentsJson: string;
    }): Promise<{outputJson: string}>;
  }

  export const PageHandlerFactory: {
    getRemote(): {
      createPageHandler(page: unknown, handler: unknown): void;
    };
  };
}

declare module '*canvas.css.js' {
  export function getCss(): unknown;
}
