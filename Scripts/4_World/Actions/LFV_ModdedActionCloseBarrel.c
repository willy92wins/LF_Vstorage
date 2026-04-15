// =========================================================
// LF_VStorage -- Modded ActionCloseBarrel
//
// Same approach as ModdedActionOpenBarrel: mod vanilla directly
// instead of replacing with a separate class.
//
// LFV logic added:
//   - ActionCondition: block during processing (LFV barrels)
//   - OnExecuteServer: module.OnCloseContainer for whitelisted
//     + SkipCloseVirtualize flag for LFV barrels (C3 audit)
// =========================================================

modded class ActionCloseBarrel
{
    // -----------------------------------------------------------
    // Block action during virtualize/restore (LFV barrels only).
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
    // Server: LFV virtualization for whitelisted barrels,
    // vanilla passthrough for non-whitelisted.
    // Always calls super for the barrel.Close() behavior.
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
                // RATE-FIX: CheckRateLimit removed from close action.
                // Previously, open and close shared a single cooldown timestamp,
                // so opening then quickly closing triggered a false rate-limit error.
                // Close doesn't need rate limiting because:
                //   - Empty barrel: OnCloseContainer is a no-op (HasCargoOrAttachments check)
                //   - Barrel with items: virtualization blocks further actions via m_IsProcessing
                // Rate limiting remains on open action only (LFV_ModdedActionOpenBarrel).

                // C3 audit: set flag on LFV barrels so Close() override
                // skips its OnCloseContainer call (prevents double-call)
                LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(container);
                if (lfvBarrel)
                    lfvBarrel.m_LFV_SkipCloseVirtualize = true;

                module.OnCloseContainer(container, player);
            }
        }

        // Vanilla barrel.Close() behavior
        super.OnExecuteServer(action_data);
    }
}
