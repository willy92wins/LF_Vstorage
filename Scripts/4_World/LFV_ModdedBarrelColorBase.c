// =========================================================
// LF_VStorage -- Modded Barrel_ColorBase (Sprint 3 + PERSIST v2)
//
// Applies to ALL Barrel_ColorBase (vanilla whitelist + LFV).
// Holds m_LFV_CachedManifest for client RPC display, and the
// PERSIST v2 OnStoreSave/OnStoreLoad override for vanilla
// Barrel_Blue/Green/Red/Yellow.
//
// Why this level (not modded ItemBase): vanilla Barrel_ColorBase
// has its own OnStoreSave that writes `opened` (bool) after super.
// Inserting our bytes at the ItemBase level would place them BEFORE
// vanilla's `opened` byte; a legacy save (no PERSIST block) would
// have our ctx.Read consume the `opened` byte and misalign the
// cursor. At Barrel_ColorBase level, our bytes land AFTER `opened`
// at the true tail of the vanilla barrel record -- legacy reads
// hit clean EOF with no downstream vanilla code to desync.
//
// LFV_Barrel_Base writes its own PERSIST block further down, so
// this class early-returns for IsKindOf("LFV_Barrel_Base").
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
    // EEInit -- register vanilla barrels with module after
    // persistence loads (server). LFV_Barrel_Base has its own
    // EEInit so we early-return for it.
    //
    // Migration (PERSIST v1 -> v2): legacy vanilla barrel saves
    // have no StorageId on the entity. Fall back to IdMap lookup
    // by PersistentID; first natural engine save writes the
    // StorageId into storage_1.db.
    // -----------------------------------------------------------
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        if (!GetGame().IsDedicatedServer())
            return;

        if (this.IsKindOf("LFV_Barrel_Base"))
            return;

        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return;

        if (m_LFV_StorageId == "")
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                string pidKey = LFV_IdMap.GetKeyFromEntity(this);
                if (pidKey != "")
                {
                    string legacySid = LFV_IdMap.Lookup(module.GetPersistentIdMap(), pidKey);
                    if (legacySid != "")
                    {
                        m_LFV_StorageId = legacySid;
                        string migMsg = "EEInit: migrated vanilla barrel StorageId from IdMap for ";
                        migMsg = migMsg + this.GetType();
                        LFV_Log.Info(migMsg);
                    }
                }
            }
        }

        LFV_Module srvModule = LFV_Module.GetModule();
        if (srvModule)
            srvModule.RegisterContainerFromPersistence(this, m_LFV_StorageId);
        #endif
    }

    // -----------------------------------------------------------
    // OnStoreSave -- append PERSIST + StorageId AFTER vanilla
    // Barrel_ColorBase writes `opened`. LFV_Barrel_Base early-returns
    // and writes its own block further down the chain.
    // -----------------------------------------------------------
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        if (this.IsKindOf("LFV_Barrel_Base"))
            return;

        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return;

        ctx.Write(LFV_Version.PERSIST);
        ctx.Write(m_LFV_StorageId);
    }

    // -----------------------------------------------------------
    // OnStoreLoad -- restore StorageId. Legacy vanilla barrel
    // saves have no PERSIST block here (only vanilla `opened`).
    // ctx.Read(persistVer) at EOF returns false cleanly (per-entity
    // record EOF is respected by ParamsReadContext, matching the
    // pattern used by DayZ Expansion and TraderPlus for schema
    // evolution without engine version bumps).
    // -----------------------------------------------------------
    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        if (this.IsKindOf("LFV_Barrel_Base"))
            return true;

        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return true;

        int persistVer;
        if (!ctx.Read(persistVer))
            return true;

        if (!ctx.Read(m_LFV_StorageId))
            return false;

        return true;
    }

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
    // Drop virtual items when vanilla whitelisted barrel
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
    // Untrack when entity is deleted from world.
    // Phase 1: route through OnEntityDestroyed (central purge).
    // -----------------------------------------------------------
    override void EEDelete(EntityAI parent)
    {
        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module)
            module.OnEntityDestroyed(this);
        #endif

        super.EEDelete(parent);
    }

    // -----------------------------------------------------------
    // Block putting barrel in cargo if it has virtual items.
    // Server-authoritative: the client's HasVirtualItems is a stub
    // that always returns false, so the check only has meaning here.
    // Wrapping in #ifdef SERVER makes the intent explicit.
    // -----------------------------------------------------------
    override bool CanPutInCargo(EntityAI parent)
    {
        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module && module.HasVirtualItems(this))
            return false;
        #endif

        return super.CanPutInCargo(parent);
    }

    // -----------------------------------------------------------
    // Block putting barrel in hands if it has virtual items
    // -----------------------------------------------------------
    override bool CanPutIntoHands(EntityAI parent)
    {
        #ifdef SERVER
        LFV_Module module = LFV_Module.GetModule();
        if (module && module.HasVirtualItems(this))
            return false;
        #endif

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
