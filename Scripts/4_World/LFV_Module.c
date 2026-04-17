// =========================================================
// LF_VStorage -- Module (v1.2)
//
// CF_ModuleWorld -- the brain of the mod.
//
// RPC: vanilla RPCSingleParam pattern (like BaseBuildingPlus).
//   Server sends in MissionServer.InvokeOnConnect
//   Client receives in PlayerBase.OnRPC
//   NO dependency on GetRPCManager or CF RPC system.
//
// Registry: initialized with defaults in constructor so
// ActionCondition works immediately on client. Server
// re-inits in OnMissionStart with loaded settings.
// =========================================================

[CF_RegisterModule(LFV_Module)]
class LFV_Module : CF_ModuleWorld
{
    // --- Singleton ---
    protected static LFV_Module sm_Instance;
    static LFV_Module GetModule() { return sm_Instance; }

    // --- Settings ---
    protected ref LFV_Settings m_Settings;

    // --- Container tracking ---
    protected ref map<ItemBase, ref LFV_ContainerState> m_ContainerStates;

    // --- ID Map (vanilla containers) ---
    protected ref map<string, string> m_PersistentIdToStorageId;

    // --- Queue management ---
    protected ref Timer m_QueueTimer;
    protected ref array<ref LFV_Queue> m_ActiveQueues;
    protected ref array<ref LFV_Queue> m_PendingQueues;
    protected ref set<ItemBase> m_QueuedContainers;
    protected bool m_IsShuttingDown;

    // --- Auto-close timer ---
    protected ref Timer m_AutoCloseTimer;
    protected ref array<Man> m_AutoClosePlayers;
    protected ref array<ItemBase> m_AutoCloseToClose;

    // --- IdMap periodic save ---
    protected ref Timer m_IdMapSaveTimer;
    protected int m_IdMapDirtyCount;

    // --- Periodic scan ---
    protected ref Timer m_PeriodicScanTimer;

    // --- Startup gate: hooks early-return until OnMissionLoaded fires ---
    protected bool m_StartupComplete;

    // -----------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------
    void LFV_Module()
    {
        sm_Instance = this;
        m_Settings = new LFV_Settings();
        m_ContainerStates = new map<ItemBase, ref LFV_ContainerState>();
        m_PersistentIdToStorageId = new map<string, string>();
        m_ActiveQueues = new array<ref LFV_Queue>();
        m_PendingQueues = new array<ref LFV_Queue>();
        m_QueuedContainers = new set<ItemBase>();
        m_IsShuttingDown = false;
        m_AutoClosePlayers = new array<Man>();
        m_AutoCloseToClose = new array<ItemBase>();
        m_IdMapDirtyCount = 0;
        m_StartupComplete = false;

        // Init Registry with defaults immediately so ActionCondition
        // works on client without waiting for server RPC.
        // Server re-inits in OnMissionStart with loaded settings.
        LFV_Registry.Init(m_Settings);
    }

    // -----------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------
    LFV_Settings GetSettings() { return m_Settings; }
    map<string, string> GetPersistentIdMap() { return m_PersistentIdToStorageId; }
    int GetTrackedCount() { return m_ContainerStates.Count(); }
    ItemBase GetTrackedContainerAt(int index) { return m_ContainerStates.GetKey(index); }
    bool IsStartupComplete() { return m_StartupComplete; }
    void SetStartupComplete(bool v) { m_StartupComplete = v; }

    LFV_ContainerState GetContainerState(ItemBase container)
    {
        if (m_ContainerStates.Contains(container))
            return m_ContainerStates.Get(container);
        return null;
    }

    // -----------------------------------------------------------
    // CF Lifecycle -- OnInit
    // -----------------------------------------------------------
    override void OnInit()
    {
        super.OnInit();
    }

    // -----------------------------------------------------------
    // Apply settings received from server via RPC
    // Called from LFV_ModdedPlayerBase.OnRPC
    // -----------------------------------------------------------
    void ApplyServerSettings(LFV_Settings serverSettings)
    {
        if (!serverSettings)
            return;

        m_Settings = serverSettings;
        LFV_Registry.Init(m_Settings);
        Print("[LFV] Server settings applied -- registry re-initialized");
    }

    // -----------------------------------------------------------
    // Client: send admin command to server via RPC
    // -----------------------------------------------------------
    static void SendAdminCommand(PlayerBase player, string command)
    {
        if (!player) return;
        if (command == "") return;
        auto param = new Param1<string>(command);
        GetGame().RPCSingleParam(player, LFV_RPC.ADMIN_COMMAND, param, true, player.GetIdentity());
    }

    // -----------------------------------------------------------
    // Client stubs -- safe no-op defaults so client compiles.
    // Server gets real implementations via modded class (#ifdef SERVER).
    // -----------------------------------------------------------
    bool HasVirtualItems(ItemBase container) { return false; }
    bool HasQueueForContainer(ItemBase container) { return false; }
    bool CheckRateLimit(PlayerBase player, ItemBase container) { return true; }
    ItemBase FindContainerByStorageId(string storageId, string containerClass, vector position) { return null; }
    LFV_ContainerFile BuildContainerFile(ItemBase container, LFV_ContainerState state) { return null; }
    LFV_ContainerFile PrepareVirtualization(ItemBase container, LFV_ContainerState state, bool doBackupRotation) { return null; }
    void PurgeBlacklisted(LFV_ContainerFile data) {}
    void SaveAdminJson(LFV_ContainerFile data) {}
    void RegisterInIdMap(ItemBase container, string storageId) {}
    void OnOpenContainer(ItemBase container, PlayerBase player) {}
    void OnCloseContainer(ItemBase container, PlayerBase player) {}
    void OnCloseContainer(ItemBase container) {}
    bool IsTracked(ItemBase container) { return false; }
    void RequestRestore(ItemBase container) {}
    void RequestVirtualize(ItemBase container) {}
    void HandleAdminCommandFromRPC(string command, PlayerIdentity sender) {}
    void UntrackContainer(ItemBase container) {}
    void OnEntityDestroyed(EntityAI entity) {}
}
