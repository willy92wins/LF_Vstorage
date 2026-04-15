// =========================================================
// LF_VStorage -- Modded ActionOpenBarrel
//
// Instead of replacing vanilla ActionOpenBarrel with a separate
// LFV_ActionOpen class (which requires RemoveAction/AddAction
// and can conflict with the action system), we mod the vanilla
// action directly. This guarantees the action always shows
// because we use the exact same class the engine expects.
//
// LFV logic added:
//   - ActionCondition: block during processing (LFV barrels)
//   - OnActionInfoUpdate: dynamic text with item count/manifest
//   - OnExecuteServer: module.OnOpenContainer for whitelisted
// =========================================================

modded class ActionOpenBarrel
{
    // -----------------------------------------------------------
    // Dynamic text for LFV barrels and whitelisted vanilla barrels.
    // Client-side: LFV_Registry is not initialized, so we detect
    // LFV barrels by class cast and vanilla barrels by cached manifest.
    // -----------------------------------------------------------
    // DISABLED: manifest preview in action text (showing items incorrectly)
    // TODO: re-enable once display is fixed
    override void OnActionInfoUpdate(PlayerBase player, ActionTarget target, ItemBase item)
    {
        m_Text = "#open";
    }

    // -----------------------------------------------------------
    // Block action during virtualize/restore (LFV barrels only).
    // Non-LFV barrels pass through to vanilla condition.
    // -----------------------------------------------------------
    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        Object obj = target.GetObject();
        if (obj)
        {
            LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(obj);
            if (lfvBarrel && lfvBarrel.LFV_GetIsProcessing())
                return false;
        }

        return super.ActionCondition(player, target, item);
    }

    // -----------------------------------------------------------
    // Server: LFV restore flow for whitelisted barrels,
    // vanilla passthrough for non-whitelisted.
    // Always calls super for the barrel.Open() behavior.
    // -----------------------------------------------------------
    override void OnExecuteServer(ActionData action_data)
    {
        Object targetObj = action_data.m_Target.GetObject();
        ItemBase container = ItemBase.Cast(targetObj);

        if (container && LFV_Registry.IsVirtualContainer(container.GetType()))
        {
            PlayerBase player = PlayerBase.Cast(action_data.m_Player);
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                if (!module.CheckRateLimit(player, container))
                    return;

                module.OnOpenContainer(container, player);
            }
        }

        // Vanilla barrel.Open() behavior
        super.OnExecuteServer(action_data);
    }
}
