// =========================================================
// LF_VStorage -- Barrel Base (v1.0)
//
// Our barrels with open/close trigger for virtualization.
// Inherits Barrel_ColorBase (so engine treats them as barrels).
//
// SyncVars:
//   m_LFV_HasItems (bool) -- synced to client
//   m_LFV_ItemCount (int) -- synced to client
//   m_LFV_Manifest (string) -- NOT synced (string SyncVar doesn't exist)
//
// RegisterNetSyncVariable* called in constructor (not EEInit).
// All SyncVar writes inside #ifdef SERVER + SetSynchDirty().
// =========================================================

class LFV_Barrel_Base : Barrel_ColorBase
{
    // --- SyncVars (bool + int synced to client) ---
    protected bool   m_LFV_HasItems;
    protected int    m_LFV_ItemCount;

    // --- Server-only data (not synced via SyncVar) ---
    // m_LFV_StorageId inherited from modded class ItemBase (PERSIST v2)
    protected string m_LFV_Manifest;
    protected int    m_LFV_PersistVersion;
    protected string m_LFV_OwnerUID;     // reserved for v2

    // --- C3 audit flag: prevents double OnCloseContainer ---
    bool m_LFV_SkipCloseVirtualize;

    // --- Synced to client: blocks actions during virtualize/restore ---
    protected bool m_LFV_IsProcessing;

    // -----------------------------------------------------------
    // Constructor -- register SyncVars here (NOT in EEInit)
    // -----------------------------------------------------------
    void LFV_Barrel_Base()
    {
        m_LFV_HasItems = false;
        m_LFV_ItemCount = 0;
        m_LFV_StorageId = "";
        m_LFV_Manifest = "";
        m_LFV_PersistVersion = LFV_Version.PERSIST;
        m_LFV_OwnerUID = "";
        m_LFV_SkipCloseVirtualize = false;
        m_LFV_IsProcessing = false;

        string varHasItems = "m_LFV_HasItems";
        RegisterNetSyncVariableBool(varHasItems);

        string varIsProcessing = "m_LFV_IsProcessing";
        RegisterNetSyncVariableBool(varIsProcessing);

        string varItemCount = "m_LFV_ItemCount";
        RegisterNetSyncVariableInt(varItemCount, 0, 9999);
    }

    // -----------------------------------------------------------
    // EEInit -- register with module after persistence loads
    // -----------------------------------------------------------
    override void EEInit()
    {
        super.EEInit();

        #ifdef SERVER
        if (GetGame().IsDedicatedServer())
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                module.RegisterBarrelFromPersistence(this);
            }
        }
        #endif
    }

    // -----------------------------------------------------------
    // Persistence -- OnStoreSave
    // -----------------------------------------------------------
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);
        ctx.Write(LFV_Version.PERSIST);
        ctx.Write(m_LFV_StorageId);
        ctx.Write(m_LFV_HasItems);
        ctx.Write(m_LFV_Manifest);
        ctx.Write(m_LFV_OwnerUID);
        ctx.Write(m_LFV_ItemCount);
    }

    // -----------------------------------------------------------
    // Persistence -- OnStoreLoad
    // -----------------------------------------------------------
    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;

        int persistVersion;
        if (!ctx.Read(persistVersion)) return false;

        if (!ctx.Read(m_LFV_StorageId)) return false;
        if (!ctx.Read(m_LFV_HasItems)) return false;
        if (!ctx.Read(m_LFV_Manifest)) return false;

        if (persistVersion >= 1)
        {
            if (!ctx.Read(m_LFV_OwnerUID)) return false;
            if (!ctx.Read(m_LFV_ItemCount)) return false;
        }

        m_LFV_PersistVersion = persistVersion;
        return true;
    }

    // -----------------------------------------------------------
    // Close override -- virtualize on close if has items
    //
    // Edge case 14 (OnWasDetached): Vanilla Barrel_ColorBase
    // calls Close() inside OnWasDetached when the last attachment
    // is removed. This is safe because:
    //   1. OnWasDetached -> Close() -> super.Close() (vanilla)
    //   2. This override fires, checks HasCargoOrAttachments
    //   3. If remaining items exist, virtualizes them
    //   4. If no items remain (the detached item was the last),
    //      HasCargoOrAttachments returns false -> no-op
    // No OnWasDetached override needed in LFV_Barrel_Base.
    // -----------------------------------------------------------
    override void Close()
    {
        super.Close();

        #ifdef SERVER
        if (GetGame().IsDedicatedServer())
        {
            // if action already called OnCloseContainer,
            // skip here to prevent double-call
            if (m_LFV_SkipCloseVirtualize)
            {
                m_LFV_SkipCloseVirtualize = false;
                return;
            }

            // Auto-close path: no action involved, we handle it
            if (LFV_StateMachine.HasCargoOrAttachments(this))
            {
                LFV_Module module = LFV_Module.GetModule();
                if (module)
                    module.OnCloseContainer(this);
            }
        }
        #endif
    }

    // -----------------------------------------------------------
    // EEKilled -- barrel destroyed while has virtual items (edge case 8)
    // Triggers OnContainerDestroyed -> DropQueue
    // -----------------------------------------------------------
    override void EEKilled(Object killer)
    {
        super.EEKilled(killer);

        #ifdef SERVER
        if (GetGame().IsDedicatedServer())
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
                module.OnContainerDestroyed(this);
        }
        #endif
    }

    // -----------------------------------------------------------
    // Accessors (for module)
    // LFV_GetStorageId / LFV_SetStorageId inherited from modded ItemBase.
    // -----------------------------------------------------------
    bool LFV_GetHasItems() { return m_LFV_HasItems; }
    int LFV_GetItemCount() { return m_LFV_ItemCount; }
    string LFV_GetManifest() { return m_LFV_Manifest; }

    void LFV_SetHasItems(bool val)
    {
        #ifdef SERVER
        m_LFV_HasItems = val;
        SetSynchDirty();
        #endif
    }

    void LFV_SetItemCount(int count)
    {
        #ifdef SERVER
        m_LFV_ItemCount = count;
        SetSynchDirty();
        #endif
    }

    void LFV_SetManifest(string manifest)
    {
        m_LFV_Manifest = manifest;
        // NOT synced via SyncVar -- string SyncVar doesn't exist
    }

    bool LFV_GetIsProcessing() { return m_LFV_IsProcessing; }

    void LFV_SetIsProcessing(bool val)
    {
        #ifdef SERVER
        m_LFV_IsProcessing = val;
        SetSynchDirty();
        #endif
    }
}

// -----------------------------------------------------------
// Large barrel variant (1000 cargo slots)
// -----------------------------------------------------------
class LFV_Barrel_Base_1000 : LFV_Barrel_Base
{
    // Config.cpp sets inventorySize to 1000
}

// -----------------------------------------------------------
// Concrete variants with scope=2
// -----------------------------------------------------------
class LFV_Barrel_Standard : LFV_Barrel_Base {}
class LFV_Barrel_Large : LFV_Barrel_Base_1000 {}
