// =========================================================
// LF_VStorage -- Settings (v1.0)
//
// JSON-based settings loaded from $profile:LFVStorage/settings.json.
// Server loads on MissionStart, syncs to clients via RPC.
// If file missing, creates default. 3_Game layer for shared access.
//
// RULES:
// - All field writes inside #ifdef SERVER
// - Never add new fields without default values
// - RPC sync uses ScriptRPC (CF module handles send/receive)
// =========================================================

class LFV_Settings
{
    // --- Container registration ---
    // m_NeverVirtualize is PRIMARY filter (blacklist-first).
    // m_VirtualContainers is informational in normal mode; becomes
    // authoritative whitelist only when m_StrictMode=true.
    // m_ActionTriggeredContainers kept for legacy state flag population;
    // no longer used for trigger routing after Phase 1 refactor.
    ref array<string> m_VirtualContainers;
    ref array<string> m_ActionTriggeredContainers;
    ref array<string> m_NeverVirtualize;
    ref array<string> m_ItemBlacklist;

    // Strict/paranoid mode: when true, only m_VirtualContainers are
    // ever considered virtual. Default false = blacklist-primary.
    bool m_StrictMode;

    // External virtualization mod detected (Expansion / VSM).
    // When set by OnMissionStart detector, hooks early-return.
    // Override via m_ForceEnableDespiteVS=true if admin knows risks.
    bool m_ForceEnableDespiteVS;

    // --- Virtualization behavior ---
    bool m_VirtualizeDecayItems;

    // --- Queue / batching ---
    int m_BatchSize;
    int m_BatchInterval;
    int m_MaxConcurrentQueues;

    // --- Item limits ---
    int m_MaxItemsPerContainer;

    // --- Auto-close (barrels only) ---
    bool m_AutoCloseEnabled;
    int m_AutoCloseShortDelay;
    int m_AutoCloseLongDelay;
    float m_AutoCloseRadius;

    // --- Backups ---
    int m_BackupRotations;

    // --- Manifest ---
    bool m_ManifestEnabled;
    int m_ManifestMaxItems;

    // --- Admin ---
    bool m_AdminSidecarJson;

    // --- Rate limiting ---
    int m_RateLimitCooldown;

    // --- Cleanup ---
    bool m_CleanupDryRun;

    // --- Queue limits (M2 fix) ---
    int m_MaxPendingQueues;

    // --- Shutdown (BUD1) ---
    int m_ShutdownBudgetMs;

    // --- Periodic scan ---
    int m_PeriodicScanInterval;

    // --- Lifecycle delays on boot (server-only; not synced to clients) ---
    int m_ScanInitialDelayMs;
    int m_CleanupInitialDelayMs;

    // --- Logging ---
    string m_LogLevel;

    // --- Admin (A1 audit) ---
    ref array<string> m_AdminUIDs;

    void LFV_Settings()
    {
        SetDefaults();
    }

    void SetDefaults()
    {
        m_VirtualContainers = new array<string>();
        string vc1 = "Barrel_Blue";
        m_VirtualContainers.Insert(vc1);
        string vc2 = "Barrel_Green";
        m_VirtualContainers.Insert(vc2);
        string vc3 = "Barrel_Red";
        m_VirtualContainers.Insert(vc3);
        string vc4 = "Barrel_Yellow";
        m_VirtualContainers.Insert(vc4);
        string vc5 = "SeaChest";
        m_VirtualContainers.Insert(vc5);
        string vc6 = "WoodenCrate";
        m_VirtualContainers.Insert(vc6);
        string vc7 = "MediumTent";
        m_VirtualContainers.Insert(vc7);
        string vc8 = "LargeTent";
        m_VirtualContainers.Insert(vc8);
        string vc9 = "CarTent";
        m_VirtualContainers.Insert(vc9);
        string vc10 = "LFV_Barrel_Standard";
        m_VirtualContainers.Insert(vc10);
        string vc11 = "LFV_Barrel_Large";
        m_VirtualContainers.Insert(vc11);

        m_ActionTriggeredContainers = new array<string>();

        m_NeverVirtualize = new array<string>();
        string nv1 = "PlayerBase";
        m_NeverVirtualize.Insert(nv1);
        string nv2 = "CarScript";
        m_NeverVirtualize.Insert(nv2);
        string nv3 = "BaseBuildingBase";
        m_NeverVirtualize.Insert(nv3);
        string nv4 = "GardenBase";
        m_NeverVirtualize.Insert(nv4);
        string nv5 = "FireplaceBase";
        m_NeverVirtualize.Insert(nv5);

        m_ItemBlacklist = new array<string>();
        string bl1 = "WrittenNote";
        m_ItemBlacklist.Insert(bl1);

        m_StrictMode = false;
        m_ForceEnableDespiteVS = false;
        m_VirtualizeDecayItems = false;
        m_BatchSize = 50;
        m_BatchInterval = 100;
        m_MaxConcurrentQueues = 5;

        m_AutoCloseEnabled = true;
        m_AutoCloseShortDelay = 60;
        m_AutoCloseLongDelay = 300;
        m_AutoCloseRadius = 10.0;

        m_MaxItemsPerContainer = 0;
        m_CleanupDryRun = true;
        m_MaxPendingQueues = 50;
        m_ShutdownBudgetMs = 45000;
        m_PeriodicScanInterval = 1800;
        m_ScanInitialDelayMs = 10000;
        m_CleanupInitialDelayMs = 120000;
        m_BackupRotations = 2;
        m_ManifestEnabled = true;
        m_ManifestMaxItems = 8;
        m_AdminSidecarJson = true;
        m_RateLimitCooldown = 3;
        m_LogLevel = "ERROR";

        m_AdminUIDs = new array<string>();
    }

    // -----------------------------------------------------------
    // Guarantee every array field is non-null after a load.
    // ReadFromString merges present keys into the existing instance
    // but explicit "key": null in the JSON can nullify an array that
    // the constructor had initialized. Re-init defensively before any
    // code iterates or Save() re-serializes the object.
    // -----------------------------------------------------------
    protected void EnsureArraysNotNull()
    {
        if (!m_VirtualContainers)
            m_VirtualContainers = new array<string>();

        if (!m_ActionTriggeredContainers)
            m_ActionTriggeredContainers = new array<string>();

        if (!m_NeverVirtualize)
            m_NeverVirtualize = new array<string>();

        if (!m_ItemBlacklist)
            m_ItemBlacklist = new array<string>();

        if (!m_AdminUIDs)
            m_AdminUIDs = new array<string>();
    }

    // -----------------------------------------------------------
    // Clamp numeric fields into the operational range LFV assumes.
    // Out-of-range values from a hand-edited settings.json would
    // otherwise lock timers, starve queues, or underflow delays.
    // -----------------------------------------------------------
    protected void ClampNumericBounds()
    {
        if (m_BatchSize < 1)
            m_BatchSize = 1;

        if (m_BatchInterval < 50)
            m_BatchInterval = 50;

        if (m_MaxConcurrentQueues < 1)
            m_MaxConcurrentQueues = 1;

        if (m_BackupRotations < 0)
            m_BackupRotations = 0;

        if (m_RateLimitCooldown < 0)
            m_RateLimitCooldown = 0;

        if (m_ManifestMaxItems < 1)
            m_ManifestMaxItems = 1;

        if (m_MaxItemsPerContainer < 0)
            m_MaxItemsPerContainer = 0;

        if (m_MaxPendingQueues < 5)
            m_MaxPendingQueues = 5;

        if (m_ShutdownBudgetMs < 10000)
            m_ShutdownBudgetMs = 10000;

        if (m_PeriodicScanInterval < 300)
            m_PeriodicScanInterval = 300;

        if (m_ScanInitialDelayMs < 1000)
            m_ScanInitialDelayMs = 1000;

        if (m_CleanupInitialDelayMs < 1000)
            m_CleanupInitialDelayMs = 1000;
    }

    // -----------------------------------------------------------
    // Load from JSON -- server only.
    //
    // Custom loader (OpenFile + ReadFile + JsonSerializer) replaces
    // the vanilla JsonFileLoader<T> path because JsonFileLoader
    // silently accepts malformed JSON and leaves the caller with
    // defaults, no error, no log line. That made mis-edited
    // settings.json files undiagnosable in production -- the server
    // would boot with silent defaults and admins would not notice
    // until a specific setting (logLevel, AdminUIDs, etc.) was
    // observed to be wrong hours later.
    //
    // JsonSerializer.ReadFromString returns the parse error from
    // the underlying Enforce parser, so a bad edit now surfaces
    // immediately with an actionable log line.
    //
    // On any failure (missing, unreadable, empty, malformed) we
    // keep the defaults that SetDefaults() established in the
    // constructor and return false. Callers currently ignore the
    // return value; existing behavior is preserved for them.
    // -----------------------------------------------------------
    bool Load()
    {
        if (!FileExist(LFV_Paths.SETTINGS_FILE))
        {
            string noFileMsg = "No settings file found, creating default";
            LFV_Log.Info(noFileMsg);
            Save();
            return true;
        }

        FileHandle file = OpenFile(LFV_Paths.SETTINGS_FILE, FileMode.READ);
        if (!file)
        {
            string openErr = "Cannot open settings.json for read -- keeping defaults: ";
            openErr = openErr + LFV_Paths.SETTINGS_FILE;
            LFV_Log.Error(openErr);
            return false;
        }

        // 1 MB ceiling. LFV settings.json is typically <8 KB; the large
        // cap gives room for sizable m_VirtualContainers / m_AdminUIDs
        // lists without needing a chunked reader. If we ever hit this,
        // the JSON parser would have failed far earlier from other
        // issues (admin list of 100k+ UIDs is not realistic).
        string content;
        ReadFile(file, content, 1048576);
        CloseFile(file);

        if (content == "")
        {
            string emptyMsg = "settings.json is empty -- keeping defaults";
            LFV_Log.Warn(emptyMsg);
            return false;
        }

        JsonSerializer serializer = new JsonSerializer();
        string parseErr;
        if (!serializer.ReadFromString(this, content, parseErr))
        {
            string detailedErr = "settings.json parse error -- keeping defaults: ";
            detailedErr = detailedErr + parseErr;
            LFV_Log.Error(detailedErr);
            return false;
        }

        EnsureArraysNotNull();
        ClampNumericBounds();

        string loadMsg = "Settings loaded -- ";
        loadMsg = loadMsg + m_VirtualContainers.Count().ToString();
        loadMsg = loadMsg + " virtual containers configured";
        LFV_Log.Info(loadMsg);
        return true;
    }

    // -----------------------------------------------------------
    // Save to JSON -- server only. Pretty-printed so admins can
    // hand-edit. WriteToString is infallible in Enforce Script;
    // the only failure surface is the file handle open.
    // -----------------------------------------------------------
    void Save()
    {
        JsonSerializer serializer = new JsonSerializer();
        string json;
        serializer.WriteToString(this, true, json);

        FileHandle file = OpenFile(LFV_Paths.SETTINGS_FILE, FileMode.WRITE);
        if (!file)
        {
            string openErr = "Cannot open settings.json for write: ";
            openErr = openErr + LFV_Paths.SETTINGS_FILE;
            LFV_Log.Error(openErr);
            return;
        }

        FPrint(file, json);
        CloseFile(file);
    }

    // RPC: serialized automatically via Param1<LFV_Settings> in
    // MissionServer.InvokeOnConnect (vanilla RPCSingleParam pattern).
}
