// =========================================================
// LF_VStorage -- Modded TentBase (PERSIST v2)
//
// Covers MediumTent / LargeTent / CarTent (all descend from TentBase).
//
// Why a separate modded class at the TentBase level: vanilla
// TentBase overrides OnStoreSave/OnStoreLoad to persist pitched
// state (m_State, colors, attachment data). If we inserted our
// PERSIST bytes at modded ItemBase level, they would land BETWEEN
// vanilla ItemBase's bytes and vanilla TentBase's bytes. On a
// legacy save (no PERSIST block) our OnStoreLoad read would
// misalign into TentBase's pitched-state bytes.
//
// By placing the override on TentBase (leaf-level for whitelisted
// tent variants), vanilla super.OnStoreSave is called first, then
// our bytes land at the true end of the entity record. Legacy
// loads see ctx.Read return false on EOF cleanly.
//
// m_LFV_StorageId and its accessors are inherited from modded
// class ItemBase. We add only the save/load/register hooks here.
// =========================================================

modded class TentBase
{
    // -----------------------------------------------------------
    // EEInit -- register with module after persistence loads (server).
    // Mirrors modded ItemBase.EEInit but runs here for tents.
    // modded ItemBase.EEInit early-returns on IsKindOf("TentBase"),
    // so registration happens exactly once.
    // -----------------------------------------------------------
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        if (!GetGame().IsDedicatedServer())
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
                        string migMsg = "EEInit: migrated tent StorageId from IdMap for ";
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
    // OnStoreSave -- persist StorageId AFTER vanilla TentBase bytes.
    // Non-whitelisted tents write zero bytes -- stream byte-identical
    // to vanilla.
    // -----------------------------------------------------------
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);

        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return;

        ctx.Write(LFV_Version.PERSIST);
        ctx.Write(m_LFV_StorageId);
    }

    // -----------------------------------------------------------
    // OnStoreLoad -- restore StorageId. Legacy saves lack the PERSIST
    // block; ctx.Read(persistVer) returns false on clean per-entity
    // EOF and we return true so the tent loads with pitched state
    // intact. EEInit then migrates via IdMap lookup.
    // -----------------------------------------------------------
    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        if (!LFV_Registry.IsVirtualContainer(this.GetType()))
            return true;

        int persistVer;
        if (!ctx.Read(persistVer))
            return true;

        if (!ctx.Read(m_LFV_StorageId))
            return false;

        return true;
    }
}
