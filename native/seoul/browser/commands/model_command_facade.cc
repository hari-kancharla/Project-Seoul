// Project Seoul outbound browser command layer.

#include "seoul/browser/commands/model_command_facade.h"

namespace seoul {

ModelCommandFacade::ModelCommandFacade(OrganizationModel* model)
    : model_(model) {}

CommandStatusResult ModelCommandFacade::Execute(const BrowserCommand& command) {
  if (!model_) {
    return CommandErr(CommandError::kInvalidCommand);
  }
  switch (command.kind) {
    case CommandKind::kCreateWorkspace: {
      auto result = model_->CreateWorkspace(command.name);
      return result.has_value() ? CommandOk()
                                : CommandErr(CommandError::kInvalidCommand);
    }
    case CommandKind::kRenameWorkspace:
      return model_->RenameWorkspace(*command.workspace_id, command.name)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kSetWorkspaceIcon:
      return model_->SetWorkspaceIcon(*command.workspace_id, command.icon)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kReorderWorkspace:
      return model_->ReorderWorkspace(*command.workspace_id, command.order)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kArchiveWorkspace:
      return model_->ArchiveWorkspace(*command.workspace_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kRestoreWorkspace:
      return model_->RestoreWorkspace(*command.workspace_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kDeleteWorkspace:
      return model_->DeleteWorkspace(*command.workspace_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kSetActiveWorkspace:
      return model_->SetActiveWorkspaceForWindow(command.window.value(),
                                                 *command.workspace_id)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kMoveTabMembership:
      return model_->MoveTabToWorkspace(*command.membership_id,
                                        *command.workspace_id)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kMarkTemporary:
      return model_->MarkTabTemporary(*command.membership_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kRetainTab:
      return model_->RetainTab(*command.membership_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kSetWorkspacePinned:
      return model_->PinTab(*command.membership_id, command.saved_root_url)
                     .has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kUnsetWorkspacePinned:
      return model_->UnpinTab(*command.membership_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kCreateOrUpdateEssential: {
      auto result = model_->CreateOrUpdateEssential(
          command.essential_id.value_or(EssentialId()), command.name,
          command.saved_root_url);
      return result.has_value() ? CommandOk()
                                : CommandErr(CommandError::kInvalidCommand);
    }
    case CommandKind::kRemoveEssential:
      return model_->RemoveEssential(*command.essential_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    case CommandKind::kAddRoutingRule: {
      auto result = model_->AddRoutingRule(command.routing_rule);
      return result.has_value() ? CommandOk()
                                : CommandErr(CommandError::kInvalidCommand);
    }
    case CommandKind::kRemoveRoutingRule:
      return model_->RemoveRoutingRule(*command.routing_rule_id).has_value()
                 ? CommandOk()
                 : CommandErr(CommandError::kInvalidCommand);
    default:
      return CommandErr(CommandError::kUnsupportedCommand);
  }
}

}  // namespace seoul
