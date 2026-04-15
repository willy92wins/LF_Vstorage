// =============================================================
// SERVER-ONLY -- all heavy logic via modded class
// =============================================================
#ifdef SERVER
modded class LFV_Module
{
    // -----------------------------------------------------------
    // CF Lifecycle -- OnInit override (enable server events)
    // -----------------------------------------------------------
    override void OnInit()
    {
        super.OnInit();
        EnableMissionStart();
        EnableMissionLoaded();
        EnableMissionFinish();
    }

    // -----------------------------------------------------------
    // CF Lifecycle -- OnMissionStart (server)
    // -----------------------------------------------------------
    override void OnMissionStart(Class sender, CF_EventArgs args)
    {
        super.OnMissionStart(sender, args);

        MakeDirectory(LFV_Paths.STORAGE_DIR);
        m_Settings.Load();
        LFV_Log.SetLevel(m_Settings.m_LogLevel);
        LFV_Registry.Init(m_Settings);
        LFV_IdMap.Load(m_PersistentIdToStorageId);
        LFV_Stats.Reset();
        LFV_FileStorage.CleanOrphanTmpFiles();
        LFV_FileStorage.CleanOrphanRestoreMarkers();

        if (m_Settings.m_AutoCloseEnabled)
        {
            StartAutoCloseTimer();
        }

        StartProximityMonitor();
        StartIdMapSaveTimer();
        StartPeriodicScanTimer();

        string startMsg = "Module started -- v";
        startMsg = startMsg + LFV_Version.MOD_VERSION;
        LFV_Log.Info(startMsg);

        if (m_Settings.m_CleanupDryRun)
        {
            Print("[LFV] CleanupDryRun is ON -- orphan .lfv files will NOT be deleted. Set m_CleanupDryRun=false in settings.json once confirmed working.");
        }
        Print("[LFV] NOTE: Restored items are persistent. See README 'Known limitations'.");
    }

    // -----------------------------------------------------------
    // CF Lifecycle -- OnMissionLoaded (server)
    // -----------------------------------------------------------
    override void OnMissionLoaded(Class sender, CF_EventArgs args)
    {
        super.OnMissionLoaded(sender, args);
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(this.ScanAndReconcile, 10000, false);
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(this.RunCleanup, 120000, false);

        string loadedMsg = "OnMissionLoaded -- scan in 10s, cleanup in 120s";
        LFV_Log.Info(loadedMsg);
    }

    // -----------------------------------------------------------
    // CF Lifecycle -- OnMissionFinish (server)
    // -----------------------------------------------------------
    override void OnMissionFinish(Class sender, CF_EventArgs args)
    {
        m_IsShuttingDown = true;

        // H2 fix: cancel pending CallLater entries before stopping timers.
        // If server restarts before these fire (10s/120s), they'd execute
        // on a partially destroyed module, causing crashes.
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(this.ScanAndReconcile);
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).Remove(this.RunCleanup);

        StopAutoCloseTimer();
        StopProximityMonitor();
        StopIdMapSaveTimer();
        StopPeriodicScanTimer();
        StopQueueProcessor();

        for (int q = 0; q < m_ActiveQueues.Count(); q = q + 1)
        {
            LFV_Queue queue = m_ActiveQueues[q];
            if (!queue) continue;

            int qType = queue.GetQueueType();

            if (qType == LFV_QueueType.VIRTUALIZE)
            {
                queue.CompleteNow();
            }
            else if (qType == LFV_QueueType.RESTORE)
            {
                queue.Cancel();
            }
            else if (qType == LFV_QueueType.DROP)
            {
                queue.CompleteNow();
            }
        }
        m_ActiveQueues.Clear();

        for (int p = 0; p < m_PendingQueues.Count(); p = p + 1)
        {
            LFV_Queue pQueue = m_PendingQueues[p];
            if (!pQueue) continue;

            int pType = pQueue.GetQueueType();

            if (pType == LFV_QueueType.VIRTUALIZE || pType == LFV_QueueType.DROP)
            {
                pQueue.Start();
                pQueue.CompleteNow();
            }
            else if (pType == LFV_QueueType.RESTORE)
            {
                // Never started -- nothing to cancel, just discard
            }
        }
        m_PendingQueues.Clear();
        m_QueuedContainers.Clear();

        int startTime = GetGame().GetTime();
        int budgetMs = m_Settings.m_ShutdownBudgetMs;
        int virtualized = 0;

        // A2 audit: copy keys before iterating -- prevents issues if
        // VirtualizeSynchronous or engine callbacks modify the map
        ref array<ItemBase> shutdownContainers = new array<ItemBase>();
        for (int sc = 0; sc < m_ContainerStates.Count(); sc = sc + 1)
        {
            shutdownContainers.Insert(m_ContainerStates.GetKey(sc));
        }

        for (int i = 0; i < shutdownContainers.Count(); i = i + 1)
        {
            int elapsed = GetGame().GetTime() - startTime;
            if (elapsed > budgetMs)
            {
                string timeoutMsg = "OnMissionFinish timeout after ";
                timeoutMsg = timeoutMsg + elapsed.ToString();
                timeoutMsg = timeoutMsg + "ms -- ";
                timeoutMsg = timeoutMsg + virtualized.ToString();
                timeoutMsg = timeoutMsg + " containers saved";
                LFV_Log.Warn(timeoutMsg);
                break;
            }

            ItemBase container = shutdownContainers[i];
            LFV_ContainerState state = GetContainerState(container);
            if (!state) continue;

            if (state.m_State == LFV_State.IDLE)
            {
                if (container && LFV_StateMachine.HasCargoOrAttachments(container))
                {
                    VirtualizeSynchronous(container, state, false);
                    virtualized = virtualized + 1;
                }
                else
                {
                    // Container is empty and IDLE -- delete any leftover .lfv
                    // from a previous restore (safety net cleanup)
                    string lfvPath = LFV_Paths.GetContainerPath(state.m_StorageId);
                    if (FileExist(lfvPath))
                    {
                        LFV_FileStorage.DeleteContainerFiles(state.m_StorageId);
                        string cleanMsg = "OnMissionFinish: cleaned leftover .lfv for empty container ";
                        cleanMsg = cleanMsg + state.m_StorageId;
                        LFV_Log.Info(cleanMsg);
                    }
                }
            }
        }

        if (virtualized > 0)
        {
            string savedMsg = "OnMissionFinish -- saved ";
            savedMsg = savedMsg + virtualized.ToString();
            savedMsg = savedMsg + " containers";
            LFV_Log.Info(savedMsg);
        }

        if (m_PersistentIdToStorageId.Count() > 0)
        {
            LFV_IdMap.Save(m_PersistentIdToStorageId, m_ContainerStates);
        }

        LFV_Stats.LogSummary();
        super.OnMissionFinish(sender, args);
    }

    // Settings sync moved to 5_Mission/MissionServer.c (InvokeOnConnect)
    // using vanilla RPCSingleParam pattern (BBP-style).

    // -----------------------------------------------------------
    // ScanAndReconcile -- find all containers, reconcile .lfv files
    // -----------------------------------------------------------
    void ScanAndReconcile()
    {
        int startTime = GetGame().GetTime();

        // A6 audit: pre-build inverse map (storageId -> container)
        // to avoid O(n^2) lookup during reconciliation
        ref map<string, ItemBase> sidToContainer = new map<string, ItemBase>();
        for (int t = 0; t < m_ContainerStates.Count(); t = t + 1)
        {
            LFV_ContainerState ts = m_ContainerStates.GetElement(t);
            ItemBase tc = m_ContainerStates.GetKey(t);
            if (ts && tc && ts.m_StorageId != "")
                sidToContainer.Set(ts.m_StorageId, tc);
        }

        // Scan .lfv files on disk
        string fileName;
        FileAttr fileAttr;
        string searchPath = LFV_Paths.STORAGE_DIR;
        searchPath = searchPath + "/*";
        searchPath = searchPath + LFV_Paths.FILE_EXT;
        FindFileHandle handle = FindFile(searchPath, fileName, fileAttr, 0);
        int reconciledCount = 0;

        if (handle)
        {
            bool keepGoing = true;
            while (keepGoing)
            {
                string fullPath = LFV_Paths.STORAGE_DIR;
                fullPath = fullPath + "/";
                fullPath = fullPath + fileName;
                LFV_ContainerFile fileData;
                if (LFV_FileStorage.LoadHeaderOnly(fullPath, fileData))
                {
                    // A6: O(1) lookup via inverse map
                    ItemBase container = null;
                    if (sidToContainer.Contains(fileData.m_StorageId))
                    {
                        container = sidToContainer.Get(fileData.m_StorageId);
                    }

                    // A5: Layer 1.5 -- use PersistentID from header (survives container moves)
                    if (!container && fileData.m_PersistentId != "")
                    {
                        container = LookupEntityByPidKey(fileData.m_PersistentId);
                    }

                    // Fallback to full search (IdMap + position)
                    if (!container)
                    {
                        container = FindContainerByStorageId(fileData.m_StorageId, fileData.m_ContainerClass, fileData.m_Position);
                    }

                    int newState = LFV_StateMachine.Reconcile(container, fileData);

                    if (container && newState != LFV_State.IDLE)
                    {
                        TrackContainer(container, fileData.m_StorageId, newState);
                    }
                    else if (!container && newState == LFV_State.VIRTUALIZED)
                    {
                        string orphanMsg = "Orphan .lfv found (no container): ";
                        orphanMsg = orphanMsg + fileData.m_StorageId;
                        LFV_Log.Warn(orphanMsg);
                    }
                    reconciledCount = reconciledCount + 1;
                }
                keepGoing = FindNextFile(handle, fileName, fileAttr);
            }
            CloseFindFile(handle);
        }

        int duration = GetGame().GetTime() - startTime;
        string scanMsg = "ScanAndReconcile complete -- ";
        scanMsg = scanMsg + reconciledCount.ToString();
        scanMsg = scanMsg + " files processed in ";
        scanMsg = scanMsg + duration.ToString();
        scanMsg = scanMsg + "ms";
        LFV_Log.Info(scanMsg);
    }

    // -----------------------------------------------------------
    // Find container by storageId, classname, position
    // -----------------------------------------------------------
    override ItemBase FindContainerByStorageId(string storageId, string containerClass, vector position)
    {
        // First check if any tracked container has this storageId
        for (int i = 0; i < m_ContainerStates.Count(); i = i + 1)
        {
            LFV_ContainerState state = m_ContainerStates.GetElement(i);
            if (state.m_StorageId == storageId)
                return m_ContainerStates.GetKey(i);
        }

        // For LFV barrels: look by storageId stored in OnStoreLoad
        // (handled when barrels report their storageId after loading)

        // Layer 2: IdMap reverse lookup -- find entity by PersistentID
        // The IdMap maps PersistentID -> StorageId. We need to find
        // which PersistentID maps to our storageId, then look up entity.
        for (int j = 0; j < m_PersistentIdToStorageId.Count(); j = j + 1)
        {
            string mappedSid = m_PersistentIdToStorageId.GetElement(j);
            if (mappedSid == storageId)
            {
                // Found the PersistentID key -- parse it back to 4 ints
                string pidKey = m_PersistentIdToStorageId.GetKey(j);
                ItemBase idMapEntity = LookupEntityByPidKey(pidKey);
                if (idMapEntity)
                    return idMapEntity;
                break;
            }
        }

        // Layer 3: Fallback -- classname + position +-1m
        return FindContainerByClassAndPosition(containerClass, position, 1.0);
    }

    // -----------------------------------------------------------
    // Find container by classname + position (Layer 2 fallback)
    // -----------------------------------------------------------
    ItemBase FindContainerByClassAndPosition(string containerClass, vector position, float tolerance)
    {
        // Build AABB box from center + tolerance
        // Vanilla pattern: DayZPlayerUtils.SceneGetEntitiesInBox (see universaltemperaturesourcelambdabaseimpl.c)
        vector halfVec = Vector(tolerance, tolerance, tolerance);
        vector minPos = position - halfVec;
        vector maxPos = position + halfVec;

        array<EntityAI> nearEntities = new array<EntityAI>();
        DayZPlayerUtils.SceneGetEntitiesInBox(minPos, maxPos, nearEntities, QueryFlags.DYNAMIC);

        for (int i = 0; i < nearEntities.Count(); i = i + 1)
        {
            ItemBase item = ItemBase.Cast(nearEntities[i]);
            if (item && item.GetType() == containerClass)
                return item;
        }
        return null;
    }

    // -----------------------------------------------------------
    // Track a container in the module
    // -----------------------------------------------------------
    void TrackContainer(ItemBase container, string storageId, int initialState)
    {
        if (m_ContainerStates.Contains(container))
            return;

        LFV_ContainerState state = new LFV_ContainerState();
        state.m_StorageId = storageId;
        state.m_State = initialState;
        state.m_IsLFVBarrel = LFV_Registry.IsBarrelType(container);
        state.m_IsActionTriggered = LFV_Registry.IsActionTriggered(container.GetType());
        state.m_LastActivity = GetGame().GetTime();

        m_ContainerStates.Set(container, state);
        LFV_Stats.s_TotalContainers = LFV_Stats.s_TotalContainers + 1;
    }

    // -----------------------------------------------------------
    // C3 fix: Untrack a container when it's destroyed/deleted.
    // Removes from m_ContainerStates and IdMap to prevent
    // stale references and memory leaks.
    // -----------------------------------------------------------
    override void UntrackContainer(ItemBase container)
    {
        if (!container) return;
        if (!m_ContainerStates.Contains(container)) return;

        LFV_ContainerState state = m_ContainerStates.Get(container);

        // Cancel any active/pending queue for this container
        CancelQueueForContainer(container);

        // Remove from IdMap if present
        if (state && state.m_StorageId != "")
        {
            string pidKey = LFV_IdMap.GetKeyFromEntity(container);
            if (pidKey != "")
            {
                LFV_IdMap.Remove(m_PersistentIdToStorageId, pidKey);
                m_IdMapDirtyCount = m_IdMapDirtyCount + 1;
            }
        }

        m_ContainerStates.Remove(container);

        // A4 audit: keep stat accurate
        LFV_Stats.s_TotalContainers = LFV_Stats.s_TotalContainers - 1;
        if (LFV_Stats.s_TotalContainers < 0)
            LFV_Stats.s_TotalContainers = 0;
    }

    // -----------------------------------------------------------
    // C3 fix: Sweep stale references from m_ContainerStates.
    // Called periodically from AutoCloseCheck (every 10s).
    // Removes entries where the entity is null (deleted by engine).
    // -----------------------------------------------------------
    void SweepStaleContainers()
    {
        int removed = 0;
        for (int i = m_ContainerStates.Count() - 1; i >= 0; i = i - 1)
        {
            ItemBase container = m_ContainerStates.GetKey(i);
            if (!container)
            {
                m_ContainerStates.RemoveElement(i);
                removed = removed + 1;
            }
        }

        if (removed > 0)
        {
            string msg = "SweepStaleContainers: removed ";
            msg = msg + removed.ToString();
            msg = msg + " stale entries";
            LFV_Log.Info(msg);
        }
    }

    // -----------------------------------------------------------
    // Resolve the correct StorageId for a container.
    // Checks IdMap first (PersistentID -> StorageId mapping)
    // and verifies a .lfv file exists. Falls back to generating
    // a new ID if no mapping or file found.
    // Fix B: prevents race condition where new random IDs
    // don't match existing .lfv files after server restart.
    // -----------------------------------------------------------
    string ResolveStorageId(ItemBase container)
    {
        string pidKey = LFV_IdMap.GetKeyFromEntity(container);
        if (pidKey != "")
        {
            string existingSid = LFV_IdMap.Lookup(m_PersistentIdToStorageId, pidKey);
            if (existingSid != "")
            {
                string lfvPath = LFV_Paths.GetContainerPath(existingSid);
                if (FileExist(lfvPath))
                {
                    string resolveMsg = "ResolveStorageId: found existing ";
                    resolveMsg = resolveMsg + existingSid;
                    resolveMsg = resolveMsg + " for ";
                    resolveMsg = resolveMsg + container.GetType();
                    LFV_Log.Info(resolveMsg);
                    return existingSid;
                }
            }
        }

        return GenerateStorageId(container);
    }

    // -----------------------------------------------------------
    // Resolve the initial state for a container based on whether
    // a .lfv file exists for the given storageId.
    // Fix B: newly tracked containers start as VIRTUALIZED if
    // they have an existing .lfv file (pending restore).
    // -----------------------------------------------------------
    int ResolveInitialState(string storageId)
    {
        string lfvPath = LFV_Paths.GetContainerPath(storageId);
        if (FileExist(lfvPath))
            return LFV_State.VIRTUALIZED;
        return LFV_State.IDLE;
    }

    // -----------------------------------------------------------
    // Generate a unique StorageId
    // -----------------------------------------------------------
    static string GenerateStorageId(ItemBase container)
    {
        vector pos = container.GetPosition();
        int px = Math.Round(pos[0]);
        int py = Math.Round(pos[1]);
        int pz = Math.Round(pos[2]);
        int time = GetGame().GetTime();
        int rnd = Math.RandomInt(100000, 999999);

        string sid = "lfv_";
        sid = sid + px.ToString();
        sid = sid + "_";
        sid = sid + py.ToString();
        sid = sid + "_";
        sid = sid + pz.ToString();
        sid = sid + "_";
        sid = sid + time.ToString();
        sid = sid + "_";
        sid = sid + rnd.ToString();
        return sid;
    }

    // -----------------------------------------------------------
    // OnOpenContainer -- called when barrel is opened
    // -----------------------------------------------------------
    override void OnOpenContainer(ItemBase container, PlayerBase player)
    {
        if (!container) return;
        if (m_IsShuttingDown) return;

        LFV_ContainerState state = GetContainerState(container);
        if (!state)
        {
            // Fix B: resolve existing StorageId from IdMap before generating new
            string sid = ResolveStorageId(container);
            int initState = ResolveInitialState(sid);
            TrackContainer(container, sid, initState);
            state = GetContainerState(container);
        }

        state.m_LastActivity = GetGame().GetTime();

        // A2 fix: reject open during active processing (RESTORING/VIRTUALIZING)
        if (state.m_IsProcessing)
        {
            if (player)
            {
                string procKey = "#STR_LFV_Processing";
                string busyMsg = Widget.TranslateString(procKey);
                SendMessageToPlayer(player, busyMsg);
            }
            return;
        }

        // If virtualized, enqueue restore + show manifest
        if (state.m_State == LFV_State.VIRTUALIZED)
        {
            // Send manifest preview to player (barrels + action-triggered)
            if (player)
            {
                string manifest = "";
                if (state.m_IsLFVBarrel)
                {
                    LFV_Barrel_Base barrel = LFV_Barrel_Base.Cast(container);
                    if (barrel)
                        manifest = barrel.LFV_GetManifest();
                }
                else
                {
                    manifest = state.m_Manifest;
                }

                if (manifest != "")
                {
                    string formatted = FormatManifestForChat(manifest);
                    string chatMsg = "[VStorage] ";
                    chatMsg = chatMsg + formatted;
                    SendMessageToPlayer(player, chatMsg);
                }
            }

            // Check no existing queue for this container
            if (!HasQueueForContainer(container))
            {
                LFV_RestoreQueue queue = new LFV_RestoreQueue(container, state, m_Settings.m_BatchSize);
                EnqueueOperation(queue);
            }
        }
    }

    // -----------------------------------------------------------
    // OnCloseContainer -- called when barrel is closed
    // -----------------------------------------------------------
    override void OnCloseContainer(ItemBase container, PlayerBase player)
    {
        if (!container) return;
        if (m_IsShuttingDown) return;

        LFV_ContainerState state = GetContainerState(container);
        if (!state)
        {
            // Fix B: resolve existing StorageId from IdMap before generating new
            string sid = ResolveStorageId(container);
            int initState = ResolveInitialState(sid);
            TrackContainer(container, sid, initState);
            state = GetContainerState(container);
        }

        state.m_LastActivity = GetGame().GetTime();

        // A2 fix: reject close during active processing
        if (state.m_IsProcessing)
        {
            if (player)
            {
                string procKey2 = "#STR_LFV_Processing";
                string busyMsg2 = Widget.TranslateString(procKey2);
                SendMessageToPlayer(player, busyMsg2);
            }
            return;
        }

        // Virtualize if has items
        if (state.m_State == LFV_State.IDLE && LFV_StateMachine.HasCargoOrAttachments(container))
        {
            // Check no existing queue for this container
            if (!HasQueueForContainer(container))
            {
                LFV_VirtualizeQueue queue = new LFV_VirtualizeQueue(container, state, m_Settings.m_BatchSize, true);
                EnqueueOperation(queue);
            }
        }
    }

    // Overload for Close() override in barrel (no player param)
    override void OnCloseContainer(ItemBase container)
    {
        OnCloseContainer(container, null);
    }

    // -----------------------------------------------------------
    // OnContainerDestroyed -- barrel killed while VIRTUALIZED or
    // RESTORING (edge cases 8 + Sprint 4 #5)
    //
    // VIRTUALIZED: load .lfv -> drop all on ground (existing)
    // RESTORING: cancel active RestoreQueue -> partially spawned
    //   items are persistent; ClearPhantomItems removes them.
    //   Collect them + load remaining from .lfv -> drop all on ground.
    // -----------------------------------------------------------
    void OnContainerDestroyed(ItemBase container)
    {
        if (!container) return;
        if (m_IsShuttingDown) return;

        LFV_ContainerState state = GetContainerState(container);
        if (!state) return;

        // Hoist all variables before conditionals (Enforce Script scope rule)
        LFV_DropQueue destroyDropQueue = null;
        LFV_Queue vQueue = null;
        int vqcIdx = 0;
        string virtDestroyMsg = "";

        if (state.m_State == LFV_State.VIRTUALIZED && state.m_HasItems)
        {
            // Drop virtual items on ground
            destroyDropQueue = new LFV_DropQueue(container, state, m_Settings.m_BatchSize);
            EnqueueOperation(destroyDropQueue);
        }
        else if (state.m_State == LFV_State.VIRTUALIZING)
        {
            // Barrel destroyed during virtualization. The .lfv is already
            // saved (PrepareVirtualization is synchronous in OnStart), but
            // items are still being deleted batch by batch.
            //
            // Accelerate the VirtualizeQueue to completion. CompleteNow()
            // finishes deleting ALL remaining items in one tick, then
            // transitions to VIRTUALIZED via OnComplete. After that,
            // DropQueue safely spawns from .lfv with zero duplication risk.
            for (int vq = m_ActiveQueues.Count() - 1; vq >= 0; vq = vq - 1)
            {
                vQueue = m_ActiveQueues[vq];
                if (vQueue && vQueue.GetContainer() == container && vQueue.GetQueueType() == LFV_QueueType.VIRTUALIZE)
                {
                    vQueue.CompleteNow();
                    vqcIdx = m_QueuedContainers.Find(container);
                    if (vqcIdx >= 0)
                        m_QueuedContainers.Remove(vqcIdx);
                    m_ActiveQueues.Remove(vq);
                    break;
                }
            }

            // State is now VIRTUALIZED (VirtualizeQueue.OnComplete handled it)
            if (state.m_HasItems)
            {
                destroyDropQueue = new LFV_DropQueue(container, state, m_Settings.m_BatchSize);
                EnqueueOperation(destroyDropQueue);
            }

            virtDestroyMsg = "Barrel destroyed during VIRTUALIZING -- completed + dropping items: ";
            virtDestroyMsg = virtDestroyMsg + state.m_StorageId;
            LFV_Log.Warn(virtDestroyMsg);
        }
        else if (state.m_State == LFV_State.RESTORING)
        {
            // Cancel active RestoreQueue -- this clears phantom items
            // and reverts state to VIRTUALIZED
            CancelQueueForContainer(container);

            // Now state is VIRTUALIZED again -- enqueue DropQueue
            // to spawn remaining items on ground from .lfv
            if (state.m_HasItems)
            {
                destroyDropQueue = new LFV_DropQueue(container, state, m_Settings.m_BatchSize);
                EnqueueOperation(destroyDropQueue);
            }
        }
    }

    // -----------------------------------------------------------
    // Cancel any active/pending queue for a container
    // Returns true if a queue was found and cancelled
    // -----------------------------------------------------------
    bool CancelQueueForContainer(ItemBase container)
    {
        if (!container) return false;
        bool found = false;

        // Check active queues
        for (int i = m_ActiveQueues.Count() - 1; i >= 0; i = i - 1)
        {
            LFV_Queue queue = m_ActiveQueues[i];
            if (queue && queue.GetContainer() == container)
            {
                queue.Cancel();
                m_ActiveQueues.Remove(i);
                found = true;
            }
        }

        // Check pending queues
        for (int j = m_PendingQueues.Count() - 1; j >= 0; j = j - 1)
        {
            LFV_Queue pQueue = m_PendingQueues[j];
            if (pQueue && pQueue.GetContainer() == container)
            {
                m_PendingQueues.Remove(j);
                found = true;
            }
        }

        if (found)
        {
            int qIdx = m_QueuedContainers.Find(container);
            if (qIdx >= 0)
                m_QueuedContainers.Remove(qIdx);
        }

        return found;
    }

    // -----------------------------------------------------------
    // PrepareVirtualization -- shared logic (M7 audit)
    //
    // Does everything up to and including disk write + SyncVars.
    // Returns data on success, null on failure.
    // Caller handles item deletion (sync vs async).
    // -----------------------------------------------------------
    override LFV_ContainerFile PrepareVirtualization(ItemBase container, LFV_ContainerState state, bool doBackupRotation)
    {
        if (!LFV_StateMachine.CanVirtualize(container, state))
            return null;

        LFV_StateMachine.Transition(state, LFV_State.VIRTUALIZING);

        // Sync processing SyncVar for client-side action locking
        LFV_Barrel_Base procBarrel = LFV_Barrel_Base.Cast(container);
        if (procBarrel)
            procBarrel.LFV_SetIsProcessing(true);

        string sid = state.m_StorageId;

        // Build item tree from live container
        LFV_ContainerFile data = BuildContainerFile(container, state);

        // Purge blacklisted + rotten BEFORE counting/saving
        PurgeBlacklisted(data);

        // Recount and rebuild manifest after purge so .lfv, SyncVars,
        // and JSON sidecar reflect the actual post-purge item set
        data.m_TotalItemCount = LFV_FileStorage.CountItemsRecursive(data.m_Items, 0);

        // Gate: reject virtualization if item count exceeds configured limit
        int maxItems = m_Settings.m_MaxItemsPerContainer;
        if (maxItems > 0 && data.m_TotalItemCount > maxItems)
        {
            string limitMsg = "PrepareVirtualization: ";
            limitMsg = limitMsg + data.m_TotalItemCount.ToString();
            limitMsg = limitMsg + " items exceeds limit of ";
            limitMsg = limitMsg + maxItems.ToString();
            limitMsg = limitMsg + " for ";
            limitMsg = limitMsg + sid;
            LFV_Log.Warn(limitMsg);
            LFV_StateMachine.Transition(state, LFV_State.IDLE);
            if (procBarrel)
                procBarrel.LFV_SetIsProcessing(false);
            return null;
        }

        if (m_Settings.m_ManifestEnabled)
        {
            data.m_Manifest = BuildManifest(data.m_Items);
            state.m_Manifest = data.m_Manifest;
        }

        // Backup rotation
        if (doBackupRotation && m_Settings.m_BackupRotations > 0)
        {
            LFV_FileStorage.RotateBackups(state.m_StorageId);
        }

        // Write to disk (atomic)
        bool saved = LFV_FileStorage.AtomicSave(state.m_StorageId, data);
        if (!saved)
        {
            string saveErrMsg = "PrepareVirtualization: save failed for ";
            saveErrMsg = saveErrMsg + state.m_StorageId;
            LFV_Log.Error(saveErrMsg);
            LFV_StateMachine.Transition(state, LFV_State.IDLE);
            // Reset IsProcessing -- otherwise barrel is permanently locked
            if (procBarrel)
                procBarrel.LFV_SetIsProcessing(false);
            return null;
        }

        // JSON sidecar
        if (m_Settings.m_AdminSidecarJson)
        {
            SaveAdminJson(data);
        }

        // Update SyncVars for LFV barrels / IdMap for vanilla
        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(container);
        if (lfvBarrel)
        {
            lfvBarrel.LFV_SetItemCount(data.m_TotalItemCount);
            lfvBarrel.LFV_SetHasItems(true);
            lfvBarrel.LFV_SetManifest(data.m_Manifest);
        }
        else
        {
            RegisterInIdMap(container, state.m_StorageId);
        }

        // Send pre-formatted manifest to nearby clients via RPC
        SendManifestRPC(container, data.m_Manifest, data.m_TotalItemCount);

        return data;
    }

    // -----------------------------------------------------------
    // VirtualizeSynchronous -- full virtualize in one tick
    // Uses PrepareVirtualization + synchronous item deletion.
    // -----------------------------------------------------------
    void VirtualizeSynchronous(ItemBase container, LFV_ContainerState state, bool doBackupRotation)
    {
        int startMs = GetGame().GetTime();

        LFV_ContainerFile data = PrepareVirtualization(container, state, doBackupRotation);
        if (!data) return;

        // Synchronous deletion
        DeleteAllItems(container);

        // Reset IsProcessing (PrepareVirtualization set it to true)
        LFV_Barrel_Base syncBarrel = LFV_Barrel_Base.Cast(container);
        if (syncBarrel)
            syncBarrel.LFV_SetIsProcessing(false);

        // Finalize state
        LFV_StateMachine.Transition(state, LFV_State.VIRTUALIZED);
        state.m_HasItems = true;

        // Stats
        float durationMs = GetGame().GetTime() - startMs;
        LFV_Stats.RecordVirtualize(durationMs);
        LFV_Stats.s_TotalVirtualizedItems = LFV_Stats.s_TotalVirtualizedItems + data.m_TotalItemCount;

        string vMsg = "Virtualized ";
        vMsg = vMsg + data.m_TotalItemCount.ToString();
        vMsg = vMsg + " items from ";
        vMsg = vMsg + data.m_ContainerClass;
        vMsg = vMsg + " in ";
        vMsg = vMsg + durationMs.ToString();
        vMsg = vMsg + "ms";
        LFV_Log.Info(vMsg);
    }

    // -----------------------------------------------------------
    // Build container file from live world state
    // -----------------------------------------------------------
    override LFV_ContainerFile BuildContainerFile(ItemBase container, LFV_ContainerState state)
    {
        LFV_ContainerFile data = new LFV_ContainerFile();
        data.m_StorageId = state.m_StorageId;
        data.m_ContainerClass = container.GetType();
        data.m_State = LFV_State.VIRTUALIZED;
        data.m_Timestamp = GetGame().GetTime();
        data.m_Position = container.GetPosition();
        data.m_OwnerUID = "";
        data.m_PersistentId = LFV_IdMap.GetKeyFromEntity(container);

        // Build item tree from container inventory
        GameInventory inv = container.GetInventory();
        if (inv)
        {
            // Attachments
            int attCount = inv.AttachmentCount();
            for (int a = 0; a < attCount; a = a + 1)
            {
                EntityAI attEnt = inv.GetAttachmentFromIndex(a);
                if (!attEnt) continue;
                ItemBase attItem = ItemBase.Cast(attEnt);
                if (attItem)
                {
                    int attSlot = inv.GetAttachmentSlotId(a);
                    LFV_ItemRecord attRec = LFV_ItemRecord.FromItem(attItem, LFV_InvType.ATTACHMENT, 0, 0, 0, attSlot, false, 0);
                    if (attRec)
                        data.m_Items.Insert(attRec);
                }
            }

            // Cargo
            CargoBase cargo = inv.GetCargo();
            if (cargo)
            {
                int cargoCount = cargo.GetItemCount();

                for (int c = 0; c < cargoCount; c = c + 1)
                {
                    EntityAI cargoEnt = cargo.GetItem(c);
                    if (!cargoEnt) continue;
                    ItemBase cargoItem = ItemBase.Cast(cargoEnt);
                    if (cargoItem)
                    {
                        GameInventory cargoInv = cargoItem.GetInventory();
                        if (!cargoInv) continue;
                        InventoryLocation cargoLoc = new InventoryLocation();
                        // LOC-FIX: fallback to default position if GetCurrentInventoryLocation fails.
                        // Vanilla can return false for freshly placed items or mod items with
                        // non-standard inventory. Previously these items were silently DROPPED,
                        // causing data loss (server logs confirmed: 3 items captured, 1 saved).
                        // SpawnWithFallback handles inexact positions via FindFirstFreeLocation
                        // and brute-force cargo grid scan.
                        int cRow = 0;
                        int cCol = 0;
                        int cIdx = c;
                        bool cFlip = false;
                        if (cargoInv.GetCurrentInventoryLocation(cargoLoc))
                        {
                            cRow = cargoLoc.GetRow();
                            cCol = cargoLoc.GetCol();
                            cIdx = cargoLoc.GetIdx();
                            cFlip = cargoLoc.GetFlip();
                        }
                        else
                        {
                            string locWarn = "GetCurrentInventoryLocation failed for cargo[";
                            locWarn = locWarn + c.ToString();
                            locWarn = locWarn + "]: ";
                            locWarn = locWarn + cargoItem.GetType();
                            locWarn = locWarn + " -- using fallback position";
                            LFV_Log.Warn(locWarn);
                        }
                        LFV_ItemRecord cargoRec = LFV_ItemRecord.FromItem(cargoItem, LFV_InvType.CARGO, cRow, cCol, cIdx, -1, cFlip, 0);
                        if (cargoRec)
                            data.m_Items.Insert(cargoRec);
                    }
                }
            }
        }

        // Count and manifest are computed by PrepareVirtualization AFTER
        // PurgeBlacklisted, so we skip them here to avoid redundant work.
        // VirtualizeSynchronous also goes through PrepareVirtualization.

        return data;
    }

    // -----------------------------------------------------------
    // Build manifest string: "M4A1:2|Ammo_556x45:3|..."
    // -----------------------------------------------------------
    string BuildManifest(array<ref LFV_ItemRecord> items)
    {
        map<string, int> counts = new map<string, int>();
        CountClassnames(items, counts);

        // Sort by count (simple selection -- max ManifestMaxItems entries)
        string manifest = "";
        int added = 0;
        int maxItems = m_Settings.m_ManifestMaxItems;

        while (added < maxItems && counts.Count() > 0)
        {
            string topClass = "";
            int topCount = 0;

            for (int i = 0; i < counts.Count(); i = i + 1)
            {
                string key = counts.GetKey(i);
                int val = counts.GetElement(i);
                if (val > topCount)
                {
                    topCount = val;
                    topClass = key;
                }
            }

            if (topClass == "") break;

            if (added > 0)
                manifest = manifest + "|";
            manifest = manifest + topClass;
            manifest = manifest + ":";
            manifest = manifest + topCount.ToString();
            counts.Remove(topClass);
            added = added + 1;
        }

        return manifest;
    }

    // -----------------------------------------------------------
    // Send pre-formatted manifest string to clients via RPC
    // on the container entity. Clients in network bubble receive it
    // and cache it for action text display.
    // -----------------------------------------------------------
    void SendManifestRPC(ItemBase container, string rawManifest, int totalCount)
    {
        if (!container) return;
        if (!m_Settings.m_ManifestEnabled) return;

        // Build display string: "15 items: 3x AK-101, 2x M4A1"
        string displayStr = totalCount.ToString();
        displayStr = displayStr + " items";

        string formatted = FormatManifestForChat(rawManifest);
        if (formatted != "")
        {
            displayStr = displayStr + ": ";
            displayStr = displayStr + formatted;
        }

        Param1<string> param = new Param1<string>(displayStr);
        GetGame().RPCSingleParam(container, LFV_RPC.MANIFEST_UPDATE, param, true, null);
    }

    // -----------------------------------------------------------
    // Format manifest for chat display (Sprint 4, #7)
    // Converts "AK101:3|M4A1:2" -> "3x AK-101, 2x M4-A1"
    // Uses displayName from CfgVehicles if available
    // -----------------------------------------------------------
    string FormatManifestForChat(string rawManifest)
    {
        if (rawManifest == "") return "";

        string result = "";
        int pos = 0;
        int len = rawManifest.Length();
        int entryCount = 0;

        // Parse entries separated by "|"
        while (pos < len)
        {
            // Find next "|" or end of string
            int pipePos = rawManifest.IndexOfFrom(pos, "|");
            string entry = "";
            if (pipePos == -1)
            {
                entry = rawManifest.Substring(pos, len - pos);
                pos = len;
            }
            else
            {
                entry = rawManifest.Substring(pos, pipePos - pos);
                pos = pipePos + 1;
            }

            if (entry == "") continue;

            // Parse "Classname:Count"
            string colonChar = ":";
            int colonPos = entry.IndexOf(colonChar);
            if (colonPos == -1) continue;

            string classname = entry.Substring(0, colonPos);
            string countStr = entry.Substring(colonPos + 1, entry.Length() - colonPos - 1);

            // Get displayName from config
            string displayName = GetDisplayName(classname);

            // Build formatted entry: "3x AK-101"
            if (entryCount > 0)
                result = result + ", ";
            result = result + countStr;
            result = result + "x ";
            result = result + displayName;
            entryCount = entryCount + 1;
        }

        return result;
    }

    // -----------------------------------------------------------
    // Get display name for a classname from CfgVehicles
    // Falls back to classname if not found
    // -----------------------------------------------------------
    string GetDisplayName(string classname)
    {
        string cfgPath = "CfgVehicles ";
        cfgPath = cfgPath + classname;
        cfgPath = cfgPath + " displayName";

        string displayName = "";
        GetGame().ConfigGetText(cfgPath, displayName);

        if (displayName == "")
            return classname;

        return displayName;
    }

    void CountClassnames(array<ref LFV_ItemRecord> items, map<string, int> counts)
    {
        if (!items) return;
        for (int i = 0; i < items.Count(); i = i + 1)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec) continue;
            int current = 0;
            if (counts.Contains(rec.m_Classname))
                current = counts.Get(rec.m_Classname);
            counts.Set(rec.m_Classname, current + 1);
            CountClassnames(rec.m_Attachments, counts);
            CountClassnames(rec.m_Cargo, counts);
        }
    }

    // -----------------------------------------------------------
    // Purge blacklisted + optionally rotten items from tree
    // -----------------------------------------------------------
    override void PurgeBlacklisted(LFV_ContainerFile data)
    {
        bool purgeDecay = !m_Settings.m_VirtualizeDecayItems;
        PurgeFromArray(data.m_Items, purgeDecay);
    }

    void PurgeFromArray(array<ref LFV_ItemRecord> items, bool purgeDecay)
    {
        if (!items) return;
        for (int i = items.Count() - 1; i >= 0; i = i - 1)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec)
            {
                items.Remove(i);
                continue;
            }
            if (LFV_Registry.IsBlacklistedItem(rec.m_Classname))
            {
                string purgeMsg = "PURGED: ";
                purgeMsg = purgeMsg + rec.m_Classname;
                LFV_Log.Info(purgeMsg);
                items.Remove(i);
                LFV_Stats.s_PurgedItems = LFV_Stats.s_PurgedItems + 1;
            }
            else if (purgeDecay && rec.m_FoodStage == FoodStageType.ROTTEN)
            {
                string decayMsg = "PURGED (rotten): ";
                decayMsg = decayMsg + rec.m_Classname;
                LFV_Log.Info(decayMsg);
                items.Remove(i);
                LFV_Stats.s_PurgedItems = LFV_Stats.s_PurgedItems + 1;
            }
            else
            {
                PurgeFromArray(rec.m_Attachments, purgeDecay);
                PurgeFromArray(rec.m_Cargo, purgeDecay);
            }
        }
    }

    // -----------------------------------------------------------
    // Delete all items from a container (world deletion)
    // -----------------------------------------------------------
    void DeleteAllItems(ItemBase container)
    {
        if (!container) return;
        GameInventory inv = container.GetInventory();
        if (!inv) return;

        // Attachments (backward)
        for (int i = inv.AttachmentCount() - 1; i >= 0; i = i - 1)
        {
            EntityAI att = inv.GetAttachmentFromIndex(i);
            if (att)
                GetGame().ObjectDelete(att);
        }

        // Cargo (backward)
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            for (int j = cargo.GetItemCount() - 1; j >= 0; j = j - 1)
            {
                EntityAI cargoItem = cargo.GetItem(j);
                if (cargoItem)
                    GetGame().ObjectDelete(cargoItem);
            }
        }
    }

    // -----------------------------------------------------------
    // Save admin JSON sidecar
    //
    // JSON-FIX v2: uses LFV_ContainerFileJson -- a mirror class
    // with ONLY JSON-safe types (no ItemBase, no vector).
    //
    // Root cause: JsonFileLoader hard-crashes on native engine types
    // (ItemBase) during C++ reflection, even when the field is:
    //   - protected (access modifier ignored by serializer)
    //   - null (engine reflects the FIELD TYPE, not the value)
    // Confirmed by server logs: crash at JsonSaveFile with no VM trace.
    // ClearItemRefs(null values) did NOT fix it -- the type itself crashes.
    // -----------------------------------------------------------
    override void SaveAdminJson(LFV_ContainerFile data)
    {
        string jsonPath = LFV_Paths.GetContainerJsonPath(data.m_StorageId);

        // JSON-FIX v2: convert to JSON-safe class (no native engine types)
        LFV_ContainerFileJson jsonData = LFV_ContainerFileJson.FromContainerFile(data);
        JsonFileLoader<LFV_ContainerFileJson>.JsonSaveFile(jsonPath, jsonData);
    }

    // -----------------------------------------------------------
    // Rate limiting per-player-per-container
    // -----------------------------------------------------------
    override bool CheckRateLimit(PlayerBase player, ItemBase container)
    {
        if (!player || !player.GetIdentity())
            return true;

        LFV_ContainerState state = GetContainerState(container);
        if (!state) return true;

        string playerUID = player.GetIdentity().GetPlainId();
        int now = GetGame().GetTime();
        int cooldownMs = m_Settings.m_RateLimitCooldown * 1000;

        if (!state.m_PlayerActionTimestamps)
            state.m_PlayerActionTimestamps = new map<string, int>();

        int lastAction = 0;
        if (state.m_PlayerActionTimestamps.Contains(playerUID))
            lastAction = state.m_PlayerActionTimestamps.Get(playerUID);

        if (now - lastAction < cooldownMs)
        {
            string rateKey = "#STR_LFV_RateLimit";
            string waitMsg = Widget.TranslateString(rateKey);
            SendMessageToPlayer(player, waitMsg);
            return false;
        }

        state.m_PlayerActionTimestamps.Set(playerUID, now);
        return true;
    }

    // -----------------------------------------------------------
    // Send message to player
    // -----------------------------------------------------------
    static void SendMessageToPlayer(PlayerBase player, string msg)
    {
        if (!player || msg == "") return;
        if (!player.GetIdentity()) return;
        GetGame().RPCSingleParam(player, ERPCs.RPC_USER_ACTION_MESSAGE,
            new Param1<string>(msg), true, player.GetIdentity());
    }

    // -----------------------------------------------------------
    // Register a barrel that loaded from persistence
    // Called from LFV_Barrel_Base.EEInit after OnStoreLoad
    // -----------------------------------------------------------
    void RegisterBarrelFromPersistence(LFV_Barrel_Base barrel)
    {
        if (!barrel) return;
        string sid = barrel.LFV_GetStorageId();
        if (sid == "")
        {
            sid = GenerateStorageId(barrel);
            barrel.LFV_SetStorageId(sid);
        }

        bool hasVirtualItems = barrel.LFV_GetHasItems();
        int initialState = LFV_State.IDLE;
        if (hasVirtualItems)
        {
            // DUP-FIX: Verify the .lfv actually exists before committing
            // to VIRTUALIZED state. If the .lfv was deleted after a
            // successful restore (anti-duplication) and the server crashed
            // before the engine persisted HasItems=false, we'd end up in
            // a stuck VIRTUALIZED state with no file to restore from.
            string lfvPath = LFV_Paths.GetContainerPath(sid);
            if (FileExist(lfvPath))
            {
                initialState = LFV_State.VIRTUALIZED;
            }
            else
            {
                // No .lfv -> restore completed successfully, items are persistent.
                // If container has items (survived crash), state is correctly IDLE.
                // Reset barrel SyncVars to match: no VIRTUAL items, physical items
                // are present and will be re-virtualized on next close.
                string fixMsg = "RegisterBarrelFromPersistence: HasItems=true but no .lfv for ";
                fixMsg = fixMsg + sid;
                fixMsg = fixMsg + " -- resetting to IDLE (post-restore crash recovery)";
                LFV_Log.Warn(fixMsg);
                barrel.LFV_SetHasItems(false);
                barrel.LFV_SetItemCount(0);
                string emptyManifest = "";
                barrel.LFV_SetManifest(emptyManifest);
                initialState = LFV_State.IDLE;
            }
        }

        TrackContainer(barrel, sid, initialState);
    }

    // -----------------------------------------------------------
    // Auto-register a vanilla container discovered by ProximityMonitor
    // (Sprint 4, #10)
    //
    // For containers in the whitelist that weren't tracked before
    // (e.g. newly placed by player, or loaded by CE after startup).
    // -----------------------------------------------------------
    override void AutoRegisterVanillaContainer(ItemBase container)
    {
        if (!container) return;
        if (m_ContainerStates.Contains(container)) return;

        // Fix B: resolve existing StorageId from IdMap before generating new
        string sid = ResolveStorageId(container);
        int initState = ResolveInitialState(sid);
        TrackContainer(container, sid, initState);

        // Eager-add to ProximityMonitor (skip waiting for ~90s rebuild)
        if (m_ProximityMonitor)
            m_ProximityMonitor.AddContainer(container);

        string msg = "Auto-registered vanilla container: ";
        msg = msg + container.GetType();
        LFV_Log.Info(msg);
    }

    // =========================================================
    // QUEUE MANAGEMENT -- Sprint 2
    // =========================================================

    // -----------------------------------------------------------
    // Enqueue a new operation (virtualize/restore/drop)
    // -----------------------------------------------------------
    void EnqueueOperation(LFV_Queue queue)
    {
        if (!queue) return;

        // M2 fix: reject if pending queue is full
        if (m_PendingQueues.Count() >= m_Settings.m_MaxPendingQueues && m_ActiveQueues.Count() >= m_Settings.m_MaxConcurrentQueues)
        {
            string limitMsg = "Queue limit reached (";
            limitMsg = limitMsg + m_Settings.m_MaxPendingQueues.ToString();
            limitMsg = limitMsg + " pending), rejecting operation";
            LFV_Log.Warn(limitMsg);
            return;
        }

        // Track container in O(1) lookup set
        m_QueuedContainers.Insert(queue.GetContainer());

        // Check if we have room in active queues
        if (m_ActiveQueues.Count() < m_Settings.m_MaxConcurrentQueues)
        {
            m_ActiveQueues.Insert(queue);
            queue.Start();
        }
        else
        {
            // FIFO pending
            m_PendingQueues.Insert(queue);
        }

        // Ensure timer is running
        StartQueueProcessor();
    }

    // -----------------------------------------------------------
    // Start the centralized queue timer
    // -----------------------------------------------------------
    void StartQueueProcessor()
    {
        if (m_QueueTimer)
            return;

        m_QueueTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        float interval = m_Settings.m_BatchInterval;
        interval = interval / 1000.0;  // ms to seconds for Timer.Run
        m_QueueTimer.Run(interval, this, "ProcessQueues", null, true);
    }

    // -----------------------------------------------------------
    // Stop the queue timer
    // -----------------------------------------------------------
    void StopQueueProcessor()
    {
        if (m_QueueTimer)
        {
            m_QueueTimer.Stop();
            m_QueueTimer = null;
        }
    }

    // -----------------------------------------------------------
    // Process all active queues -- called by timer each tick
    // -----------------------------------------------------------
    void ProcessQueues()
    {
        if (m_ActiveQueues.Count() == 0)
        {
            StopQueueProcessor();
            return;
        }

        // Process each active queue
        for (int i = m_ActiveQueues.Count() - 1; i >= 0; i = i - 1)
        {
            LFV_Queue queue = m_ActiveQueues[i];
            if (!queue)
            {
                m_ActiveQueues.Remove(i);
                continue;
            }

            if (queue.IsComplete() || queue.IsCancelled() || queue.IsFailed())
            {
                int qcIdx = m_QueuedContainers.Find(queue.GetContainer());
                if (qcIdx >= 0)
                    m_QueuedContainers.Remove(qcIdx);
                m_ActiveQueues.Remove(i);
                continue;
            }

            if (queue.IsActive())
            {
                queue.ProcessBatch();
            }
        }

        // Promote pending queues to active if slots available
        while (m_PendingQueues.Count() > 0 && m_ActiveQueues.Count() < m_Settings.m_MaxConcurrentQueues)
        {
            LFV_Queue pending = m_PendingQueues[0];
            m_PendingQueues.Remove(0);

            if (pending)
            {
                m_ActiveQueues.Insert(pending);
                pending.Start();
            }
        }

        // Stop timer if no work left
        if (m_ActiveQueues.Count() == 0 && m_PendingQueues.Count() == 0)
        {
            StopQueueProcessor();
        }
    }

    // -----------------------------------------------------------
    // Check if any queue is active for a given container
    // -----------------------------------------------------------
    override bool HasQueueForContainer(ItemBase container)
    {
        return m_QueuedContainers.Find(container) >= 0;
    }

    // =========================================================
    // HELPER METHODS
    // =========================================================

    // -----------------------------------------------------------
    // Does container have virtual items (is VIRTUALIZED)?
    // -----------------------------------------------------------
    override bool HasVirtualItems(ItemBase container)
    {
        LFV_ContainerState state = GetContainerState(container);
        if (!state) return false;
        return state.m_HasItems && state.m_State == LFV_State.VIRTUALIZED;
    }

    // -----------------------------------------------------------
    // Is container being tracked by the module?
    // -----------------------------------------------------------
    override bool IsTracked(ItemBase container)
    {
        return m_ContainerStates.Contains(container);
    }

    // -----------------------------------------------------------
    // Is container currently restoring items?
    // -----------------------------------------------------------
    bool IsRestoring(ItemBase container)
    {
        LFV_ContainerState state = GetContainerState(container);
        if (!state) return false;
        return state.m_State == LFV_State.RESTORING;
    }

    // -----------------------------------------------------------
    // Update last activity timestamp (for auto-close tracking)
    // -----------------------------------------------------------
    void OnContainerActivity(ItemBase container)
    {
        LFV_ContainerState state = GetContainerState(container);
        if (state)
            state.m_LastActivity = GetGame().GetTime();
    }

    // =========================================================
    // IDMAP + CLEANUP -- Sprint 3 Phase C
    // =========================================================

    // -----------------------------------------------------------
    // Register a vanilla container in the IdMap
    // Called after virtualizing a non-LFV_Barrel container
    // -----------------------------------------------------------
    override void RegisterInIdMap(ItemBase container, string storageId)
    {
        if (!container) return;
        if (storageId == "") return;

        string pidKey = LFV_IdMap.GetKeyFromEntity(container);
        if (pidKey == "")
        {
            string pidWarnMsg = "IdMap: no PersistentID for ";
            pidWarnMsg = pidWarnMsg + container.GetType();
            LFV_Log.Warn(pidWarnMsg);
            return;
        }

        LFV_IdMap.Register(m_PersistentIdToStorageId, pidKey, storageId);
        m_IdMapDirtyCount = m_IdMapDirtyCount + 1;
    }

    // -----------------------------------------------------------
    // Save IdMap to disk (call periodically, not every register)
    // -----------------------------------------------------------
    void SaveIdMap()
    {
        LFV_IdMap.Save(m_PersistentIdToStorageId, m_ContainerStates);
        m_IdMapDirtyCount = 0;
    }

    // -----------------------------------------------------------
    // Start/Stop IdMap periodic save timer (Sprint 4, #9)
    // Saves every 300s to protect against server crash
    // -----------------------------------------------------------
    void StartIdMapSaveTimer()
    {
        if (m_IdMapSaveTimer) return;
        m_IdMapSaveTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_IdMapSaveTimer.Run(300.0, this, "OnIdMapSaveTimerTick", null, true);
        string idmTimerMsg = "IdMap periodic save timer started (300s interval)";
        LFV_Log.Info(idmTimerMsg);
    }

    void StopIdMapSaveTimer()
    {
        if (m_IdMapSaveTimer)
        {
            m_IdMapSaveTimer.Stop();
            m_IdMapSaveTimer = null;
        }
    }

    // Called every 300s -- saves if dirty, always sweeps stale refs
    void OnIdMapSaveTimerTick()
    {
        // C3 fix: sweep stale references even if AutoClose is disabled.
        // This timer always runs (300s interval), providing a safety net.
        SweepStaleContainers();

        if (m_IdMapDirtyCount > 0 && m_PersistentIdToStorageId.Count() > 0)
        {
            SaveIdMap();
            string idmSaveMsg = "IdMap periodic save triggered";
            LFV_Log.Info(idmSaveMsg);
        }
    }

    // -----------------------------------------------------------
    // Periodic scan timer -- catches containers placed mid-session
    // far from players that ProximityMonitor can't discover.
    // Default: every 1800s (30 min). Min: 300s.
    // -----------------------------------------------------------
    void StartPeriodicScanTimer()
    {
        if (m_PeriodicScanTimer) return;
        float intervalSec = m_Settings.m_PeriodicScanInterval;
        if (intervalSec < 300) intervalSec = 300;
        m_PeriodicScanTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_PeriodicScanTimer.Run(intervalSec, this, "OnPeriodicScanTick", null, true);
        string msg = "Periodic scan timer started (";
        msg = msg + intervalSec.ToString();
        msg = msg + "s interval)";
        LFV_Log.Info(msg);
    }

    void StopPeriodicScanTimer()
    {
        if (m_PeriodicScanTimer)
        {
            m_PeriodicScanTimer.Stop();
            m_PeriodicScanTimer = null;
        }
    }

    void OnPeriodicScanTick()
    {
        if (m_IsShuttingDown) return;
        string scanTickMsg = "Periodic ScanAndReconcile triggered";
        LFV_Log.Info(scanTickMsg);
        ScanAndReconcile();

        // Safety net: re-virtualize IDLE containers with items that
        // have been idle for >5 minutes. Catches containers that
        // auto-close and proximity monitoring somehow missed,
        // reducing the non-persistent items crash-loss window.
        int idleThresholdMs = 300000;
        int now = GetGame().GetTime();
        int stateCount = m_ContainerStates.Count();
        int requeued = 0;
        for (int si = 0; si < stateCount; si = si + 1)
        {
            ItemBase safetyContainer = m_ContainerStates.GetKey(si);
            LFV_ContainerState safetyState = m_ContainerStates.GetElement(si);
            if (!safetyContainer || !safetyState) continue;
            if (safetyState.m_State != LFV_State.IDLE) continue;
            if (safetyState.m_IsProcessing) continue;
            if (HasQueueForContainer(safetyContainer)) continue;
            if (!LFV_StateMachine.HasCargoOrAttachments(safetyContainer)) continue;

            int elapsed = now - safetyState.m_LastActivity;
            if (elapsed > idleThresholdMs)
            {
                RequestVirtualize(safetyContainer);
                requeued = requeued + 1;
            }
        }
        if (requeued > 0)
        {
            string safetyMsg = "Safety net: re-virtualizing ";
            safetyMsg = safetyMsg + requeued.ToString();
            safetyMsg = safetyMsg + " idle containers with non-persistent items";
            LFV_Log.Info(safetyMsg);
        }
    }

    // -----------------------------------------------------------
    // Lookup entity by PersistentID key string ("b1_b2_b3_b4")
    // Parses key back to 4 ints and calls vanilla API
    //
    // NOTE: Bohemia typo -- "GetEntityByPersitentID" is correct
    // -----------------------------------------------------------
    ItemBase LookupEntityByPidKey(string pidKey)
    {
        if (pidKey == "") return null;

        // Parse "b1_b2_b3_b4" -> 4 ints
        // Split by "_"
        int b1 = 0;
        int b2 = 0;
        int b3 = 0;
        int b4 = 0;

        array<string> parts = new array<string>();
        string separator = "_";
        pidKey.Split(separator, parts);

        if (parts.Count() < 4) return null;

        b1 = parts[0].ToInt();
        b2 = parts[1].ToInt();
        b3 = parts[2].ToInt();
        b4 = parts[3].ToInt();

        // NOTE: Bohemia typo in API -- "Persitent" not "Persistent"
        EntityAI entity = GetGame().GetEntityByPersitentID(b1, b2, b3, b4);
        if (!entity) return null;

        return ItemBase.Cast(entity);
    }

    // -----------------------------------------------------------
    // RunCleanup wrapper -- called via CallLater 120s after load
    // -----------------------------------------------------------
    void RunCleanup()
    {
        LFV_Cleanup.Run(this);
    }

    // =========================================================
    // PROXIMITY MONITOR -- Sprint 3 Phase D
    // =========================================================

    // -----------------------------------------------------------
    // Start proximity monitor for non-barrel containers
    // -----------------------------------------------------------
    void StartProximityMonitor()
    {
        m_ProximityMonitor = new LFV_ProximityMonitor(this);

        float intervalSec = m_Settings.m_ProximityCheckInterval;
        intervalSec = intervalSec / 1000.0;

        m_ProximityTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_ProximityTimer.Run(intervalSec, m_ProximityMonitor, "OnTick", null, true);

        string msg = "ProximityMonitor started (";
        msg = msg + m_Settings.m_ProximityCheckInterval.ToString();
        msg = msg + "ms interval)";
        LFV_Log.Info(msg);
    }

    // -----------------------------------------------------------
    // Stop proximity monitor
    // -----------------------------------------------------------
    void StopProximityMonitor()
    {
        if (m_ProximityTimer)
        {
            m_ProximityTimer.Stop();
            m_ProximityTimer = null;
        }
        m_ProximityMonitor = null;
    }

    // -----------------------------------------------------------
    // Request restore for a container (called by ProximityMonitor)
    // -----------------------------------------------------------
    override void RequestRestore(ItemBase container)
    {
        if (!container) return;
        if (m_IsShuttingDown) return;

        LFV_ContainerState state = GetContainerState(container);
        if (!state) return;

        if (state.m_State != LFV_State.VIRTUALIZED) return;
        if (HasQueueForContainer(container)) return;

        // Update activity timestamp to prevent immediate re-virtualization
        // after restore completes (anti ping-pong with ProximityMonitor)
        state.m_LastActivity = GetGame().GetTime();

        LFV_RestoreQueue queue = new LFV_RestoreQueue(container, state, m_Settings.m_BatchSize);
        EnqueueOperation(queue);

        string msg = "ProximityMonitor: restore requested for ";
        msg = msg + container.GetType();
        LFV_Log.Info(msg);
    }

    // -----------------------------------------------------------
    // Request virtualize for a container (called by ProximityMonitor)
    // -----------------------------------------------------------
    override void RequestVirtualize(ItemBase container)
    {
        if (!container) return;
        if (m_IsShuttingDown) return;

        LFV_ContainerState state = GetContainerState(container);
        if (!state) return;

        if (state.m_State != LFV_State.IDLE) return;
        if (HasQueueForContainer(container)) return;
        if (!LFV_StateMachine.HasCargoOrAttachments(container)) return;

        LFV_VirtualizeQueue queue = new LFV_VirtualizeQueue(container, state, m_Settings.m_BatchSize, true);
        EnqueueOperation(queue);

        string msg = "ProximityMonitor: virtualize requested for ";
        msg = msg + container.GetType();
        LFV_Log.Info(msg);
    }

    // =========================================================
    // ADMIN COMMANDS -- Sprint 4
    // =========================================================

    // -----------------------------------------------------------
    // Check if a UID is in the admin list (A1 audit)
    // Empty list = nobody allowed (secure default)
    // -----------------------------------------------------------
    bool IsAdminUID(string uid)
    {
        if (!m_Settings.m_AdminUIDs)
            return false;
        if (m_Settings.m_AdminUIDs.Count() == 0)
            return false;
        for (int i = 0; i < m_Settings.m_AdminUIDs.Count(); i = i + 1)
        {
            if (m_Settings.m_AdminUIDs[i] == uid)
                return true;
        }
        return false;
    }

    // -----------------------------------------------------------
    // Handle admin command RPC from client
    //
    // Commands: "status", "stats", "cleanup force", "save idmap"
    // Admin check: sender UID must be in m_AdminUIDs (A1 audit)
    // -----------------------------------------------------------
    override void HandleAdminCommandFromRPC(string command, PlayerIdentity sender)
    {
        // A1 audit: verify sender is in admin UID list
        if (!sender)
            return;

        string senderUID = sender.GetPlainId();
        if (!IsAdminUID(senderUID))
        {
            string denyMsg = "Admin command denied for UID: ";
            denyMsg = denyMsg + senderUID;
            LFV_Log.Warn(denyMsg);
            return;
        }

        // M7 audit: normalize command (trim + lowercase)
        command.Trim();
        command.ToLower();

        string logMsg = "Admin command: ";
        logMsg = logMsg + command;
        LFV_Log.Info(logMsg);

        string response = "";

        if (command == "status")
        {
            response = AdminCmdStatus();
        }
        else if (command == "stats")
        {
            response = AdminCmdStats();
        }
        else if (command == "cleanup force")
        {
            AdminCmdCleanupForce();
            response = "Cleanup forced (dry-run OFF). Check server logs.";
        }
        else if (command == "save idmap")
        {
            SaveIdMap();
            response = "IdMap saved to disk.";
        }
        else
        {
            response = "Unknown command. Available: status, stats, cleanup force, save idmap";
        }

        // Send response back to client
        if (sender && response != "")
        {
            AdminSendResponse(sender, response);
        }
    }

    // -----------------------------------------------------------
    // Admin command: status -- overview of mod state
    // -----------------------------------------------------------
    string AdminCmdStatus()
    {
        string r = "=== LFV Status ===";

        string tracked = " Tracked: ";
        tracked = tracked + m_ContainerStates.Count().ToString();
        r = r + tracked;

        // Count by state
        int idle = 0;
        int virtualized = 0;
        int processing = 0;
        for (int i = 0; i < m_ContainerStates.Count(); i = i + 1)
        {
            LFV_ContainerState st = m_ContainerStates.GetElement(i);
            if (!st) continue;
            if (st.m_State == LFV_State.IDLE)
                idle = idle + 1;
            else if (st.m_State == LFV_State.VIRTUALIZED)
                virtualized = virtualized + 1;
            else
                processing = processing + 1;
        }

        string stateInfo = " | IDLE: ";
        stateInfo = stateInfo + idle.ToString();
        stateInfo = stateInfo + " | VIRTUALIZED: ";
        stateInfo = stateInfo + virtualized.ToString();
        stateInfo = stateInfo + " | Processing: ";
        stateInfo = stateInfo + processing.ToString();
        r = r + stateInfo;

        string queueInfo = " | Active queues: ";
        queueInfo = queueInfo + m_ActiveQueues.Count().ToString();
        queueInfo = queueInfo + " | Pending: ";
        queueInfo = queueInfo + m_PendingQueues.Count().ToString();
        r = r + queueInfo;

        string idMapInfo = " | IdMap entries: ";
        idMapInfo = idMapInfo + m_PersistentIdToStorageId.Count().ToString();
        r = r + idMapInfo;

        return r;
    }

    // -----------------------------------------------------------
    // Admin command: stats -- performance statistics
    // -----------------------------------------------------------
    string AdminCmdStats()
    {
        string r = "=== LFV Stats === ";
        r = r + LFV_Stats.GetSummary();
        return r;
    }

    // -----------------------------------------------------------
    // Admin command: cleanup force -- run cleanup in LIVE mode
    // -----------------------------------------------------------
    void AdminCmdCleanupForce()
    {
        // Temporarily override dry-run
        bool wasDryRun = m_Settings.m_CleanupDryRun;
        m_Settings.m_CleanupDryRun = false;
        LFV_Cleanup.Run(this);
        m_Settings.m_CleanupDryRun = wasDryRun;
    }

    // -----------------------------------------------------------
    // Send admin response to client via RPC
    // -----------------------------------------------------------
    void AdminSendResponse(PlayerIdentity identity, string msg)
    {
        // Find player from identity and send via vanilla message system
        int low = 0;
        int high = 0;
        GetGame().GetPlayerNetworkIDByIdentityID(identity.GetPlayerId(), low, high);
        PlayerBase player = PlayerBase.Cast(GetGame().GetObjectByNetworkId(low, high));
        if (player)
            SendMessageToPlayer(player, msg);
    }

    // =========================================================
    // AUTO-CLOSE SYSTEM -- Sprint 3
    // =========================================================

    // -----------------------------------------------------------
    // Start auto-close timer (10s interval)
    // -----------------------------------------------------------
    void StartAutoCloseTimer()
    {
        if (m_AutoCloseTimer)
            return;

        m_AutoCloseTimer = new Timer(CALL_CATEGORY_GAMEPLAY);
        m_AutoCloseTimer.Run(10.0, this, "AutoCloseCheck", null, true);
        string acTimerMsg = "Auto-close timer started (10s interval)";
        LFV_Log.Info(acTimerMsg);
    }

    // -----------------------------------------------------------
    // Stop auto-close timer
    // -----------------------------------------------------------
    void StopAutoCloseTimer()
    {
        if (m_AutoCloseTimer)
        {
            m_AutoCloseTimer.Stop();
            m_AutoCloseTimer = null;
        }
    }

    // -----------------------------------------------------------
    // AutoCloseCheck -- called every 10s by timer
    //
    // For each open tracked container:
    //   - If nobody nearby + ShortDelay exceeded -> Close()
    //   - If someone nearby + LongDelay exceeded -> Close() (AFK)
    //
    // Close() on LFV_Barrel_Base triggers OnCloseContainer ->
    // VirtualizeQueue (existing pipeline).
    // -----------------------------------------------------------
    void AutoCloseCheck()
    {
        if (m_IsShuttingDown) return;

        // C3 fix: sweep stale references every AutoClose tick (10s)
        SweepStaleContainers();

        if (m_ContainerStates.Count() == 0) return;

        int now = GetGame().GetTime();
        float radiusSq = m_Settings.m_AutoCloseRadius * m_Settings.m_AutoCloseRadius;
        int shortDelayMs = m_Settings.m_AutoCloseShortDelay * 1000;
        int longDelayMs = m_Settings.m_AutoCloseLongDelay * 1000;

        // Get player list (reuse array, clear each tick)
        m_AutoClosePlayers.Clear();
        GetGame().GetWorld().GetPlayerList(m_AutoClosePlayers);

        // Collect containers to close (reuse array, avoid modifying map during iteration)
        m_AutoCloseToClose.Clear();

        for (int i = 0; i < m_ContainerStates.Count(); i = i + 1)
        {
            ItemBase container = m_ContainerStates.GetKey(i);
            LFV_ContainerState state = m_ContainerStates.GetElement(i);

            if (!container) continue;
            if (!state) continue;

            // Only IDLE containers can be auto-closed.
            // RESTORED is transient (immediately transitions to IDLE in
            // RestoreQueue.OnComplete), so it's never visible here.
            if (state.m_State != LFV_State.IDLE)
                continue;

            // Must be a barrel with open/close behavior.
            // Action-triggered non-barrels are handled by their action hooks
            // (Close() / IsOpen() not available on ItemBase at compile time).
            Barrel_ColorBase barrel = Barrel_ColorBase.Cast(container);
            if (!barrel) continue;
            if (!barrel.IsOpen()) continue;

            // Skip if in active queue (virtualizing/restoring)
            if (HasQueueForContainer(container)) continue;

            // How long has it been open without activity?
            int elapsed = now - state.m_LastActivity;
            if (elapsed < shortDelayMs) continue;

            // Check if any player is nearby
            bool playerNearby = false;
            vector barrelPos = barrel.GetPosition();

            for (int p = 0; p < m_AutoClosePlayers.Count(); p = p + 1)
            {
                Man man = m_AutoClosePlayers[p];
                if (!man) continue;

                float distSq = vector.DistanceSq(barrelPos, man.GetPosition());
                if (distSq <= radiusSq)
                {
                    playerNearby = true;
                    break;
                }
            }

            // Nobody nearby + ShortDelay exceeded -> close
            if (!playerNearby)
            {
                m_AutoCloseToClose.Insert(container);
                continue;
            }

            // Someone nearby but LongDelay exceeded -> close (AFK)
            if (elapsed >= longDelayMs)
            {
                m_AutoCloseToClose.Insert(container);
            }
        }

        // Close collected containers
        for (int c = 0; c < m_AutoCloseToClose.Count(); c = c + 1)
        {
            ItemBase closeTarget = m_AutoCloseToClose[c];
            Barrel_ColorBase closeBarrel = Barrel_ColorBase.Cast(closeTarget);
            if (closeBarrel)
            {
                string closeMsg = "Auto-close: ";
                closeMsg = closeMsg + closeBarrel.GetType();
                LFV_Log.Info(closeMsg);

                // AC-FIX: For vanilla whitelisted barrels (Barrel_Blue, etc.),
                // Close() does NOT trigger OnCloseContainer (only LFV_Barrel_Base
                // overrides Close()). We must call OnCloseContainer explicitly
                // so items are virtualized before the barrel closes.
                // For LFV barrels, the Close() override handles it via the
                // m_LFV_SkipCloseVirtualize flag to prevent double-call.
                bool isWhitelisted = LFV_Registry.IsVirtualContainer(closeBarrel.GetType());
                LFV_Barrel_Base lfvCloseBarrel = LFV_Barrel_Base.Cast(closeBarrel);

                if (isWhitelisted && !lfvCloseBarrel)
                {
                    if (LFV_StateMachine.HasCargoOrAttachments(closeTarget))
                    {
                        OnCloseContainer(closeTarget, null);
                    }
                }

                closeBarrel.Close();
            }
        }
    }
}
#endif
