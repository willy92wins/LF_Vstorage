// =========================================================
// LF_VStorage -- Modded Barrel_ColorBase (Sprint 3)
//
// Applies to ALL Barrel_ColorBase (vanilla whitelist + LFV).
// Only m_LFV_CachedManifest stored here (client RPC cache).
//
// Edge case 17: Block pickup of barrels with virtual items
// Edge case 6:  Track inventory activity for auto-close
//
// Verified signatures (vanilla ItemBase):
//   CanPutInCargo(EntityAI parent)           -- itembase.c L4112
//   CanPutIntoHands(EntityAI parent)         -- itembase.c (used in powergenerator, fireplacebase)
//   CanReceiveItemIntoCargo(EntityAI item)   -- itembase.c L4141
//   CanReceiveAttachment(EntityAI attachment, int slotId) -- itembase.c L4150
// =========================================================

modded class Barrel_ColorBase
{
    // --- Client-side manifest cache (received via RPC from server) ---
    protected string m_LFV_CachedManifest;

    string LFV_GetCachedManifest() { return m_LFV_CachedManifest; }

    // -----------------------------------------------------------
    // RPC receiver: manifest display string from server
    // -----------------------------------------------------------
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        if (rpc_type == LFV_RPC.MANIFEST_UPDATE)
        {
            Param1<string> manifestData = new Param1<string>("");
            if (!ctx.Read(manifestData))
                return;
            m_LFV_CachedManifest = manifestData.param1;
        }
    }

    // -----------------------------------------------------------
    // SetActions: vanilla ActionOpenBarrel/ActionCloseBarrel are
    // modded directly (LFV_ModdedActionOpenBarrel/CloseBarrel)
    // with LFV logic. No need to remove/replace them.
    // -----------------------------------------------------------

    // -----------------------------------------------------------
    // C1 audit: Drop virtual items when vanilla whitelisted barrel
    // is destroyed. LFV_Barrel_Base has its own EEKilled; this
    // covers vanilla barrels (Barrel_Blue, Barrel_Green, etc.)
    // that are in the whitelist but don't inherit LFV_Barrel_Base.
    // -----------------------------------------------------------
    override void EEKilled(Object killer)
    {
        #ifdef SERVER
        if (GetGame().IsDedicatedServer())
        {
            LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(this);
            if (!lfvBarrel)
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module && LFV_Registry.IsVirtualContainer(this.GetType()))
                    module.OnContainerDestroyed(this);
            }
        }
        #endif

        super.EEKilled(killer);
    }

    // -----------------------------------------------------------
    // C3 fix: Untrack when entity is deleted from world
    // (admin wipe, CE cleanup, player picks up empty barrel, etc.)
    // -----------------------------------------------------------
    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module)
            module.UntrackContainer(this);
        #endif

        super.EEDelete(parent);
    }

    // -----------------------------------------------------------
    // Block putting barrel in cargo if it has virtual items
    // -----------------------------------------------------------
    override bool CanPutInCargo(EntityAI parent)
    {
        LFV_Module module = LFV_Module.GetModule();
        if (module && module.HasVirtualItems(this))
            return false;

        return super.CanPutInCargo(parent);
    }

    // -----------------------------------------------------------
    // Block putting barrel in hands if it has virtual items
    // -----------------------------------------------------------
    override bool CanPutIntoHands(EntityAI parent)
    {
        LFV_Module module = LFV_Module.GetModule();
        if (module && module.HasVirtualItems(this))
            return false;

        return super.CanPutIntoHands(parent);
    }

    // -----------------------------------------------------------
    // Track activity: item actually entered cargo (A3 audit)
    // Replaces CanReceive* which fired speculatively on UI checks
    // -----------------------------------------------------------
    override void EECargoIn(EntityAI item)
    {
        super.EECargoIn(item);

        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module)
            module.OnContainerActivity(this);
        #endif
    }

    // -----------------------------------------------------------
    // Track activity: attachment actually attached (A3 audit)
    // -----------------------------------------------------------
    override void EEItemAttached(EntityAI item, string slot_name)
    {
        super.EEItemAttached(item, slot_name);

        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module)
            module.OnContainerActivity(this);
        #endif
    }

    // -----------------------------------------------------------
    // Track activity: attachment detached (C2-fix)
    // -----------------------------------------------------------
    override void EEItemDetached(EntityAI item, string slot_name)
    {
        super.EEItemDetached(item, slot_name);

        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module)
            module.OnContainerActivity(this);
        #endif
    }
}
