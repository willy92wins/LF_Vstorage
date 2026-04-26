// =========================================================
// LF_VStorage -- Modded ItemBase (Fix A + PERSIST v2 fallback)
//
// Generic hooks for custom mod containers that extend ItemBase
// without going through Barrel_ColorBase or TentBase (e.g.
// RaG_ContainerBase, A6_Openable_Base, MMG containers).
//
// Scope guard (CRITICAL):
//   Every gate below uses IsExplicitVirtualContainer, NOT the
//   default-true IsVirtualContainer. This override runs on EVERY
//   ItemBase-derived entity in the game -- weapons, magazines,
//   codelocks, BBP structures, every modded item. If we used the
//   default-true path, we would (a) register every entity in
//   m_ContainerStates and (b) inject PERSIST bytes between vanilla
//   ItemBase and the child-class OnStoreSave (CombinationLock
//   m_Combination, Magazine state, BBP m_IsHologram, etc.), which
//   misaligns every subsequent read and triggers the codelock
//   delete-on-load / magazine-falls-to-ground / BBP-hologram
//   issues users reported. Explicit whitelist only.
//
// Persistence placement:
//   - Barrel_ColorBase family (vanilla Barrel_Blue/Green/Red/Yellow
//     + LFV_Barrel_Base) -> handled by LFV_ModdedBarrelColorBase
//     at Barrel_ColorBase level (after vanilla `opened` byte).
//   - TentBase family (MediumTent/LargeTent/CarTent) -> handled
//     by LFV_ModdedTentBase at TentBase level (after vanilla
//     pitched-state bytes).
//   - Everything else (SeaChest, WoodenCrate, custom mod containers)
//     -> handled here. Assumes no downstream vanilla OnStoreSave
//     between ItemBase and the leaf class. If a custom whitelisted
//     class DOES have its own vanilla OnStoreSave, the modder should
//     add a per-class override file for it.
//
// Hooks:
//   OnRPC        -- client manifest cache from server RPC
//   EEInit       -- register container with module after persistence load (server)
//   OnStoreSave  -- persist m_LFV_StorageId via engine (server, PERSIST v2)
//   OnStoreLoad  -- restore m_LFV_StorageId from engine (server, PERSIST v2)
//   EEDelete     -- untrack container on world deletion
//   CanPutInCargo/Hands -- block move if virtual items exist
// =========================================================

modded class ItemBase
{
    // --- Client-side manifest cache for action text display ---
    protected string m_LFV_CachedManifest;

    // --- Server-side StorageId (PERSIST v2, persisted via OnStoreSave) ---
    protected string m_LFV_StorageId = "";

    string LFV_GetCachedManifest() { return m_LFV_CachedManifest; }
    string LFV_GetStorageId() { return m_LFV_StorageId; }

    void LFV_SetStorageId(string sid)
    {
        m_LFV_StorageId = sid;
    }

    // -----------------------------------------------------------
    // EEInit -- register with module after persistence loads (server).
    // Skips Barrel_ColorBase (handled by LFV_ModdedBarrelColorBase)
    // and TentBase (handled by LFV_ModdedTentBase). Covers custom
    // whitelisted containers like RaG/A6/MMG items.
    //
    // Migration (PERSIST v1 -> v2): legacy saves have no StorageId on
    // the entity, so m_LFV_StorageId is "". Fall back to IdMap lookup
    // by PersistentID; first natural engine save writes it to
    // storage_1.db and migration is complete for that container.
    // -----------------------------------------------------------
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        if (!GetGame().IsDedicatedServer())
            return;

        // O(1) whitelist check first. For the 99% of ItemBase-derived
        // entities that are not containers (weapons, mags, clothing,
        // BBP pieces, etc.) this is the only work EEInit does for us.
        // The IsKindOf checks below only run for entities that passed
        // the whitelist and need to be routed to the per-family hook.
        if (!LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
            return;

        if (this.IsKindOf("Barrel_ColorBase"))
            return;
        if (this.IsKindOf("TentBase"))
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
                        string migMsg = "EEInit: migrated StorageId from IdMap for ";
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
    // OnStoreSave -- persist StorageId (PERSIST v2). Server only.
    //
    // Early-return conditions MUST exactly mirror OnStoreLoad so the
    // byte stream is symmetric. Non-virtual items and handled-elsewhere
    // classes (Barrel_ColorBase family, TentBase family) skip the write
    // -- their entity record stays byte-identical to vanilla at this
    // inheritance level.
    // -----------------------------------------------------------
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        // O(1) whitelist check first -- skips 99% of persistent entities
        // (weapons, mags, clothing, BBP...) with a single map lookup,
        // avoiding two IsKindOf calls per entity per save cycle.
        if (!LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
            return;

        if (this.IsKindOf("Barrel_ColorBase"))
            return;
        if (this.IsKindOf("TentBase"))
            return;

        ctx.Write(LFV_Version.PERSIST);
        ctx.Write(m_LFV_StorageId);
    }

    // -----------------------------------------------------------
    // OnStoreLoad -- restore StorageId (PERSIST v2). Server only.
    //
    // Legacy saves (PERSIST v1) have no block here. ctx.Read returning
    // false on the version int means "no bytes written by our code" --
    // we return true so the engine treats the entity as loaded OK and
    // EEInit will migrate via IdMap fallback.
    // Relies on DayZ's per-entity record EOF being clean (ctx.Read
    // returns false without advancing the cursor into the next record).
    // Assumption empirically safe for classes without downstream vanilla
    // OnStoreSave overrides (SeaChest, WoodenCrate, and typical custom
    // mod containers).
    // -----------------------------------------------------------
    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        // O(1) whitelist check first -- symmetric with OnStoreSave.
        // 99% of entities take the single map-lookup path and exit.
        if (!LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
            return true;

        if (this.IsKindOf("Barrel_ColorBase"))
            return true;
        if (this.IsKindOf("TentBase"))
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
    // Only processes MANIFEST_UPDATE for non-barrel containers
    // -----------------------------------------------------------
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        super.OnRPC(sender, rpc_type, ctx);

        // O(1) registry check first -- skips 99% of items immediately
        if (!LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
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
        if (LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
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
    // other mods that might allow cargo placement).
    // Server-authoritative: HasVirtualItems on the client is a
    // stub that returns false, so the whole inner check only has
    // meaning on the server.
    // -----------------------------------------------------------
    override bool CanPutInCargo(EntityAI parent)
    {
        #ifdef SERVER
        // O(1) registry check first -- skips non-virtual items immediately
        if (LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
        {
            // Skip barrels -- handled by LFV_ModdedBarrelColorBase
            if (!this.IsKindOf("Barrel_ColorBase"))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module && module.HasVirtualItems(this))
                    return false;
            }
        }
        #endif

        return super.CanPutInCargo(parent);
    }

    // -----------------------------------------------------------
    // Block putting container in hands if it has virtual items
    // -----------------------------------------------------------
    override bool CanPutIntoHands(EntityAI parent)
    {
        #ifdef SERVER
        // O(1) registry check first -- skips non-virtual items immediately
        if (LFV_Registry.IsExplicitVirtualContainer(this.GetType()))
        {
            // Skip barrels -- handled by LFV_ModdedBarrelColorBase
            if (!this.IsKindOf("Barrel_ColorBase"))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module && module.HasVirtualItems(this))
                    return false;
            }
        }
        #endif

        return super.CanPutIntoHands(parent);
    }
}
