// =========================================================
// LF_VStorage -- Cleanup (Sprint 3, Phase C.2)
//
// Scans .lfv files on disk and identifies orphans -- files
// that have no matching container in the world.
//
// Verification chain:
//   1. Check m_ContainerStates (tracked containers)
//   2. Check IdMap (PersistentID -> StorageId)
//   3. Fallback: classname + position +-1m
//
// Safety:
//   - SKIP files for containers in RESTORING state
//   - SKIP files for containers with active queues
//   - DryRun mode (default) only logs, never deletes
//
// Called 120s after OnMissionLoaded via one-shot CallLater.
// =========================================================

class LFV_Cleanup
{
    // -----------------------------------------------------------
    // Run cleanup scan
    //
    // module: reference to LFV_Module for accessing state
    // -----------------------------------------------------------
    static void Run(LFV_Module module)
    {
        if (!module) return;

        LFV_Settings settings = module.GetSettings();
        if (!settings) return;

        bool dryRun = settings.m_CleanupDryRun;

        string modeStr = "LIVE";
        if (dryRun)
            modeStr = "DRY-RUN";

        string startMsg = "Cleanup started (";
        startMsg = startMsg + modeStr;
        startMsg = startMsg + ")";
        LFV_Log.Info(startMsg);

        int scanned = 0;
        int orphans = 0;
        int skipped = 0;
        int deleted = 0;

        // Scan .lfv files on disk
        string fileName;
        FileAttr fileAttr;
        string searchPath = LFV_Paths.STORAGE_DIR;
        searchPath = searchPath + "/*";
        searchPath = searchPath + LFV_Paths.FILE_EXT;
        FindFileHandle handle = FindFile(searchPath, fileName, fileAttr, 0);

        if (!handle)
        {
            string noFilesMsg = "Cleanup: no .lfv files found";
            LFV_Log.Info(noFilesMsg);
            return;
        }

        // Collect orphan storageIds first (avoid modifying files during scan)
        array<string> orphanIds = new array<string>();
        bool keepScanning = true;

        while (keepScanning)
        {
            scanned++;
            string fullPath = LFV_Paths.STORAGE_DIR;
            fullPath = fullPath + "/";
            fullPath = fullPath + fileName;

            // Read header only (fast -- no item data)
            LFV_ContainerFile fileData;
            bool headerOk = LFV_FileStorage.LoadHeaderOnly(fullPath, fileData);
            if (!headerOk)
            {
                string headerErr = "Cleanup: failed to read header: ";
                headerErr = headerErr + fileName;
                LFV_Log.Warn(headerErr);
                keepScanning = FindNextFile(handle, fileName, fileAttr);
                continue;
            }

            string storageId = fileData.m_StorageId;

            // --- Verification: single 3-layer lookup ---
            ItemBase foundContainer = module.FindContainerByStorageId(
                storageId, fileData.m_ContainerClass, fileData.m_Position);
            if (foundContainer)
            {
                keepScanning = FindNextFile(handle, fileName, fileAttr);
                continue;
            }

            // --- Not found: this is an orphan ---

            // Safety check: don't delete if any container is RESTORING
            if (IsRestoringOrQueued(module, storageId))
            {
                string skipMsg = "Cleanup: SKIP (restoring/queued): ";
                skipMsg = skipMsg + storageId;
                LFV_Log.Info(skipMsg);
                skipped++;
                keepScanning = FindNextFile(handle, fileName, fileAttr);
                continue;
            }

            orphans++;
            orphanIds.Insert(storageId);

            string orphanMsg = "Cleanup: ORPHAN ";
            if (dryRun)
                orphanMsg = orphanMsg + "(would delete): ";
            else
                orphanMsg = orphanMsg + "(deleting): ";
            orphanMsg = orphanMsg + storageId;
            orphanMsg = orphanMsg + " [";
            orphanMsg = orphanMsg + fileData.m_ContainerClass;
            orphanMsg = orphanMsg + " @ ";
            orphanMsg = orphanMsg + fileData.m_Position.ToString();
            orphanMsg = orphanMsg + "]";
            LFV_Log.Info(orphanMsg);

            keepScanning = FindNextFile(handle, fileName, fileAttr);
        }
        CloseFindFile(handle);

        // Delete orphans (if not dry run)
        if (!dryRun)
        {
            for (int d = 0; d < orphanIds.Count(); d++)
            {
                string delId = orphanIds[d];
                LFV_FileStorage.DeleteContainerFiles(delId);
                deleted++;
            }
        }

        // Summary log
        string summary = "Cleanup complete: ";
        summary = summary + scanned.ToString();
        summary = summary + " scanned, ";
        summary = summary + orphans.ToString();
        summary = summary + " orphans";
        if (dryRun)
        {
            summary = summary + " (DRY-RUN, nothing deleted)";
        }
        else
        {
            summary = summary + ", ";
            summary = summary + deleted.ToString();
            summary = summary + " deleted";
        }
        if (skipped > 0)
        {
            summary = summary + ", ";
            summary = summary + skipped.ToString();
            summary = summary + " skipped (active)";
        }
        LFV_Log.Info(summary);
    }

    // -----------------------------------------------------------
    // Check if any tracked container with this storageId is in
    // RESTORING state or has an active queue.
    // NOTE: At this point FindContainerByStorageId returned null,
    // meaning the container is NOT tracked. But we still check
    // module's tracked containers in case the storageId matches
    // a container whose entity ref is stale (edge case).
    // -----------------------------------------------------------
    protected static bool IsRestoringOrQueued(LFV_Module module, string storageId)
    {
        int count = module.GetTrackedCount();
        for (int i = 0; i < count; i++)
        {
            ItemBase container = module.GetTrackedContainerAt(i);
            if (!container) continue;

            LFV_ContainerState state = module.GetContainerState(container);
            if (!state) continue;

            if (state.m_StorageId != storageId) continue;

            if (state.m_State == LFV_State.RESTORING)
                return true;

            if (state.m_IsProcessing)
                return true;

            if (module.HasQueueForContainer(container))
                return true;
        }
        return false;
    }
}
