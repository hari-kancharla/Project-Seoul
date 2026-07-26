// Project Seoul connected tools.

#include "seoul/browser/connectors/generic_capabilities.h"

#include <utility>

namespace seoul {

namespace {

SchemaField RequiredString(const char *name, const char *description) {
  SchemaField field;
  field.name = name;
  field.kind = SchemaFieldKind::kString;
  field.required = true;
  field.description = description;
  return field;
}

ToolDescriptor Base(const char *id, const char *name, const char *description,
                    RiskCategory risk, DataSensitivity sensitivity,
                    const char *observation) {
  ToolDescriptor descriptor;
  descriptor.id = ToolId::FromString(id);
  descriptor.name = name;
  descriptor.description = description;
  descriptor.provider = "seoul";
  descriptor.risk = risk;
  descriptor.sensitivity = sensitivity;
  descriptor.observation_contract = observation;
  if (risk == RiskCategory::kReadOnly) {
    descriptor.idempotency = IdempotencyClass::kIdempotent;
  }
  return descriptor;
}

} // namespace

std::vector<ToolDescriptor> BuildInformationCapabilities() {
  std::vector<ToolDescriptor> capabilities;

  ToolDescriptor search =
      Base("info.search.web", "Search",
           "Searches the configured engine and returns cited results as a "
           "generic entity collection.",
           RiskCategory::kReadOnly, DataSensitivity::kNone,
           "cited result collection with provenance");
  search.requires_network = true;
  search.supports_streaming = true;
  search.input_schema.fields.push_back(
      RequiredString("query", "What to search for."));
  SchemaField max_results;
  max_results.name = "max_results";
  max_results.kind = SchemaFieldKind::kInteger;
  max_results.has_range = true;
  max_results.min_value = 1;
  max_results.max_value = 50;
  search.input_schema.fields.push_back(std::move(max_results));
  capabilities.push_back(std::move(search));

  ToolDescriptor extract =
      Base("page.extract.structured", "Extract from page",
           "Extracts bounded structured data matching a requested semantic "
           "schema from one open page.",
           RiskCategory::kReadOnly, DataSensitivity::kPageContent,
           "semantic rows conforming to the requested schema");
  extract.input_schema.fields.push_back(
      RequiredString("tab_key", "Stable key of the tab to read."));
  extract.input_schema.fields.push_back(RequiredString(
      "wanted_schema_json",
      "The semantic schema (fields and roles) the extraction must fill."));
  extract.approval = ApprovalPolicy::kFirstUsePerScope;
  capabilities.push_back(std::move(extract));

  return capabilities;
}

std::vector<ToolDescriptor> BuildBrowserCapabilities() {
  std::vector<ToolDescriptor> capabilities;

  {
    ToolDescriptor open =
        Base("browser.tabs.open", "Open tab",
             "Opens a URL in a new tab in the current window and workspace.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "tab inserted and observed by the lifecycle bridge");
    SchemaField url;
    url.name = "url";
    url.kind = SchemaFieldKind::kUrl;
    url.required = true;
    open.input_schema.fields.push_back(std::move(url));
    SchemaField retained;
    retained.name = "retained";
    retained.kind = SchemaFieldKind::kBoolean;
    retained.description = "Keep the tab out of temporary auto-archive.";
    open.input_schema.fields.push_back(std::move(retained));
    // The exact destination origin/window grant is reusable for ordinary
    // reversible opens. Runtime routing can strengthen this to always-required
    // for ask-user and external-application rules.
    open.approval = ApprovalPolicy::kFirstUsePerScope;
    capabilities.push_back(std::move(open));
  }
  {
    ToolDescriptor preview =
        Base("browser.preview.open", "Open link preview",
             "Opens a URL in an ephemeral overlay bound to an exact parent "
             "tab. It remains outside the tab strip until the user explicitly "
             "promotes it to a tab or split.",
             RiskCategory::kReversibleMutation, DataSensitivity::kPageContent,
             "window-bound Preview overlay exists outside the tab strip");
    SchemaField url;
    url.name = "url";
    url.kind = SchemaFieldKind::kUrl;
    url.required = true;
    preview.input_schema.fields.push_back(std::move(url));
    preview.input_schema.fields.push_back(
        RequiredString("tab_key", "Exact parent tab for focus restoration."));
    preview.approval = ApprovalPolicy::kFirstUsePerScope;
    capabilities.push_back(std::move(preview));
  }
  {
    ToolDescriptor activate =
        Base("browser.tabs.activate", "Activate tab",
             "Brings an existing tab to the foreground by its stable key.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "tab activation observed");
    activate.input_schema.fields.push_back(
        RequiredString("tab_key", "Stable key of the tab."));
    capabilities.push_back(std::move(activate));
  }
  {
    ToolDescriptor close = Base(
        "browser.tabs.close", "Close tab", "Closes one tab by its stable key.",
        RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
        "tab removal observed");
    close.input_schema.fields.push_back(
        RequiredString("tab_key", "Stable key of the tab."));
    capabilities.push_back(std::move(close));
  }
  {
    ToolDescriptor enumerate = Base(
        "browser.tabs.enumerate", "List tabs",
        "Lists open tabs with stable keys, titles, and workspace roles.",
        RiskCategory::kReadOnly, DataSensitivity::kOrganization, "tab listing");
    capabilities.push_back(std::move(enumerate));
  }
  {
    ToolDescriptor switch_workspace =
        Base("browser.workspace.switch", "Switch workspace",
             "Switches the current window to another workspace.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "workspace switch transaction observed to a terminal phase");
    switch_workspace.input_schema.fields.push_back(
        RequiredString("workspace_id", "Target workspace id."));
    capabilities.push_back(std::move(switch_workspace));
  }
  {
    ToolDescriptor activate_scene =
        Base("scene.activate", "Activate scene",
             "Applies a Scene: workspace, theme, site layers, routing, "
             "lifecycle, and assistant defaults.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "scene activation plan executed");
    activate_scene.input_schema.fields.push_back(
        RequiredString("scene_id", "Scene to activate."));
    capabilities.push_back(std::move(activate_scene));
  }
  {
    ToolDescriptor compact =
        Base("browser.compact.set", "Set compact mode",
             "Enters or exits compact browser chrome for the current "
             "Workspace. Compact state is remembered per Workspace and "
             "remains keyboard-accessible.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "native vertical chrome reaches the requested collapse and "
             "reveal-on-hover state");
    SchemaField enabled;
    enabled.name = "enabled";
    enabled.kind = SchemaFieldKind::kBoolean;
    enabled.required = true;
    enabled.description = "True to enter compact mode; false to exit.";
    compact.input_schema.fields.push_back(std::move(enabled));
    capabilities.push_back(std::move(compact));
  }
  {
    // Operates on the active tab of the task's window and returns each visible
    // control with a stable, generation-scoped `handle`; page.act.* consume
    // those handles. There is no free-text element selector anywhere in the
    // page pipeline.
    ToolDescriptor observe = Base(
        "page.observe.text", "Read page text",
        "Returns the visible semantic elements of the active page, each with a "
        "stable handle usable by page.act.click and page.act.type.",
        RiskCategory::kReadOnly, DataSensitivity::kPageContent,
        "bounded page text");
    observe.approval = ApprovalPolicy::kFirstUsePerScope;
    capabilities.push_back(std::move(observe));
  }
  {
    ToolDescriptor click = Base(
        "page.act.click", "Click on page",
        "Clicks the element named by a handle from a prior page.observe.text.",
        RiskCategory::kIrreversibleMutation, DataSensitivity::kPageContent,
        "post-click page state observed");
    click.approval = ApprovalPolicy::kFirstUsePerScope;
    click.input_schema.fields.push_back(RequiredString(
        "handle", "Element handle from a prior page observation."));
    capabilities.push_back(std::move(click));
  }
  {
    ToolDescriptor type_text =
        Base("page.act.type", "Type into page",
             "Types a value into the field named by a handle from a prior "
             "page.observe.text.",
             RiskCategory::kIrreversibleMutation, DataSensitivity::kPageContent,
             "field value observed after typing");
    type_text.approval = ApprovalPolicy::kFirstUsePerScope;
    type_text.input_schema.fields.push_back(RequiredString(
        "handle", "Element handle from a prior page observation."));
    type_text.input_schema.fields.push_back(
        RequiredString("value", "The text to type into the field."));
    capabilities.push_back(std::move(type_text));
  }
  {
    ToolDescriptor submit =
        Base("page.act.submit", "Submit form",
             "Activates a submit control identified by a handle from a prior "
             "page observation. Always approval-gated.",
             RiskCategory::kExternalSideEffect, DataSensitivity::kPageContent,
             "navigation or confirmation state observed after submit");
    submit.approval = ApprovalPolicy::kAlwaysRequired;
    submit.input_schema.fields.push_back(RequiredString(
        "handle", "Submit-control handle from a prior page observation."));
    capabilities.push_back(std::move(submit));
  }
  {
    ToolDescriptor split =
        Base("browser.split.create", "Create split",
             "Creates a two-pane split from two tabs in the same workspace.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "split creation observed");
    split.input_schema.fields.push_back(
        RequiredString("first_tab_key", "Pane A tab key."));
    split.input_schema.fields.push_back(
        RequiredString("second_tab_key", "Pane B tab key."));
    capabilities.push_back(std::move(split));
  }
  {
    ToolDescriptor archive =
        Base("browser.tabs.archive", "Archive tab",
             "Archives a temporary tab after protection checks; recoverable.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "tab archived and recoverable");
    archive.input_schema.fields.push_back(
        RequiredString("tab_key", "Stable key of the tab."));
    capabilities.push_back(std::move(archive));
  }
  {
    ToolDescriptor restore =
        Base("browser.tabs.restore", "Restore archived tab",
             "Reopens one recoverable archived tab in its original workspace.",
             RiskCategory::kReversibleMutation, DataSensitivity::kOrganization,
             "tab reopened and archive record consumed");
    restore.input_schema.fields.push_back(
        RequiredString("archive_id", "Stable id of the archived tab."));
    capabilities.push_back(std::move(restore));
  }

  return capabilities;
}

} // namespace seoul
