// =========================================================
// LF_VStorage -- StateMachine (v1.0)
//
// Manages the 5-state lifecycle of each tracked container.
// Handles transitions, validation, and crash reconciliation.
//
// States: IDLE -> VIRTUALIZING -> VIRTUALIZED -> RESTORING -> RESTORED
//
// Reconciliation at startup restores consistent state after
// crash during any intermediate state.
// =========================================================

class LFV_StateMachine
{
    // -----------------------------------------------------------
    // Validate transition -- returns true if transition is legal
    // -----------------------------------------------------------
    static bool CanTransition(int fromState, int toState)
    {
        // IDLE -> VIRTUALIZING (start saving)
        if (fromState == LFV_State.IDLE && toState == LFV_State.VIRTUALIZING)
            return true;

        // VIRTUALIZING -> VIRTUALIZED (save complete, items deleted)
        if (fromState == LFV_State.VIRTUALIZING && toState == LFV_State.VIRTUALIZED)
            return true;

        // VIRTUALIZING -> IDLE (save failed, abort)
        if (fromState == LFV_State.VIRTUALIZING && toState == LFV_State.IDLE)
            return true;

        // VIRTUALIZED -> RESTORING (start loading)
        if (fromState == LFV_State.VIRTUALIZED && toState == LFV_State.RESTORING)
            return true;

        // RESTORING -> RESTORED (all items spawned)
        if (fromState == LFV_State.RESTORING && toState == LFV_State.RESTORED)
            return true;

        // RESTORING -> VIRTUALIZED (restore failed, abort -- keep .lfv)
        if (fromState == LFV_State.RESTORING && toState == LFV_State.VIRTUALIZED)
            return true;

        // RESTORED -> IDLE (cleanup: delete .lfv)
        if (fromState == LFV_State.RESTORED && toState == LFV_State.IDLE)
            return true;

        // IDLE -> IDLE (no-op, e.g. reconciliation already clean)
        if (fromState == LFV_State.IDLE && toState == LFV_State.IDLE)
            return true;

        return false;
    }

    // -----------------------------------------------------------
    // Apply transition on a container state
    // -----------------------------------------------------------
    static bool Transition(LFV_ContainerState state, int newState)
    {
        if (!CanTransition(state.m_State, newState))
        {
            string msg = "Illegal transition: ";
            msg = msg + LFV_State.ToString(state.m_State);
            msg = msg + " -> ";
            msg = msg + LFV_State.ToString(newState);
            msg = msg + " for ";
            msg = msg + state.m_StorageId;
            LFV_Log.Error(msg);
            return false;
        }

        string logMsg = "State: ";
        logMsg = logMsg + LFV_State.ToString(state.m_State);
        logMsg = logMsg + " -> ";
        logMsg = logMsg + LFV_State.ToString(newState);
        logMsg = logMsg + " [";
        logMsg = logMsg + state.m_StorageId;
        logMsg = logMsg + "]";
        LFV_Log.Info(logMsg);

        state.m_State = newState;

        // Processing flag management
        if (newState == LFV_State.VIRTUALIZING || newState == LFV_State.RESTORING)
            state.m_IsProcessing = true;
        else
            state.m_IsProcessing = false;

        return true;
    }

    // -----------------------------------------------------------
    // Reconcile a single container at startup
    //
    // Called for each .lfv found on disk during ScanAndReconcile.
    // The .lfv state tells us where the crash happened.
    // -----------------------------------------------------------
    static int Reconcile(ItemBase container, LFV_ContainerFile fileData)
    {
        int fileState = fileData.m_State;
        string sid = fileData.m_StorageId;

        switch (fileState)
        {
            case LFV_State.VIRTUALIZING:
            {
                // Crash during save. Items should still be in world.
                // .lfv may be corrupt -- delete it.
                string warnMsg1 = "Reconcile VIRTUALIZING -> IDLE: ";
                warnMsg1 = warnMsg1 + sid;
                LFV_Log.Warn(warnMsg1);
                LFV_FileStorage.DeleteContainerFiles(sid);
                return LFV_State.IDLE;
            }

            case LFV_State.VIRTUALIZED:
            {
                // --- Nivel 1: Crash durante RESTORING? ---
                // El .lfv siempre tiene state=VIRTUALIZED (nunca RESTORING),
                // asi que usamos el marker file para detectar restore interrumpido.
                if (LFV_FileStorage.HasRestoreMarker(sid))
                {
                    // Crash durante restore. Items parciales persistieron.
                    // Limpiar parciales, mantener .lfv para re-restore limpio.
                    if (container)
                    {
                        ClearPhantomItems(container);
                    }
                    LFV_FileStorage.DeleteRestoreMarker(sid);
                    string restoreMsg = "Reconcile RESTORING (marker) -> VIRTUALIZED: ";
                    restoreMsg = restoreMsg + sid;
                    LFV_Log.Warn(restoreMsg);
                    return LFV_State.VIRTUALIZED;
                }

                // --- Nivel 2: Items persistentes sobrevivieron crash? ---
                // Restore completo + .lfv deberia haberse borrado.
                // Si container tiene items = items sobrevivieron, .lfv es stale.
                if (container && HasCargoOrAttachments(container))
                {
                    // Items en mundo son autoritativos. Borrar .lfv stale.
                    LFV_FileStorage.DeleteContainerFiles(sid);
                    string idleSurvivedMsg = "Reconcile VIRTUALIZED -> IDLE (items survived, deleting stale .lfv): ";
                    idleSurvivedMsg = idleSurvivedMsg + sid;
                    LFV_Log.Info(idleSurvivedMsg);
                    return LFV_State.IDLE;
                }

                // --- Nivel 3: Normal VIRTUALIZED (container vacio, items en .lfv) ---
                if (container)
                {
                    ClearPhantomItems(container);
                }
                string infoMsg1 = "Reconcile VIRTUALIZED (ok): ";
                infoMsg1 = infoMsg1 + sid;
                LFV_Log.Info(infoMsg1);
                return LFV_State.VIRTUALIZED;
            }

            case LFV_State.RESTORING:
            {
                // NOTE: Currently unreachable -- .lfv always stores VIRTUALIZED state.
                // RESTORING is runtime-only (in-memory LFV_ContainerState).
                // Kept as defensive fallback in case future versions stamp state in .lfv.
                if (container)
                {
                    ClearPhantomItems(container);
                }
                string warnMsg2 = "Reconcile RESTORING -> VIRTUALIZED: ";
                warnMsg2 = warnMsg2 + sid;
                LFV_Log.Warn(warnMsg2);
                return LFV_State.VIRTUALIZED;
            }

            case LFV_State.RESTORED:
            {
                // NOTE: Currently unreachable -- .lfv always stores VIRTUALIZED state.
                // RESTORED is runtime-only. Kept as defensive fallback.
                if (container && HasCargoOrAttachments(container))
                {
                    // Items are in world -- safe to delete .lfv
                    string infoMsg2 = "Reconcile RESTORED -> IDLE (items in world): ";
                    infoMsg2 = infoMsg2 + sid;
                    LFV_Log.Info(infoMsg2);
                    LFV_FileStorage.DeleteContainerFiles(sid);
                    return LFV_State.IDLE;
                }
                else
                {
                    // Items NOT in world (crash lost items)
                    // Keep .lfv for re-restore on next player approach
                    string warnMsg3 = "Reconcile RESTORED -> VIRTUALIZED (items lost, keeping .lfv): ";
                    warnMsg3 = warnMsg3 + sid;
                    LFV_Log.Warn(warnMsg3);
                    return LFV_State.VIRTUALIZED;
                }
            }
        }

        // IDLE in file: leftover .lfv from a restore that completed
        // but wasn't cleaned up (new safety net behavior).
        // If container has no items, treat as VIRTUALIZED for re-restore.
        if (fileState == LFV_State.IDLE)
        {
            if (container && HasCargoOrAttachments(container))
            {
                string idleOkMsg = "Reconcile IDLE .lfv (items in world, deleting .lfv): ";
                idleOkMsg = idleOkMsg + sid;
                LFV_Log.Info(idleOkMsg);
                LFV_FileStorage.DeleteContainerFiles(sid);
                return LFV_State.IDLE;
            }
            else
            {
                string idleWarnMsg = "Reconcile IDLE .lfv -> VIRTUALIZED (items lost, keeping .lfv): ";
                idleWarnMsg = idleWarnMsg + sid;
                LFV_Log.Warn(idleWarnMsg);
                return LFV_State.VIRTUALIZED;
            }
        }

        // Unknown state
        string unknownMsg = "Reconcile unknown state ";
        unknownMsg = unknownMsg + fileState.ToString();
        unknownMsg = unknownMsg + " for ";
        unknownMsg = unknownMsg + sid;
        LFV_Log.Warn(unknownMsg);
        return LFV_State.IDLE;
    }

    // -----------------------------------------------------------
    // Clear phantom items from a container
    //
    // Primary mechanism for cleaning up persistent items from
    // interrupted restores. Called during reconciliation when:
    //   - .restoring marker found (crash mid-restore)
    //   - Normal VIRTUALIZED state (engine-loaded items)
    // Also handles RestoreQueue.OnCancel (partial spawns).
    // -----------------------------------------------------------
    static void ClearPhantomItems(ItemBase container)
    {
        if (!container) return;

        GameInventory inv = container.GetInventory();
        if (!inv) return;

        int cleared = 0;

        // Attachments (iterate backward for safe removal)
        for (int i = inv.AttachmentCount() - 1; i >= 0; i = i - 1)
        {
            EntityAI att = inv.GetAttachmentFromIndex(i);
            if (att)
            {
                GetGame().ObjectDelete(att);
                cleared = cleared + 1;
            }
        }

        // Cargo
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            for (int j = cargo.GetItemCount() - 1; j >= 0; j = j - 1)
            {
                EntityAI cargoItem = cargo.GetItem(j);
                if (cargoItem)
                {
                    GetGame().ObjectDelete(cargoItem);
                    cleared = cleared + 1;
                }
            }
        }

        if (cleared > 0)
        {
            string clearMsg = "Cleared ";
            clearMsg = clearMsg + cleared.ToString();
            clearMsg = clearMsg + " phantom items from container";
            LFV_Log.Info(clearMsg);
        }
    }

    // -----------------------------------------------------------
    // Check if container passes all gates before virtualizing
    // -----------------------------------------------------------
    static bool CanVirtualize(ItemBase container, LFV_ContainerState state)
    {
        if (!container || !state)
            return false;

        // Already processing
        if (state.m_IsProcessing)
            return false;

        // Invalid state for virtualization
        if (state.m_State != LFV_State.IDLE)
            return false;

        // In player inventory
        if (container.GetHierarchyRootPlayer())
            return false;

        // In vehicle
        EntityAI root = container.GetHierarchyRoot();
        if (root)
        {
            string kCar = "CarScript";
            if (root.IsKindOf(kCar))
                return false;
        }

        // Nested container
        if (root && root != container)
            return false;

        // Destroyed
        if (container.IsDamageDestroyed())
            return false;

        return true;
    }

    // -----------------------------------------------------------
    // Check if container passes all gates before restoring
    // -----------------------------------------------------------
    static bool CanRestore(ItemBase container, LFV_ContainerState state)
    {
        if (!container || !state)
            return false;

        if (state.m_IsProcessing)
            return false;

        if (state.m_State != LFV_State.VIRTUALIZED)
            return false;

        if (container.IsDamageDestroyed())
            return false;

        return true;
    }

    // -----------------------------------------------------------
    // Check if container has any cargo or attachments
    // -----------------------------------------------------------
    static bool HasCargoOrAttachments(ItemBase container)
    {
        if (!container) return false;

        GameInventory inv = container.GetInventory();
        if (!inv) return false;

        if (inv.AttachmentCount() > 0)
            return true;

        CargoBase cargo = inv.GetCargo();
        if (cargo && cargo.GetItemCount() > 0)
            return true;

        return false;
    }
}
