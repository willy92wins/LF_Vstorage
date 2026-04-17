// =========================================================
// LF_VStorage -- Queue System (v1.0)
//
// Async batch processing for virtualize/restore/drop operations.
// Base class + 3 concrete queue types.
//
// Design:
//   - VirtualizeQueue: file write sync (OnStart), item deletion batched
//   - RestoreQueue: file load sync (OnStart), item spawning batched
//     with explicit field restore (FORMAT 3)
//   - DropQueue: spawn items on ground in batches (barrel destroyed)
//
// All queues managed centrally by LFV_Module:
//   - Timer fires every BatchInterval ms
//   - MaxConcurrentQueues active, rest in FIFO pending
//   - OnMissionFinish: accelerate virtualizes, cancel restores
//
// RULES:
//   - One queue per container at a time
//   - Top-level items per batch, children complete in same tick
//   - File loaded once in OnStart, data kept in memory for batch processing
// =========================================================

// -----------------------------------------------------------
// Base Queue
// -----------------------------------------------------------
class LFV_Queue
{
    protected ItemBase m_Container;
    protected ref LFV_ContainerState m_ContainerState;
    protected int m_QueueState;
    protected int m_QueueType;
    protected int m_BatchSize;
    protected int m_CurrentIndex;
    protected int m_StartTimeMs;

    // No parameterized constructor -- subclasses assign fields directly.
    // Enforce Script requires child constructors to match parent signature;
    // using default constructor avoids that constraint.

    // --- Lifecycle ---

    void Start()
    {
        m_QueueState = LFV_QueueState.ACTIVE;
        m_StartTimeMs = GetGame().GetTime();
        OnStart();
    }

    // Called each timer tick while active
    void ProcessBatch()
    {
        // Override in subclasses
    }

    // Complete the entire remaining work in one call (shutdown accelerate)
    // safety counter prevents infinite loop if ProcessBatch has a bug
    void CompleteNow()
    {
        int safetyLimit = 10000;
        while (m_QueueState == LFV_QueueState.ACTIVE && safetyLimit > 0)
        {
            ProcessBatch();
            safetyLimit--;
        }
        if (safetyLimit <= 0)
        {
            LFV_Log.Error("CompleteNow: safety limit reached -- forcing COMPLETE");
            m_QueueState = LFV_QueueState.COMPLETE;
        }
    }

    void Cancel()
    {
        m_QueueState = LFV_QueueState.CANCELLED;
        OnCancel();
    }

    // --- State queries ---

    bool IsComplete() { return m_QueueState == LFV_QueueState.COMPLETE; }
    bool IsCancelled() { return m_QueueState == LFV_QueueState.CANCELLED; }
    bool IsActive() { return m_QueueState == LFV_QueueState.ACTIVE; }
    bool IsPending() { return m_QueueState == LFV_QueueState.PENDING; }
    bool IsFailed() { return m_QueueState == LFV_QueueState.FAILED; }
    int GetQueueType() { return m_QueueType; }
    ItemBase GetContainer() { return m_Container; }
    LFV_ContainerState GetContainerState() { return m_ContainerState; }

    // --- Overrides ---

    protected void OnStart() {}
    protected void OnComplete() {}
    protected void OnCancel() {}
}

// -----------------------------------------------------------
// VirtualizeQueue
//
// OnStart: build item tree, purge, rotate backups, atomic write,
//          JSON sidecar, update SyncVars, transition VIRTUALIZING
// ProcessBatch: delete BatchSize items from container per tick
// OnComplete: transition VIRTUALIZED, stats
// -----------------------------------------------------------
class LFV_VirtualizeQueue : LFV_Queue
{
    protected ref array<EntityAI> m_ItemsToDelete;
    protected ref LFV_ContainerFile m_Data;
    protected bool m_DoBackupRotation;

    void LFV_VirtualizeQueue(ItemBase container, LFV_ContainerState state, int batchSize, bool doBackupRotation)
    {
        m_Container = container;
        m_ContainerState = state;
        m_QueueState = LFV_QueueState.PENDING;
        m_QueueType = LFV_QueueType.VIRTUALIZE;
        m_BatchSize = batchSize;
        m_CurrentIndex = 0;
        m_StartTimeMs = 0;
        m_ItemsToDelete = new array<EntityAI>();
        m_DoBackupRotation = doBackupRotation;
    }

    override protected void OnStart()
    {
        // shared logic via PrepareVirtualization
        LFV_Module module = LFV_Module.GetModule();
        if (!module)
        {
            m_QueueState = LFV_QueueState.FAILED;
            return;
        }

        // Phase 5.5: signal we're mid-serialization so EEDelete refuses
        // to purge our state + queue. Cleared in OnComplete / OnCancel.
        m_ContainerState.m_IsVirtualizing = true;

        m_Data = module.PrepareVirtualization(m_Container, m_ContainerState, m_DoBackupRotation);
        if (!m_Data)
        {
            m_ContainerState.m_IsVirtualizing = false;
            m_QueueState = LFV_QueueState.FAILED;
            return;
        }

        // Collect items to delete (async via ProcessBatch)
        CollectAllItems(m_Container);

        string msg = "VirtualizeQueue started: ";
        msg = msg + m_ItemsToDelete.Count().ToString();
        msg = msg + " items to delete for ";
        msg = msg + m_Data.m_ContainerClass;
        LFV_Log.Info(msg);
    }

    // Collect all items from container into flat deletion list
    protected void CollectAllItems(ItemBase container)
    {
        GameInventory inv = container.GetInventory();
        if (!inv) return;

        // Attachments (forward order is fine for collection)
        for (int a = 0; a < inv.AttachmentCount(); a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
                m_ItemsToDelete.Insert(att);
        }

        // Cargo
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            for (int c = 0; c < cargo.GetItemCount(); c++)
            {
                EntityAI cargoItem = cargo.GetItem(c);
                if (cargoItem)
                    m_ItemsToDelete.Insert(cargoItem);
            }
        }
    }

    override void ProcessBatch()
    {
        if (m_QueueState != LFV_QueueState.ACTIVE)
            return;

        // guard against container deleted between batches
        if (!m_Container)
        {
            m_QueueState = LFV_QueueState.COMPLETE;
            OnComplete();
            return;
        }

        // Guard against null or exhausted list
        if (!m_ItemsToDelete || m_CurrentIndex >= m_ItemsToDelete.Count())
        {
            m_QueueState = LFV_QueueState.COMPLETE;
            OnComplete();
            return;
        }

        // Delete up to BatchSize items per tick (forward through pre-collected list)
        int remaining = m_ItemsToDelete.Count() - m_CurrentIndex;
        int batchEnd = m_CurrentIndex + Math.Min(m_BatchSize, remaining);

        for (int i = m_CurrentIndex; i < batchEnd; i++)
        {
            EntityAI item = m_ItemsToDelete[i];
            if (item)
            {
                // Clear reference before delete to avoid dangling access
                m_ItemsToDelete.Set(i, null);
                GetGame().ObjectDelete(item);
            }
        }
        m_CurrentIndex = batchEnd;

        // Check if done
        if (m_CurrentIndex >= m_ItemsToDelete.Count())
        {
            m_QueueState = LFV_QueueState.COMPLETE;
            OnComplete();
        }
    }

    override protected void OnComplete()
    {
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
        m_ContainerState.m_HasItems = true;
        m_ContainerState.m_IsVirtualizing = false;

        // Sync processing SyncVar (action locking)
        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(m_Container);
        if (lfvBarrel)
            lfvBarrel.LFV_SetIsProcessing(false);

        // clear m_ItemRef from data tree.
        // Uses shared static from LFV_FileStorage (also used by SaveAdminJson JSON-FIX).
        // After JSON-FIX, refs are already null here -- this is now a safe no-op.
        LFV_FileStorage.ClearItemRefs(m_Data.m_Items);

        // Stats
        float durationMs = GetGame().GetTime() - m_StartTimeMs;
        LFV_Stats.RecordVirtualize(durationMs);
        LFV_Stats.s_TotalVirtualizedItems = LFV_Stats.s_TotalVirtualizedItems + m_Data.m_TotalItemCount;

        string msg = "Virtualized ";
        msg = msg + m_Data.m_TotalItemCount.ToString();
        msg = msg + " items from ";
        msg = msg + m_Data.m_ContainerClass;
        msg = msg + " in ";
        msg = msg + durationMs.ToString();
        msg = msg + "ms";
        LFV_Log.Info(msg);
    }

    // ClearItemRefs moved to LFV_FileStorage.ClearItemRefs (static, shared)

    override protected void OnCancel()
    {
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
        m_ContainerState.m_HasItems = true;
        m_ContainerState.m_IsVirtualizing = false;
        LFV_FileStorage.ClearItemRefs(m_Data.m_Items);

        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(m_Container);
        if (lfvBarrel)
            lfvBarrel.LFV_SetIsProcessing(false);
    }
}

// -----------------------------------------------------------
// RestoreQueue
//
// OnStart: load .lfv with fallback, transition RESTORING
// ProcessBatch: spawn BatchSize top-level items per tick
//   (children complete in same tick via explicit field restore)
// OnComplete: delete .lfv, transition IDLE, stats
// -----------------------------------------------------------
class LFV_RestoreQueue : LFV_Queue
{
    protected ref LFV_ContainerFile m_Data;
    protected int m_RestoredCount;

    void LFV_RestoreQueue(ItemBase container, LFV_ContainerState state, int batchSize)
    {
        m_Container = container;
        m_ContainerState = state;
        m_QueueState = LFV_QueueState.PENDING;
        m_QueueType = LFV_QueueType.RESTORE;
        m_BatchSize = batchSize;
        m_CurrentIndex = 0;
        m_StartTimeMs = 0;
        m_RestoredCount = 0;
    }

    override protected void OnStart()
    {
        if (!LFV_StateMachine.CanRestore(m_Container, m_ContainerState))
        {
            m_QueueState = LFV_QueueState.FAILED;
            return;
        }

        LFV_StateMachine.Transition(m_ContainerState, LFV_State.RESTORING);

        // Write restore marker -- used by reconciliation to detect crash mid-restore
        LFV_FileStorage.WriteRestoreMarker(m_ContainerState.m_StorageId);

        // Sync processing SyncVar (action locking)
        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(m_Container);
        if (lfvBarrel)
            lfvBarrel.LFV_SetIsProcessing(true);

        // Load from disk (synchronous -- fast)
        bool loaded = LFV_FileStorage.LoadWithFallback(
            m_ContainerState.m_StorageId, m_Data);

        if (!loaded)
        {
            string rLoadErr = "RestoreQueue: load failed for ";
            rLoadErr = rLoadErr + m_ContainerState.m_StorageId;
            LFV_Log.Error(rLoadErr);
            LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
            // Reset IsProcessing -- otherwise barrel is permanently locked
            if (lfvBarrel)
                lfvBarrel.LFV_SetIsProcessing(false);
            LFV_Stats.s_FailedRestores = LFV_Stats.s_FailedRestores + 1;
            m_QueueState = LFV_QueueState.FAILED;
            return;
        }

        string msg = "RestoreQueue started: ";
        msg = msg + m_Data.m_Items.Count().ToString();
        msg = msg + " top-level items for ";
        msg = msg + m_Data.m_ContainerClass;
        LFV_Log.Info(msg);
    }

    override void ProcessBatch()
    {
        if (m_QueueState != LFV_QueueState.ACTIVE)
            return;

        // guard against container deleted between batches
        if (!m_Container)
        {
            string lostMsg = "RestoreQueue: container lost mid-restore for ";
            lostMsg = lostMsg + m_ContainerState.m_StorageId;
            LFV_Log.Error(lostMsg);
            m_QueueState = LFV_QueueState.FAILED;
            LFV_Stats.s_FailedRestores = LFV_Stats.s_FailedRestores + 1;
            return;
        }

        // Spawn BatchSize top-level items per tick
        // Children complete in same tick (recursive)
        int remaining = m_Data.m_Items.Count() - m_CurrentIndex;
        int batchEnd = m_CurrentIndex + Math.Min(m_BatchSize, remaining);

        for (int i = m_CurrentIndex; i < batchEnd; i++)
        {
            LFV_ItemRecord record = m_Data.m_Items[i];
            RestoreItem(record, m_Container, 0);
        }
        m_CurrentIndex = batchEnd;

        // Check if done
        if (m_CurrentIndex >= m_Data.m_Items.Count())
        {
            m_QueueState = LFV_QueueState.COMPLETE;
            OnComplete();
        }
    }

    // Restore a single item + children (explicit field restore, no StoreCtx)
    protected void RestoreItem(LFV_ItemRecord rec, ItemBase parent, int depth)
    {
        // Classname gone (mod removed) -- skip entire subtree
        if (!LFV_Registry.ClassnameExists(rec.m_Classname))
        {
            string skipMsg = "Classname not found, skipping: ";
            skipMsg = skipMsg + rec.m_Classname;
            LFV_Log.Warn(skipMsg);
            LFV_Stats.s_SkippedItems = LFV_Stats.s_SkippedItems + 1;
            return;
        }

        // Spawn
        ItemBase item = LFV_SpawnHelper.SpawnWithFallback(parent, rec);

        if (!item)
        {
            string spawnErrMsg = "Failed to spawn: ";
            spawnErrMsg = spawnErrMsg + rec.m_Classname;
            LFV_Log.Error(spawnErrMsg);
            LFV_Stats.s_SkippedItems = LFV_Stats.s_SkippedItems + 1;
            return;
        }

        // Apply all properties from explicit fields
        LFV_SpawnHelper.ApplyProperties(item, rec);
        rec.SetItemRef(item);
        m_RestoredCount++;

        // Recurse attachments + cargo (depth guarded)
        if (depth < LFV_Limits.MAX_ITEM_DEPTH)
        {
            int nextDepth = depth + 1;
            for (int a = 0; a < rec.m_Attachments.Count(); a++)
            {
                RestoreItem(rec.m_Attachments[a], item, nextDepth);
            }
            for (int c = 0; c < rec.m_Cargo.Count(); c++)
            {
                RestoreItem(rec.m_Cargo[c], item, nextDepth);
            }
        }
    }

    override protected void OnComplete()
    {
        // Delete restore marker -- restore completed successfully
        LFV_FileStorage.DeleteRestoreMarker(m_ContainerState.m_StorageId);

        // Hoist variables used across branches
        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(m_Container);
        float durationMs = GetGame().GetTime() - m_StartTimeMs;
        string msg = "";

        // Total failure: ZERO items restored -- revert to VIRTUALIZED for retry.
        // No items in world = no duplication risk, safe to keep .lfv.
        if (m_RestoredCount == 0)
        {
            LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
            m_ContainerState.m_HasItems = true;

            if (lfvBarrel)
                lfvBarrel.LFV_SetIsProcessing(false);

            string zeroMsg = "RestoreQueue: 0 items restored -- keeping .lfv for retry: ";
            zeroMsg = zeroMsg + m_ContainerState.m_StorageId;
            LFV_Log.Warn(zeroMsg);

            LFV_Stats.RecordRestore(durationMs);
            return;
        }

        // Normal path: items restored successfully
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.RESTORED);
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.IDLE);
        m_ContainerState.m_HasItems = false;

        // Anti-duplication: Delete .lfv IMMEDIATELY after successful restore.
        // Items are persistent -- they survive engine restarts via engine
        // persistence. The .lfv is no longer needed as backup.
        // If kept, crash would cause: player has items (persistent in
        // inventory) + .lfv re-restores all items -> duplication.
        // OnMissionFinish re-virtualizes IDLE containers with items,
        // creating a fresh .lfv that reflects actual current state.
        LFV_FileStorage.DeleteContainerFiles(m_ContainerState.m_StorageId);

        // Refresh activity timestamp so ProximityMonitor delay starts from NOW
        // (prevents immediate re-virtualization due to stale m_LastActivity)
        m_ContainerState.m_LastActivity = GetGame().GetTime();

        // Update SyncVars
        if (lfvBarrel)
        {
            lfvBarrel.LFV_SetItemCount(0);
            lfvBarrel.LFV_SetHasItems(false);
            lfvBarrel.LFV_SetIsProcessing(false);
            string emptyManifest = "";
            lfvBarrel.LFV_SetManifest(emptyManifest);
        }

        // Clear cached manifest on clients (covers both LFV and vanilla barrels)
        Param1<string> clearParam = new Param1<string>("");
        GetGame().RPCSingleParam(m_Container, LFV_RPC.MANIFEST_UPDATE, clearParam, true, null);

        // Stats
        LFV_Stats.RecordRestore(durationMs);

        msg = "Restored ";
        msg = msg + m_RestoredCount.ToString();
        msg = msg + " items to ";
        msg = msg + m_Data.m_ContainerClass;
        msg = msg + " in ";
        msg = msg + durationMs.ToString();
        msg = msg + "ms";
        LFV_Log.Info(msg);
    }

    override protected void OnCancel()
    {
        // Cancel during restore -- partial items in world.
        // Delete partial items, keep .lfv, revert to VIRTUALIZED.

        // Delete restore marker -- no longer mid-restore
        LFV_FileStorage.DeleteRestoreMarker(m_ContainerState.m_StorageId);

        // Delete partial items spawned so far
        LFV_StateMachine.ClearPhantomItems(m_Container);

        // Revert state -- .lfv still exists for next restore attempt
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);

        LFV_Barrel_Base lfvBarrel = LFV_Barrel_Base.Cast(m_Container);
        if (lfvBarrel)
            lfvBarrel.LFV_SetIsProcessing(false);

        string rCancelMsg = "RestoreQueue cancelled for ";
        rCancelMsg = rCancelMsg + m_ContainerState.m_StorageId;
        LFV_Log.Warn(rCancelMsg);
    }
}

// -----------------------------------------------------------
// DropQueue
//
// For barrel destroyed while VIRTUALIZED (edge case 8).
// Loads .lfv, spawns all items on ground near barrel position.
//
// OnStart: load .lfv, store position
// ProcessBatch: spawn BatchSize items on ground per tick
// OnComplete: delete .lfv, stats
// -----------------------------------------------------------
class LFV_DropQueue : LFV_Queue
{
    protected ref LFV_ContainerFile m_Data;
    protected vector m_DropPosition;
    protected ref array<ref LFV_ItemRecord> m_FlatItems;
    protected int m_DroppedCount;

    void LFV_DropQueue(ItemBase container, LFV_ContainerState state, int batchSize)
    {
        m_Container = container;
        m_ContainerState = state;
        m_QueueState = LFV_QueueState.PENDING;
        m_QueueType = LFV_QueueType.DROP;
        m_BatchSize = batchSize;
        m_CurrentIndex = 0;
        m_StartTimeMs = 0;
        // Own fields
        m_FlatItems = new array<ref LFV_ItemRecord>();
        m_DroppedCount = 0;
        m_DropPosition = vector.Zero;
    }

    override protected void OnStart()
    {
        // Store position before container might be deleted
        if (m_Container)
            m_DropPosition = m_Container.GetPosition();

        // Transition to RESTORING so no other operation can interfere
        // (VIRTUALIZED -> RESTORING is a legal transition)
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.RESTORING);

        // Load from disk
        bool loaded = LFV_FileStorage.LoadWithFallback(
            m_ContainerState.m_StorageId, m_Data);

        if (!loaded)
        {
            string dLoadErr = "DropQueue: load failed for ";
            dLoadErr = dLoadErr + m_ContainerState.m_StorageId;
            LFV_Log.Error(dLoadErr);
            // Revert to VIRTUALIZED -- .lfv may still exist for retry
            LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
            m_QueueState = LFV_QueueState.FAILED;
            return;
        }

        // Flatten all items (top-level only, children lose hierarchy on ground)
        FlattenTopLevel(m_Data.m_Items);

        if (m_DropPosition == vector.Zero && m_Data.m_Position != vector.Zero)
            m_DropPosition = m_Data.m_Position;

        string msg = "DropQueue started: ";
        msg = msg + m_FlatItems.Count().ToString();
        msg = msg + " items to drop on ground";
        LFV_Log.Info(msg);
    }

    // Flatten recursively -- all items become top-level for ground drop
    protected void FlattenTopLevel(array<ref LFV_ItemRecord> items)
    {
        if (!items) return;
        for (int i = 0; i < items.Count(); i++)
        {
            LFV_ItemRecord rec = items[i];
            if (!rec) continue;
            m_FlatItems.Insert(rec);
            FlattenTopLevel(rec.m_Attachments);
            FlattenTopLevel(rec.m_Cargo);
        }
    }

    override void ProcessBatch()
    {
        if (m_QueueState != LFV_QueueState.ACTIVE)
            return;

        int remaining = m_FlatItems.Count() - m_CurrentIndex;
        int batchEnd = m_CurrentIndex + Math.Min(m_BatchSize, remaining);

        for (int i = m_CurrentIndex; i < batchEnd; i++)
        {
            LFV_ItemRecord rec = m_FlatItems[i];
            if (!LFV_Registry.ClassnameExists(rec.m_Classname))
            {
                LFV_Stats.s_SkippedItems = LFV_Stats.s_SkippedItems + 1;
                continue;
            }

            // Spawn on ground with slight random offset
            vector dropPos = m_DropPosition;
            float rx = Math.RandomFloat(-1.0, 1.0);
            float rz = Math.RandomFloat(-1.0, 1.0);
            dropPos[0] = dropPos[0] + rx;
            dropPos[2] = dropPos[2] + rz;

            ItemBase item = LFV_SpawnHelper.SpawnOnGround(rec.m_Classname, dropPos);
            if (item)
            {
                LFV_SpawnHelper.ApplyProperties(item, rec);
                m_DroppedCount++;
            }
        }
        m_CurrentIndex = batchEnd;

        if (m_CurrentIndex >= m_FlatItems.Count())
        {
            m_QueueState = LFV_QueueState.COMPLETE;
            OnComplete();
        }
    }

    override protected void OnComplete()
    {
        // Delete .lfv files
        LFV_FileStorage.DeleteContainerFiles(m_ContainerState.m_StorageId);

        // Cleanup state via state machine
        // Already in RESTORING (from OnStart) -> RESTORED -> IDLE
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.RESTORED);
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.IDLE);
        m_ContainerState.m_HasItems = false;

        float durationMs = GetGame().GetTime() - m_StartTimeMs;

        string msg = "Dropped ";
        msg = msg + m_DroppedCount.ToString();
        msg = msg + " items on ground in ";
        msg = msg + durationMs.ToString();
        msg = msg + "ms";
        LFV_Log.Info(msg);

        // Phase 5.5: DropQueue is only created after container has been
        // destroyed (EEKilled). OnEntityDestroyed (from EEDelete) deferred
        // the purge to let us run. Finish the purge now that items landed.
        LFV_Module module = LFV_Module.GetModule();
        if (module && m_Container)
            module.UntrackContainer(m_Container);
    }

    override protected void OnCancel()
    {
        // revert to VIRTUALIZED so container doesn't get stuck
        // in RESTORING state with no recovery path. Keep .lfv for retry.
        LFV_StateMachine.Transition(m_ContainerState, LFV_State.VIRTUALIZED);
        m_ContainerState.m_HasItems = true;

        string dCancelMsg = "DropQueue cancelled for ";
        dCancelMsg = dCancelMsg + m_ContainerState.m_StorageId;
        LFV_Log.Warn(dCancelMsg);
    }
}
