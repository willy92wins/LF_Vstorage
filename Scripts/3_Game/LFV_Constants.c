// =========================================================
// LF_VStorage -- Constants (v1.0)
//
// All static constants used across the mod. 3_Game layer
// so both server and client can reference them.
//
// RULES:
// - NEVER change existing values (breaks format migration)
// - Add new values at the end of relevant sections
// - Increment FORMAT only when adding fields to .lfv
// =========================================================

class LFV_Version
{
    static const int FORMAT = 3;         // .lfv binary format version (v3: removed StoreCtx, added Agents/Combination/Cleanness)
    static const int PERSIST = 1;        // OnStoreSave/OnStoreLoad version for LFV_Barrel_Base
    static const string MOD_VERSION = "1.1.0";
}

// Recursion safety
class LFV_Limits
{
    static const int MAX_ITEM_DEPTH = 5; // Max nesting depth for item tree recursion
}

class LFV_Magic
{
    static const int LFV_FILE = 0x4C4656;    // "LFV" in hex -- magic number for .lfv files
}

// State machine states
class LFV_State
{
    static const int IDLE         = 0;   // No virtual file, items live in world
    static const int VIRTUALIZING = 1;   // Writing .lfv, items still in world
    static const int VIRTUALIZED  = 2;   // .lfv written, items deleted from world
    static const int RESTORING    = 3;   // Reading .lfv, spawning items
    static const int RESTORED     = 4;   // Items spawned, .lfv pending delete

    static string ToString(int state)
    {
        switch (state)
        {
            case IDLE:         return "IDLE";
            case VIRTUALIZING: return "VIRTUALIZING";
            case VIRTUALIZED:  return "VIRTUALIZED";
            case RESTORING:    return "RESTORING";
            case RESTORED:     return "RESTORED";
        }
        return "UNKNOWN";
    }
}

// Queue processing states
class LFV_QueueState
{
    static const int PENDING   = 0;   // Waiting for active slot (FIFO)
    static const int ACTIVE    = 1;   // Processing batches
    static const int COMPLETE  = 2;   // All batches done
    static const int CANCELLED = 3;   // Aborted (e.g. server shutdown during restore)
    static const int FAILED    = 4;   // Unrecoverable error

    static string ToString(int state)
    {
        switch (state)
        {
            case PENDING:   return "PENDING";
            case ACTIVE:    return "ACTIVE";
            case COMPLETE:  return "COMPLETE";
            case CANCELLED: return "CANCELLED";
            case FAILED:    return "FAILED";
        }
        return "UNKNOWN";
    }
}

// Queue operation types
class LFV_QueueType
{
    static const int VIRTUALIZE = 0;
    static const int RESTORE    = 1;
    static const int DROP       = 2;
}

// Inventory placement types for LFV_ItemRecord
class LFV_InvType
{
    static const int CARGO      = 0;
    static const int ATTACHMENT = 1;
}

// ECE flags from centraleconomy.c (verified values)
class LFV_ECE
{
    static const int IN_INVENTORY           = 787456;      // ECE_CREATEPHYSICS|ECE_KEEPHEIGHT|ECE_NOSURFACEALIGN
    static const int PLACE_ON_SURFACE       = 1060;        // ECE_CREATEPHYSICS|ECE_UPDATEPATHGRAPH|ECE_TRACE
    static const int NOLIFETIME             = 4194304;     // Do not set lifetime on entity
    static const int NOPERSISTENCY_WORLD    = 8388608;     // Do NOT save in world persistence
}

// RF flags
class LFV_RF
{
    static const int DEFAULT = 0;   // RF_DEFAULT
}

// RPC IDs -- vanilla RPCSingleParam pattern (BBP-style)
// High base to avoid collision with other mods
class LFV_RPC
{
    static const int SYNC_SETTINGS   = 24701;
    static const int ADMIN_COMMAND   = 24702;
    // ADMIN_RESPONSE (24703) removed -- responses use ERPCs.RPC_USER_ACTION_MESSAGE
    static const int MANIFEST_UPDATE = 24704;
}

// File paths
class LFV_Paths
{
    static const string STORAGE_DIR = "$profile:LFVStorage";
    static const string SETTINGS_FILE = "$profile:LFVStorage/settings.json";
    static const string IDMAP_FILE = "$profile:LFVStorage/id_map.json";
    static const string FILE_EXT = ".lfv";
    static const string JSON_EXT = ".json";
    static const string TMP_EXT = ".tmp";
    static const string BAK1_EXT = ".bak1";
    static const string BAK2_EXT = ".bak2";
    static const string CORRUPT_EXT = ".corrupt";

    static string GetContainerPath(string storageId)
    {
        string path = STORAGE_DIR + "/container_";
        path = path + storageId;
        path = path + FILE_EXT;
        return path;
    }

    static string GetContainerJsonPath(string storageId)
    {
        string path = STORAGE_DIR + "/container_";
        path = path + storageId;
        path = path + JSON_EXT;
        return path;
    }

    static string GetContainerTmpPath(string storageId)
    {
        string path = STORAGE_DIR + "/container_";
        path = path + storageId;
        path = path + TMP_EXT;
        return path;
    }
}

// Log levels (A4 audit: filter by configured level)
class LFV_LogLevel
{
    static const int CRITICAL = 0;
    static const int ERROR    = 1;
    static const int WARN     = 2;
    static const int INFO     = 3;

    static int FromString(string level)
    {
        if (level == "CRITICAL") return CRITICAL;
        if (level == "ERROR")    return ERROR;
        if (level == "WARN")     return WARN;
        if (level == "INFO")     return INFO;
        return ERROR; // default
    }
}

class LFV_Log
{
    static const string PREFIX = "[LFV] ";
    static int s_Level = LFV_LogLevel.ERROR; // default until settings loaded

    static void SetLevel(string levelStr)
    {
        s_Level = LFV_LogLevel.FromString(levelStr);
    }

    static void Info(string msg)
    {
        if (s_Level < LFV_LogLevel.INFO) return;
        string fullMsg = PREFIX;
        fullMsg = fullMsg + msg;
        Print(fullMsg);
    }

    static void Warn(string msg)
    {
        if (s_Level < LFV_LogLevel.WARN) return;
        string fullMsg = PREFIX;
        fullMsg = fullMsg + "WARN: ";
        fullMsg = fullMsg + msg;
        Print(fullMsg);
    }

    static void Error(string msg)
    {
        if (s_Level < LFV_LogLevel.ERROR) return;
        string fullMsg = PREFIX;
        fullMsg = fullMsg + "ERROR: ";
        fullMsg = fullMsg + msg;
        Print(fullMsg);
    }

    static void Critical(string msg)
    {
        // Critical siempre se imprime
        string fullMsg = PREFIX;
        fullMsg = fullMsg + "CRITICAL: ";
        fullMsg = fullMsg + msg;
        Print(fullMsg);
    }
}
