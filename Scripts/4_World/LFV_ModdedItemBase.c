// =========================================================
// LF_VStorage -- Modded ItemBase (Fix A)
//
// Generic hooks for non-barrel action-triggered containers
// (RaG_ContainerBase, A6_Openable_Base, etc.)
//
// Barrel_ColorBase has its own handler (LFV_ModdedBarrelColorBase)
// so we bail early for barrels to avoid double processing.
//
// Hooks:
//   OnRPC        -- client manifest cache from server RPC
//   EEDelete     -- untrack container on world deletion
//   CanPutInCargo/Hands -- block move if virtual items exist
// =========================================================

modded class ItemBase
{
    // --- Client-side manifest cache for action text display ---
    protected string m_LFV_CachedManifest;

    string LFV_GetCachedManifest() { return m_LFV_CachedManifest; }

    // -----------------------------------------------------------
    // RPC receiver: manifest display string from server
    // Only processes MANIFEST_UPDATE for non-barrel containers
    // -----------------------------------------------------------
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        // O(1) registry check first -- skips 99% of items immediately
        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return;

        // Barrels have their own handler in LFV_ModdedBarrelColorBase
        if (this.IsKindOf("Barrel_ColorBase"))
            return;

        if (rpc_type != LFV_RPC.MANIFEST_UPDATE)
            return;

        Param1<string> manifestData = new Param1<string>("");
        if (!ctx.Read(manifestData))
            return;
        m_LFV_CachedManifest = manifestData.param1;
    }

    // -----------------------------------------------------------
    // Untrack when entity is deleted from world
    // (admin wipe, CE cleanup, dismantle, etc.)
    //
    // Phase 1: route through centralized OnEntityDestroyed so all
    // tracking structures are purged atomically.
    // -----------------------------------------------------------
    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        // O(1) registry check first -- skips non-virtual items immediately
        if (LFV_Registry.IsVirtualContainer(this.GetType()))
        {
            // Skip barrels -- handled by LFV_ModdedBarrelColorBase
            if (!this.IsKindOf("Barrel_ColorBase"))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module)
                    module.OnEntityDestroyed(this);
            }
        }
        #endif

        super.EEDelete(parent);
    }

    // -----------------------------------------------------------
    // Block putting container in cargo if it has virtual items
    // (Safety net -- RaG/A6 already return false, but covers
    // other mods that might allow cargo placement)
    // -----------------------------------------------------------
    override bool CanPutInCargo(EntityAI parent)
    {
        // O(1) registry check first -- skips non-virtual items immediately
        if (LFV_Registry.IsVirtualContainer(this.GetType()))
        {
            // Skip barrels -- handled by LFV_ModdedBarrelColorBase
            if (!this.IsKindOf("Barrel_ColorBase"))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module && module.HasVirtualItems(this))
                    return false;
            }
        }

        return super.CanPutInCargo(parent);
    }

    // -----------------------------------------------------------
    // Block putting container in hands if it has virtual items
    // -----------------------------------------------------------
    override bool CanPutIntoHands(EntityAI parent)
    {
        // O(1) registry check first -- skips non-virtual items immediately
        if (LFV_Registry.IsVirtualContainer(this.GetType()))
        {
            // Skip barrels -- handled by LFV_ModdedBarrelColorBase
            if (!this.IsKindOf("Barrel_ColorBase"))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module && module.HasVirtualItems(this))
                    return false;
            }
        }

        return super.CanPutIntoHands(parent);
    }
}
