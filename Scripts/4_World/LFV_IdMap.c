// =========================================================
// LF_VStorage -- IdMap (Sprint 3, Phase C.1)
//
// Maps PersistentID -> StorageId for vanilla containers
// that don't have OnStoreSave/OnStoreLoad (not LFV_Barrel_Base).
//
// PersistentID: 4 ints from EntityAI.GetPersistentID() ->
// concatenated as "b1_b2_b3_b4" string key.
//
// IMPORTANT: The vanilla reverse-lookup function has a TYPO
// in Bohemia's API: GetEntityByPersitentID (missing 's').
// This is NOT a bug in our code.
//
// Storage: JSON file at $profile:LFVStorage/id_map.json
// Load on MissionStart, save after registration batches.
// =========================================================

// -----------------------------------------------------------
// Serializable entry for JSON
// -----------------------------------------------------------
class LFV_IdMapEntry
{
    string m_PersistentId;
    string m_StorageId;
    string m_Classname;
    vector m_Position;
}

// -----------------------------------------------------------
// Serializable wrapper (JsonFileLoader needs a root object)
// -----------------------------------------------------------
class LFV_IdMapData
{
    ref array<ref LFV_IdMapEntry> m_Mapping;

    void LFV_IdMapData()
    {
        m_Mapping = new array<ref LFV_IdMapEntry>();
    }
}

// -----------------------------------------------------------
// Static helper -- Load, Save, Register, Lookup, BuildKey
// -----------------------------------------------------------
class LFV_IdMap
{
    // -----------------------------------------------------------
    // Build string key from 4 persistent ID blocks
    // Format: "b1_b2_b3_b4"
    // -----------------------------------------------------------
    static string BuildKey(int b1, int b2, int b3, int b4)
    {
        string key = b1.ToString();
        key = key + "_";
        key = key + b2.ToString();
        key = key + "_";
        key = key + b3.ToString();
        key = key + "_";
        key = key + b4.ToString();
        return key;
    }

    // -----------------------------------------------------------
    // Get persistent ID key from an entity
    // Returns "" if entity is null or ID is all zeros
    // -----------------------------------------------------------
    static string GetKeyFromEntity(EntityAI entity)
    {
        if (!entity) return "";

        int b1 = 0;
        int b2 = 0;
        int b3 = 0;
        int b4 = 0;
        entity.GetPersistentID(b1, b2, b3, b4);

        // All zeros means no persistent ID assigned
        if (b1 == 0 && b2 == 0 && b3 == 0 && b4 == 0)
            return "";

        return BuildKey(b1, b2, b3, b4);
    }

    // -----------------------------------------------------------
    // Load from JSON -> populate the runtime map
    // Returns number of entries loaded
    // -----------------------------------------------------------
    static int Load(map<string, string> outMap)
    {
        if (!outMap) return 0;

        string filePath = LFV_Paths.IDMAP_FILE;
        if (!FileExist(filePath))
        {
            string idmNoFile = "IdMap: no file found, starting fresh";
            LFV_Log.Info(idmNoFile);
            return 0;
        }

        LFV_IdMapData data = new LFV_IdMapData();
        JsonFileLoader<LFV_IdMapData>.JsonLoadFile(filePath, data);

        if (!data.m_Mapping)
        {
            string idmNullWarn = "IdMap: file loaded but m_Mapping is null";
            LFV_Log.Warn(idmNullWarn);
            return 0;
        }

        int count = 0;
        for (int i = 0; i < data.m_Mapping.Count(); i = i + 1)
        {
            LFV_IdMapEntry entry = data.m_Mapping[i];
            if (!entry) continue;
            if (entry.m_PersistentId == "") continue;
            if (entry.m_StorageId == "") continue;

            outMap.Set(entry.m_PersistentId, entry.m_StorageId);
            count = count + 1;
        }

        string msg = "IdMap: loaded ";
        msg = msg + count.ToString();
        msg = msg + " entries";
        LFV_Log.Info(msg);
        return count;
    }

    // -----------------------------------------------------------
    // Save the runtime map to JSON (atomic write)
    // containerStates: optional -- if provided, populates classname
    //   and position from live entities for diagnostic value
    // -----------------------------------------------------------
    static void Save(map<string, string> inMap, map<ItemBase, ref LFV_ContainerState> containerStates)
    {
        if (!inMap) return;

        // Build reverse lookup: storageId -> container (if states available)
        map<string, ItemBase> sidToContainer = null;
        if (containerStates)
        {
            sidToContainer = new map<string, ItemBase>();
            for (int cs = 0; cs < containerStates.Count(); cs = cs + 1)
            {
                ItemBase csContainer = containerStates.GetKey(cs);
                LFV_ContainerState csState = containerStates.GetElement(cs);
                if (csContainer && csState)
                {
                    sidToContainer.Set(csState.m_StorageId, csContainer);
                }
            }
        }

        LFV_IdMapData data = new LFV_IdMapData();

        for (int i = 0; i < inMap.Count(); i = i + 1)
        {
            string persistId = inMap.GetKey(i);
            string storageId = inMap.GetElement(i);

            LFV_IdMapEntry entry = new LFV_IdMapEntry();
            entry.m_PersistentId = persistId;
            entry.m_StorageId = storageId;

            // Populate classname and position from live entity if available
            entry.m_Classname = "";
            entry.m_Position = vector.Zero;
            if (sidToContainer && sidToContainer.Contains(storageId))
            {
                ItemBase ent = sidToContainer.Get(storageId);
                if (ent)
                {
                    entry.m_Classname = ent.GetType();
                    entry.m_Position = ent.GetPosition();
                }
            }

            data.m_Mapping.Insert(entry);
        }

        // Atomic write: save to .tmp, copy to final, delete .tmp
        // NOTE: DayZ CopyFile cannot overwrite, so we must delete first.
        // Risk: if CopyFile fails after DeleteFile, both files are lost.
        // Same pattern used in LFV_FileStorage.AtomicSave (Sprint 1).
        // Acceptable risk: IdMap is rebuilt on next restart from live entities.
        string filePath = LFV_Paths.IDMAP_FILE;
        string tmpPath = filePath + LFV_Paths.TMP_EXT;

        JsonFileLoader<LFV_IdMapData>.JsonSaveFile(tmpPath, data);

        if (FileExist(tmpPath))
        {
            if (FileExist(filePath))
                DeleteFile(filePath);
            if (!CopyFile(tmpPath, filePath))
            {
                // M1 fix: do NOT delete .tmp on copy failure --
                // it's the only valid copy. IdMap rebuilds from
                // live entities on restart, so this is recoverable.
                string errMsg = "IdMap: CopyFile failed, keeping .tmp";
                LFV_Log.Error(errMsg);
                return;
            }
            DeleteFile(tmpPath);
        }

        string msg = "IdMap: saved ";
        msg = msg + inMap.Count().ToString();
        msg = msg + " entries";
        LFV_Log.Info(msg);
    }

    // -----------------------------------------------------------
    // Register a new mapping
    // -----------------------------------------------------------
    static void Register(map<string, string> idMap, string persistentId, string storageId)
    {
        if (!idMap) return;
        if (persistentId == "") return;
        if (storageId == "") return;

        idMap.Set(persistentId, storageId);
    }

    // -----------------------------------------------------------
    // Lookup: PersistentID -> StorageId
    // Returns "" if not found
    // -----------------------------------------------------------
    static string Lookup(map<string, string> idMap, string persistentId)
    {
        if (!idMap) return "";
        if (persistentId == "") return "";

        if (idMap.Contains(persistentId))
            return idMap.Get(persistentId);

        return "";
    }

    // -----------------------------------------------------------
    // Remove a mapping (for cleanup when container is deleted)
    // -----------------------------------------------------------
    static void Remove(map<string, string> idMap, string persistentId)
    {
        if (!idMap) return;
        if (persistentId == "") return;

        if (idMap.Contains(persistentId))
            idMap.Remove(persistentId);
    }
}
