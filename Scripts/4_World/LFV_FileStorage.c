// =========================================================
// LF_VStorage -- FileStorage (v1.0)
//
// Handles all .lfv binary I/O: Save, Load, atomic write,
// backup rotation, WriteItemRecord/ReadItemRecord campo
// por campo, JSON sidecar generation.
//
// RULES:
// - Every Read() must check return value
// - Magic number verified on every Load
// - Atomic write: .tmp -> CopyFile -> Delete
// - FormatVersion migration via if (ver >= N)
// =========================================================

class LFV_FileStorage
{
    // -----------------------------------------------------------
    // EnsureStorageDir -- idempotent dir-exists check.
    //
    // $storage: aliases resolve to <mission>/storage_N/. The N folder
    // is created by the engine, not us, and may not exist when LFV's
    // OnMissionStart fires (engine populates storage during/after the
    // first mission load). Calling MakeDirectory before storage_N
    // exists is a silent no-op and any subsequent FileSerializer.Open
    // returns "Cannot open for write".
    //
    // Fix: re-check + retry MakeDirectory at every write path. Cheap
    // (one FileExist syscall on the happy path) and defensive against
    // anything else that might delete the dir mid-session.
    //
    // Returns true if the dir exists after the call. Caller should
    // log + abort the write if false.
    // -----------------------------------------------------------
    static bool EnsureStorageDir()
    {
        if (FileExist(LFV_Paths.STORAGE_DIR))
            return true;

        MakeDirectory(LFV_Paths.STORAGE_DIR);

        if (FileExist(LFV_Paths.STORAGE_DIR))
            return true;

        string err = "EnsureStorageDir: cannot create ";
        err = err + LFV_Paths.STORAGE_DIR;
        err = err + " -- storage_N may not exist yet";
        LFV_Log.Error(err);
        return false;
    }

    // -----------------------------------------------------------
    // WRITE -- Item array (recursive)
    // -----------------------------------------------------------
    static void WriteItemArray(FileSerializer file, array<ref LFV_ItemRecord> items, int depth)
    {
        // pre-count non-null entries so written count matches
        // actual records. Prevents binary stream desync on read.
        int actualCount = 0;
        if (items)
        {
            for (int j = 0; j < items.Count(); j++)
            {
                if (items[j])
                    actualCount++;
            }
        }
        file.Write(actualCount);
        if (items)
        {
            for (int i = 0; i < items.Count(); i++)
            {
                LFV_ItemRecord rec = items[i];
                if (!rec) continue;
                WriteItemRecord(file, rec, depth);
            }
        }
    }

    // -----------------------------------------------------------
    // WRITE -- Single item record (campo por campo)
    // -----------------------------------------------------------
    static void WriteItemRecord(FileSerializer file, LFV_ItemRecord rec, int depth)
    {
        file.Write(rec.m_Classname);
        file.Write(rec.m_InvType);
        file.Write(rec.m_Row);
        file.Write(rec.m_Col);
        file.Write(rec.m_Idx);
        file.Write(rec.m_SlotId);
        file.Write(rec.m_Flipped);
        file.Write(rec.m_Health);
        file.Write(rec.m_Quantity);
        file.Write(rec.m_LiquidType);
        file.Write(rec.m_FoodStage);
        file.Write(rec.m_AmmoCount);
        file.Write(rec.m_Temperature);
        file.Write(rec.m_Wetness);
        file.Write(rec.m_Energy);
        file.Write(rec.m_HasEnergy);
        WriteCartridgeArray(file, rec.m_ChamberRounds);
        WriteCartridgeArray(file, rec.m_InternalMagRounds);
        WriteCartridgeArray(file, rec.m_MagRounds);
        // FORMAT >= 3: new explicit fields
        file.Write(rec.m_Agents);
        file.Write(rec.m_Combination);
        file.Write(rec.m_Cleanness);
        int nextDepth = depth + 1;
        if (depth < LFV_Limits.MAX_ITEM_DEPTH)
        {
            WriteItemArray(file, rec.m_Attachments, nextDepth);
            WriteItemArray(file, rec.m_Cargo, nextDepth);
        }
        else
        {
            // Write empty arrays to maintain binary format
            file.Write(0);
            file.Write(0);
        }
    }

    // -----------------------------------------------------------
    // WRITE -- Cartridge array
    // -----------------------------------------------------------
    static void WriteCartridgeArray(FileSerializer file, array<ref LFV_CartridgeData> rounds)
    {
        // pre-count non-null entries (same pattern as C1 fix)
        int actualCount = 0;
        if (rounds)
        {
            for (int j = 0; j < rounds.Count(); j++)
            {
                if (rounds[j])
                    actualCount++;
            }
        }
        file.Write(actualCount);
        if (rounds)
        {
            for (int i = 0; i < rounds.Count(); i++)
            {
                if (!rounds[i]) continue;
                file.Write(rounds[i].m_MuzzleIdx);
                file.Write(rounds[i].m_Damage);
                file.Write(rounds[i].m_AmmoType);
            }
        }
    }

    // -----------------------------------------------------------
    // READ -- Item array (recursive)
    // -----------------------------------------------------------
    static bool ReadItemArray(FileSerializer file, out array<ref LFV_ItemRecord> items, int formatVersion, int depth)
    {
        items = new array<ref LFV_ItemRecord>();
        int count;
        if (!file.Read(count)) return false;
        for (int i = 0; i < count; i++)
        {
            LFV_ItemRecord rec = new LFV_ItemRecord();
            if (!ReadItemRecord(file, rec, formatVersion, depth)) return false;
            items.Insert(rec);
        }
        return true;
    }

    // -----------------------------------------------------------
    // READ -- Single item record (campo por campo)
    // -----------------------------------------------------------
    static bool ReadItemRecord(FileSerializer file, LFV_ItemRecord rec, int formatVersion, int depth)
    {
        if (!file.Read(rec.m_Classname)) return false;
        if (!file.Read(rec.m_InvType)) return false;
        if (!file.Read(rec.m_Row)) return false;
        if (!file.Read(rec.m_Col)) return false;
        if (!file.Read(rec.m_Idx)) return false;
        if (!file.Read(rec.m_SlotId)) return false;
        if (!file.Read(rec.m_Flipped)) return false;
        if (!file.Read(rec.m_Health)) return false;
        if (!file.Read(rec.m_Quantity)) return false;
        if (!file.Read(rec.m_LiquidType)) return false;
        if (!file.Read(rec.m_FoodStage)) return false;
        if (!file.Read(rec.m_AmmoCount)) return false;
        if (!file.Read(rec.m_Temperature)) return false;
        if (!file.Read(rec.m_Wetness)) return false;
        if (!file.Read(rec.m_Energy)) return false;
        if (!file.Read(rec.m_HasEnergy)) return false;
        if (!ReadCartridgeArray(file, rec.m_ChamberRounds)) return false;
        if (!ReadCartridgeArray(file, rec.m_InternalMagRounds)) return false;
        if (!ReadCartridgeArray(file, rec.m_MagRounds)) return false;
        // FORMAT >= 3: new explicit fields
        if (formatVersion >= 3)
        {
            if (!file.Read(rec.m_Agents)) return false;
            if (!file.Read(rec.m_Combination)) return false;
            if (!file.Read(rec.m_Cleanness)) return false;
        }
        int nextDepth = depth + 1;
        if (!ReadItemArray(file, rec.m_Attachments, formatVersion, nextDepth)) return false;
        if (!ReadItemArray(file, rec.m_Cargo, formatVersion, nextDepth)) return false;
        return true;
    }

    // -----------------------------------------------------------
    // READ -- Cartridge array
    // -----------------------------------------------------------
    static bool ReadCartridgeArray(FileSerializer file, out array<ref LFV_CartridgeData> rounds)
    {
        rounds = new array<ref LFV_CartridgeData>();
        int count;
        if (!file.Read(count)) return false;
        for (int i = 0; i < count; i++)
        {
            LFV_CartridgeData cd = new LFV_CartridgeData();
            if (!file.Read(cd.m_MuzzleIdx)) return false;
            if (!file.Read(cd.m_Damage)) return false;
            if (!file.Read(cd.m_AmmoType)) return false;
            rounds.Insert(cd);
        }
        return true;
    }

    // -----------------------------------------------------------
    // SAVE -- Full container to .lfv (writes to tmpPath)
    // -----------------------------------------------------------
    static bool SaveContainerToFile(string filePath, LFV_ContainerFile data)
    {
        FileSerializer file = new FileSerializer();
        if (!file.Open(filePath, FileMode.WRITE))
        {
            string errWrite = "Cannot open for write: ";
            errWrite = errWrite + filePath;
            LFV_Log.Error(errWrite);
            return false;
        }

        // Magic + version
        file.Write(LFV_Magic.LFV_FILE);
        file.Write(data.m_FormatVersion);

        // Metadata campo por campo
        file.Write(data.m_StorageId);
        file.Write(data.m_ContainerClass);
        file.Write(data.m_State);
        file.Write(data.m_Timestamp);
        file.Write(data.m_Position);
        file.Write(data.m_OwnerUID);
        file.Write(data.m_PersistentId);  // v2
        file.Write(data.m_Manifest);
        file.Write(data.m_TotalItemCount);

        // Item tree -- campo por campo recursivo
        WriteItemArray(file, data.m_Items, 0);

        // FORMAT 3: no StoreCtx section. Write hasCtx=false for
        // backward compat (FORMAT 2 readers check this flag).
        bool hasCtx = false;
        file.Write(hasCtx);

        file.Close();
        return true;
    }

    // -----------------------------------------------------------
    // LOAD -- Full container from .lfv
    //
    // Returns: true on success
    // Out params: data (header + item tree)
    // -----------------------------------------------------------
    static bool LoadContainerFromFile(string path, out LFV_ContainerFile data)
    {
        data = new LFV_ContainerFile();

        FileSerializer file = new FileSerializer();
        if (!file.Open(path, FileMode.READ))
        {
            string errRead = "Cannot open for read: ";
            errRead = errRead + path;
            LFV_Log.Error(errRead);
            return false;
        }

        // Magic check
        int magic;
        if (!file.Read(magic)) { file.Close(); return false; }
        if (magic != LFV_Magic.LFV_FILE)
        {
            string errMagic = "Invalid magic number in: ";
            errMagic = errMagic + path;
            LFV_Log.Error(errMagic);
            file.Close();
            return false;
        }

        if (!file.Read(data.m_FormatVersion)) { file.Close(); return false; }

        if (data.m_FormatVersion > LFV_Version.FORMAT)
        {
            string fmtMsg = "Format version ";
            fmtMsg = fmtMsg + data.m_FormatVersion.ToString();
            fmtMsg = fmtMsg + " > current ";
            fmtMsg = fmtMsg + LFV_Version.FORMAT.ToString();
            fmtMsg = fmtMsg + " at ";
            fmtMsg = fmtMsg + path;
            LFV_Log.Error(fmtMsg);
            file.Close();
            return false;
        }

        // Metadata campo por campo
        if (!file.Read(data.m_StorageId)) { file.Close(); return false; }
        if (!file.Read(data.m_ContainerClass)) { file.Close(); return false; }
        if (!file.Read(data.m_State)) { file.Close(); return false; }
        if (!file.Read(data.m_Timestamp)) { file.Close(); return false; }
        if (!file.Read(data.m_Position)) { file.Close(); return false; }
        if (!file.Read(data.m_OwnerUID)) { file.Close(); return false; }
        if (data.m_FormatVersion >= 2)
        {
            if (!file.Read(data.m_PersistentId)) { file.Close(); return false; }
        }
        if (!file.Read(data.m_Manifest)) { file.Close(); return false; }
        if (!file.Read(data.m_TotalItemCount)) { file.Close(); return false; }

        // Item tree
        if (!ReadItemArray(file, data.m_Items, data.m_FormatVersion, 0)) { file.Close(); return false; }

        // StoreCtx section (FORMAT 2 backward compat)
        // FORMAT 3 always writes hasCtx=false. FORMAT 2 may have StoreCtx data.
        // Either way, we no longer use StoreCtx -- close and discard.
        bool hasCtx;
        if (!file.Read(hasCtx)) { file.Close(); return false; }

        // StoreCtx section discarded -- explicit fields replace OnStoreSave/OnStoreLoad
        file.Close();

        return true;
    }

    // -----------------------------------------------------------
    // LOAD -- Header only (for orphan cleanup, reconciliation)
    // -----------------------------------------------------------
    static bool LoadHeaderOnly(string path, out LFV_ContainerFile data)
    {
        data = new LFV_ContainerFile();
        FileSerializer file = new FileSerializer();
        if (!file.Open(path, FileMode.READ))
            return false;

        int magic;
        if (!file.Read(magic)) { file.Close(); return false; }
        if (magic != LFV_Magic.LFV_FILE) { file.Close(); return false; }
        if (!file.Read(data.m_FormatVersion)) { file.Close(); return false; }
        if (!file.Read(data.m_StorageId)) { file.Close(); return false; }
        if (!file.Read(data.m_ContainerClass)) { file.Close(); return false; }
        if (!file.Read(data.m_State)) { file.Close(); return false; }
        if (!file.Read(data.m_Timestamp)) { file.Close(); return false; }
        if (!file.Read(data.m_Position)) { file.Close(); return false; }
        if (!file.Read(data.m_OwnerUID)) { file.Close(); return false; }
        if (data.m_FormatVersion >= 2)
        {
            if (!file.Read(data.m_PersistentId)) { file.Close(); return false; }
        }
        if (!file.Read(data.m_Manifest)) { file.Close(); return false; }
        if (!file.Read(data.m_TotalItemCount)) { file.Close(); return false; }

        file.Close();
        return true;
    }

    // -----------------------------------------------------------
    // ATOMIC WRITE -- .tmp -> CopyFile -> Delete
    //
    // .tmp is kept as last-resort fallback if crash occurs
    // between DeleteFile and CopyFile. LoadWithFallback now checks
    // for .tmp after bak2 fails. The .tmp is only deleted after
    // the copy to basePath succeeds.
    // -----------------------------------------------------------
    static bool AtomicSave(string storageId, LFV_ContainerFile data)
    {
        if (!EnsureStorageDir()) return false;

        string basePath = LFV_Paths.GetContainerPath(storageId);
        string tmpPath = basePath + LFV_Paths.TMP_EXT;

        bool writeOk = SaveContainerToFile(tmpPath, data);
        if (!writeOk)
        {
            DeleteFile(tmpPath);
            return false;
        }

        // DayZ CopyFile cannot overwrite -- must delete first.
        // If crash between Delete and Copy: .tmp still has valid data
        // and LoadWithFallback will find it (H3 fix).
        if (FileExist(basePath))
            DeleteFile(basePath);

        if (!CopyFile(tmpPath, basePath))
        {
            string errCopy = "CopyFile failed: ";
            errCopy = errCopy + tmpPath;
            errCopy = errCopy + " -> ";
            errCopy = errCopy + basePath;
            LFV_Log.Error(errCopy);
            // H3: do NOT delete .tmp on copy failure --
            // it's the only valid copy left. LoadWithFallback
            // or CleanOrphanTmpFiles will handle it on restart.
            return false;
        }

        // Verify the copied .lfv is readable (detect truncated/partial copies)
        LFV_ContainerFile verifyData;
        if (!LoadHeaderOnly(basePath, verifyData))
        {
            string verifyErr = "Post-copy verification failed (truncated?): ";
            verifyErr = verifyErr + basePath;
            LFV_Log.Error(verifyErr);
            // Keep .tmp as recovery -- basePath is unusable
            DeleteFile(basePath);
            return false;
        }

        DeleteFile(tmpPath);
        return true;
    }

    // -----------------------------------------------------------
    // LOAD WITH FALLBACK -- .lfv -> .bak1 -> .bak2
    // -----------------------------------------------------------
    static bool LoadWithFallback(string storageId, out LFV_ContainerFile data)
    {
        string basePath = LFV_Paths.GetContainerPath(storageId);

        // Try primary
        if (FileExist(basePath))
        {
            if (LoadContainerFromFile(basePath, data))
                return true;
            string warnPrimary = "Primary .lfv corrupt, trying bak1: ";
            warnPrimary = warnPrimary + storageId;
            LFV_Log.Warn(warnPrimary);
        }

        // Try bak1
        string bak1 = basePath + LFV_Paths.BAK1_EXT;
        if (FileExist(bak1))
        {
            if (LoadContainerFromFile(bak1, data))
            {
                string infoBak1 = "Loaded from bak1: ";
                infoBak1 = infoBak1 + storageId;
                LFV_Log.Info(infoBak1);
                return true;
            }
            string warnBak1 = "bak1 also corrupt, trying bak2: ";
            warnBak1 = warnBak1 + storageId;
            LFV_Log.Warn(warnBak1);
        }

        // Try bak2
        string bak2 = basePath + LFV_Paths.BAK2_EXT;
        if (FileExist(bak2))
        {
            if (LoadContainerFromFile(bak2, data))
            {
                string infoBak2 = "Loaded from bak2: ";
                infoBak2 = infoBak2 + storageId;
                LFV_Log.Info(infoBak2);
                return true;
            }
        }

        // try .tmp as last resort (crash during AtomicSave
        // left valid data in .tmp but basePath was already deleted)
        string tmpPath = basePath + LFV_Paths.TMP_EXT;
        if (FileExist(tmpPath))
        {
            if (LoadContainerFromFile(tmpPath, data))
            {
                string infoTmp = "Recovered from .tmp (crash during save): ";
                infoTmp = infoTmp + storageId;
                LFV_Log.Warn(infoTmp);
                // Promote .tmp to primary so future saves work normally.
                // If CopyFile fails (disk full, perms), keep the .tmp so the
                // next boot can still recover from it -- deleting it would
                // leave the container with zero disk-side data after a
                // clean CopyFile failure (which LoadContainerFromFile above
                // already proved to be valid data we'd otherwise throw away).
                if (!CopyFile(tmpPath, basePath))
                {
                    string copyFailMsg = "Failed to promote .tmp to .lfv (";
                    copyFailMsg = copyFailMsg + tmpPath;
                    copyFailMsg = copyFailMsg + ") -- keeping .tmp for next boot recovery";
                    LFV_Log.Error(copyFailMsg);
                    return true;
                }
                DeleteFile(tmpPath);
                return true;
            }
        }

        // All failed -- rename primary to .corrupt for admin inspection
        if (FileExist(basePath))
        {
            string corruptPath = basePath + LFV_Paths.CORRUPT_EXT;
            CopyFile(basePath, corruptPath);
            DeleteFile(basePath);
            string critMsg = "All copies corrupt, renamed to .corrupt: ";
            critMsg = critMsg + storageId;
            LFV_Log.Critical(critMsg);
        }

        return false;
    }

    // -----------------------------------------------------------
    // BACKUP ROTATION -- returns false on CopyFile failure so callers
    // can abort the save and preserve the existing .lfv. Silent failure
    // here (disk full, perms) used to let AtomicSave clobber the only
    // good copy with a possibly-bad new one.
    // -----------------------------------------------------------
    static bool RotateBackups(string storageId)
    {
        if (!EnsureStorageDir()) return false;

        string basePath = LFV_Paths.GetContainerPath(storageId);
        string bak1 = basePath + LFV_Paths.BAK1_EXT;
        string bak2 = basePath + LFV_Paths.BAK2_EXT;

        // Delete bak2
        if (FileExist(bak2))
            DeleteFile(bak2);

        // bak1 -> bak2
        if (FileExist(bak1))
        {
            if (!CopyFile(bak1, bak2))
            {
                string err1 = "RotateBackups: bak1->bak2 failed for ";
                err1 = err1 + storageId;
                LFV_Log.Error(err1);
                return false;
            }
            // must delete bak1 after copying to bak2,
            // because DayZ CopyFile cannot overwrite existing files.
            // Without this, the next CopyFile(basePath, bak1) silently
            // fails and bak1 stays frozen to the first backup ever made.
            DeleteFile(bak1);
        }

        // current -> bak1
        if (FileExist(basePath))
        {
            if (!CopyFile(basePath, bak1))
            {
                string err2 = "RotateBackups: base->bak1 failed for ";
                err2 = err2 + storageId;
                LFV_Log.Error(err2);
                return false;
            }
        }

        return true;
    }

    // -----------------------------------------------------------
    // CLEAN ORPHAN .tmp FILES
    // -----------------------------------------------------------
    static void CleanOrphanTmpFiles()
    {
        string fileName;
        FileAttr fileAttr;
        string searchPath = LFV_Paths.STORAGE_DIR + "/*.tmp";
        FindFileHandle handle = FindFile(searchPath, fileName, fileAttr, 0);
        int cleaned = 0;
        int kept = 0;
        if (handle)
        {
            bool keepGoing = true;
            while (keepGoing)
            {
                string fullPath = LFV_Paths.STORAGE_DIR;
                fullPath = fullPath + "/";
                fullPath = fullPath + fileName;

                // if the corresponding .lfv doesn't exist,
                // this .tmp may be a crash-recovery file. Keep it --
                // LoadWithFallback will use it as last resort.
                string lfvPath = fullPath.Substring(0, fullPath.Length() - 4); // strip .tmp
                if (!FileExist(lfvPath))
                {
                    kept++;
                    string keepMsg = "Keeping .tmp (no .lfv, possible recovery): ";
                    keepMsg = keepMsg + fileName;
                    LFV_Log.Warn(keepMsg);
                }
                else
                {
                    DeleteFile(fullPath);
                    cleaned++;
                }
                keepGoing = FindNextFile(handle, fileName, fileAttr);
            }
            CloseFindFile(handle);
        }
        if (cleaned > 0 || kept > 0)
        {
            string cleanMsg = "Cleaned ";
            cleanMsg = cleanMsg + cleaned.ToString();
            cleanMsg = cleanMsg + " orphan .tmp files";
            if (kept > 0)
            {
                cleanMsg = cleanMsg + ", kept ";
                cleanMsg = cleanMsg + kept.ToString();
                cleanMsg = cleanMsg + " recovery .tmp files";
            }
            LFV_Log.Info(cleanMsg);
        }
    }

    // -----------------------------------------------------------
    // DELETE ALL FILES for a container
    // -----------------------------------------------------------
    static void DeleteContainerFiles(string storageId)
    {
        string basePath = LFV_Paths.GetContainerPath(storageId);
        string jsonPath = LFV_Paths.GetContainerJsonPath(storageId);
        string bak1 = basePath + LFV_Paths.BAK1_EXT;
        string bak2 = basePath + LFV_Paths.BAK2_EXT;

        if (FileExist(basePath)) DeleteFile(basePath);
        if (FileExist(jsonPath)) DeleteFile(jsonPath);
        if (FileExist(bak1)) DeleteFile(bak1);
        if (FileExist(bak2)) DeleteFile(bak2);
    }

    // -----------------------------------------------------------
    // COUNT TOTAL ITEMS (recursive, for m_TotalItemCount)
    // -----------------------------------------------------------
    static int CountItemsRecursive(array<ref LFV_ItemRecord> items, int depth)
    {
        int total = 0;
        if (!items) return 0;
        if (depth >= LFV_Limits.MAX_ITEM_DEPTH) return 0;
        int nextDepth = depth + 1;
        for (int i = 0; i < items.Count(); i++)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec) continue;
            total++;
            total = total + CountItemsRecursive(rec.m_Attachments, nextDepth);
            total = total + CountItemsRecursive(rec.m_Cargo, nextDepth);
        }
        return total;
    }

    // -----------------------------------------------------------
    // CLEAR m_ItemRef recursively from item tree.
    //
    // Shared utility: used by SaveAdminJson (JSON-FIX) and
    // VirtualizeQueue MEM2 cleanup.
    //
    // ItemBase is a native engine type that hard-crashes
    // JsonFileLoader even when protected. Nulling before
    // JsonSaveFile prevents the crash. Safe because no code
    // reads m_ItemRef after PrepareVirtualization -- item
    // deletion uses the live container inventory, not the tree.
    // -----------------------------------------------------------
    static void ClearItemRefs(array<ref LFV_ItemRecord> items)
    {
        if (!items) return;
        for (int i = 0; i < items.Count(); i++)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec) continue;
            rec.SetItemRef(null);
            ClearItemRefs(rec.m_Attachments);
            ClearItemRefs(rec.m_Cargo);
        }
    }

    // -----------------------------------------------------------
    // FLATTEN DEPTH-FIRST (utility for item enumeration)
    // -----------------------------------------------------------
    static void FlattenDepthFirst(array<ref LFV_ItemRecord> items, array<ItemBase> outFlat, int depth)
    {
        if (!items) return;
        if (depth >= LFV_Limits.MAX_ITEM_DEPTH) return;
        int nextDepth = depth + 1;
        for (int i = 0; i < items.Count(); i++)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec) continue;
            ItemBase itemRef = rec.GetItemRef();
            if (itemRef)
                outFlat.Insert(itemRef);
            FlattenDepthFirst(rec.m_Attachments, outFlat, nextDepth);
            FlattenDepthFirst(rec.m_Cargo, outFlat, nextDepth);
        }
    }

    // -----------------------------------------------------------
    // Restore marker -- lightweight file to detect crash-during-restore.
    //
    // Created at RestoreQueue.OnStart, deleted at OnComplete/OnCancel.
    // If present at startup, reconciliation knows a restore was
    // interrupted and should ClearPhantomItems + re-restore from .lfv.
    // -----------------------------------------------------------
    static void WriteRestoreMarker(string storageId)
    {
        if (!EnsureStorageDir()) return;

        string path = LFV_Paths.GetContainerPath(storageId) + ".restoring";
        FileHandle file = OpenFile(path, FileMode.WRITE);
        if (file)
        {
            CloseFile(file);
        }
    }

    static void DeleteRestoreMarker(string storageId)
    {
        string path = LFV_Paths.GetContainerPath(storageId) + ".restoring";
        if (FileExist(path))
            DeleteFile(path);
    }

    static bool HasRestoreMarker(string storageId)
    {
        string path = LFV_Paths.GetContainerPath(storageId) + ".restoring";
        return FileExist(path);
    }

    // -----------------------------------------------------------
    // Clean orphan .restoring markers on startup.
    // A .restoring marker without a corresponding .lfv is stale
    // (the restore completed but the marker wasn't cleaned up).
    // -----------------------------------------------------------
    static void CleanOrphanRestoreMarkers()
    {
        string fileName;
        FileAttr fileAttr;
        string searchPath = LFV_Paths.STORAGE_DIR + "/*.restoring";
        FindFileHandle handle = FindFile(searchPath, fileName, fileAttr, 0);
        int cleaned = 0;
        if (handle)
        {
            bool keepGoing = true;
            while (keepGoing)
            {
                string fullPath = LFV_Paths.STORAGE_DIR;
                fullPath = fullPath + "/";
                fullPath = fullPath + fileName;

                // Check if corresponding .lfv exists
                // fileName = "container_xxx.lfv.restoring" -> strip ".restoring"
                string lfvPath = fullPath.Substring(0, fullPath.Length() - 10);
                if (!FileExist(lfvPath))
                {
                    DeleteFile(fullPath);
                    cleaned++;
                }
                keepGoing = FindNextFile(handle, fileName, fileAttr);
            }
            CloseFindFile(handle);
        }
        if (cleaned > 0)
        {
            string msg = "Cleaned ";
            msg = msg + cleaned.ToString();
            msg = msg + " orphan .restoring markers";
            LFV_Log.Info(msg);
        }
    }
}
