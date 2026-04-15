// =========================================================
// LF_VStorage -- ProximityMonitor (Sprint 3, Phase D.1)
//
// Handles virtualize/restore for NON-BARREL containers
// (SeaChest, WoodenCrate, Tents, Shelters, mod furniture).
// Barrels use open/close hooks and do NOT need proximity.
//
// Design:
//   - Timer fires every ProximityCheckInterval ms
//   - Container list split into 3 buckets, 1/3 checked per tick
//     (spreads load over 3 ticks instead of spiking every tick)
//   - VIRTUALIZED + player within RestoreRadius -> RequestRestore
//   - IDLE + items + nobody within VirtualizeRadius + delay -> RequestVirtualize
//   - Anti ping-pong: VirtualizeRadius > RestoreRadius (25m > 20m)
//
// Container list rebuilt periodically (every ~30 ticks = ~90s at 3s interval)
// to pick up newly tracked containers and drop deleted ones.
//
// All arrays are m_ fields, cleared each tick (rule 8: no new in ticks).
// =========================================================

class LFV_ProximityMonitor
{
    protected LFV_Module m_Module;
    protected LFV_Settings m_Settings;

    // --- Reusable arrays (rule 8: no allocations in ticks) ---
    protected ref array<ItemBase> m_Containers;
    protected ref array<Man> m_Players;
    protected ref array<EntityAI> m_ScanEntities;

    // --- Bucket rotation ---
    protected int m_CurrentBucket;
    protected int m_TicksSinceRebuild;
    protected static const int REBUILD_INTERVAL = 30;
    protected static const string BARREL_BASE_CLASS = "Barrel_ColorBase";

    // -----------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------
    void LFV_ProximityMonitor(LFV_Module module)
    {
        m_Module = module;
        m_Settings = module.GetSettings();
        m_Containers = new array<ItemBase>();
        m_Players = new array<Man>();
        m_ScanEntities = new array<EntityAI>();
        m_CurrentBucket = 0;
        m_TicksSinceRebuild = 0;

        RebuildContainerList();
    }

    // -----------------------------------------------------------
    // Rebuild the non-barrel container list from Module state
    //
    // Also scans world for un-tracked vanilla containers that
    // are in the whitelist (Sprint 4, #10: auto-registration).
    //
    // Filters: only containers where state.m_IsLFVBarrel == false
    // AND the entity is NOT a Barrel_ColorBase (catches vanilla
    // barrels in the whitelist that aren't LFV barrels).
    // -----------------------------------------------------------
    void RebuildContainerList()
    {
        m_Containers.Clear();

        // --- Phase 1: Collect already-tracked non-barrel containers ---
        int count = m_Module.GetTrackedCount();
        for (int i = 0; i < count; i = i + 1)
        {
            ItemBase container = m_Module.GetTrackedContainerAt(i);
            if (!container) continue;

            LFV_ContainerState state = m_Module.GetContainerState(container);
            if (!state) continue;

            // Skip barrels -- they use open/close hooks
            if (state.m_IsLFVBarrel)
                continue;

            // Skip action-triggered -- they use action hooks (RaG, A6, etc.)
            if (state.m_IsActionTriggered)
                continue;

            // Double-check: skip vanilla barrels too
            if (container.IsKindOf(BARREL_BASE_CLASS))
                continue;

            m_Containers.Insert(container);
        }

        // --- Phase 2: Auto-discover un-tracked whitelist containers ---
        // Scan near online players for containers in the whitelist
        // that aren't yet tracked by the Module (Sprint 4, #10)
        int discovered = 0;
        m_Players.Clear();
        GetGame().GetWorld().GetPlayerList(m_Players);

        float scanRadius = m_Settings.m_ProximityVirtualizeRadius;
        float scanHalf = scanRadius;

        for (int p = 0; p < m_Players.Count(); p = p + 1)
        {
            Man man = m_Players[p];
            if (!man) continue;

            vector playerPos = man.GetPosition();
            vector halfVec = Vector(scanHalf, scanHalf, scanHalf);
            vector minPos = playerPos - halfVec;
            vector maxPos = playerPos + halfVec;

            m_ScanEntities.Clear();
            DayZPlayerUtils.SceneGetEntitiesInBox(minPos, maxPos, m_ScanEntities, QueryFlags.DYNAMIC);

            for (int e = 0; e < m_ScanEntities.Count(); e = e + 1)
            {
                ItemBase item = ItemBase.Cast(m_ScanEntities[e]);
                if (!item) continue;

                // Already tracked?
                if (m_Module.IsTracked(item)) continue;

                // Is it in the whitelist?
                string classname = item.GetType();
                if (!LFV_Registry.IsVirtualContainer(classname)) continue;

                // Skip barrels -- they self-register via EEInit
                if (item.IsKindOf(BARREL_BASE_CLASS)) continue;

                // Skip action-triggered -- they register via action hooks
                if (LFV_Registry.IsActionTriggered(classname)) continue;

                // Skip items in player inventory or vehicles
                if (item.GetHierarchyRootPlayer()) continue;
                EntityAI root = item.GetHierarchyRoot();
                if (root && root != item) continue;

                // Auto-register this container
                // (AddContainer inside AutoRegister handles m_Containers insert)
                m_Module.AutoRegisterVanillaContainer(item);
                discovered = discovered + 1;
            }
        }

        string msg = "ProximityMonitor: rebuilt list -- ";
        msg = msg + m_Containers.Count().ToString();
        msg = msg + " non-barrel containers";
        if (discovered > 0)
        {
            msg = msg + " (";
            msg = msg + discovered.ToString();
            msg = msg + " newly discovered)";
        }
        LFV_Log.Info(msg);
    }

    // -----------------------------------------------------------
    // Eager-add a newly tracked container (skip waiting for rebuild)
    // -----------------------------------------------------------
    void AddContainer(ItemBase container)
    {
        if (!container) return;
        if (container.IsKindOf(BARREL_BASE_CLASS)) return;
        int count = m_Containers.Count();
        for (int i = 0; i < count; i = i + 1)
        {
            if (m_Containers[i] == container) return;
        }
        m_Containers.Insert(container);
    }

    // -----------------------------------------------------------
    // OnTick -- called by Timer every ProximityCheckInterval
    //
    // Processes 1/3 of containers per tick (bucket rotation).
    // -----------------------------------------------------------
    void OnTick()
    {
        if (!m_Module) return;
        if (m_Containers.Count() == 0)
        {
            // Periodic rebuild even when empty (new containers may appear)
            m_TicksSinceRebuild = m_TicksSinceRebuild + 1;
            if (m_TicksSinceRebuild >= REBUILD_INTERVAL)
            {
                RebuildContainerList();
                m_TicksSinceRebuild = 0;
            }
            return;
        }

        // Get player list
        m_Players.Clear();
        GetGame().GetWorld().GetPlayerList(m_Players);
        if (m_Players.Count() == 0)
        {
            // No players online -- skip proximity checks but still
            // advance bucket so we don't get stuck
            m_CurrentBucket = (m_CurrentBucket + 1) % 3;
            return;
        }

        // Calculate bucket bounds
        int total = m_Containers.Count();
        int bucketStart = (total * m_CurrentBucket) / 3;
        int bucketEnd = (total * (m_CurrentBucket + 1)) / 3;

        // Pre-compute squared radii
        float restoreRad = m_Settings.m_ProximityRestoreRadius;
        float restoreRadSq = restoreRad * restoreRad;
        float virtualRad = m_Settings.m_ProximityVirtualizeRadius;
        float virtualRadSq = virtualRad * virtualRad;
        int delayMs = m_Settings.m_ProximityVirtualizeDelay * 1000;
        int now = GetGame().GetTime();

        for (int i = bucketStart; i < bucketEnd; i = i + 1)
        {
            ItemBase container = m_Containers[i];
            if (!container) continue;

            LFV_ContainerState state = m_Module.GetContainerState(container);
            if (!state) continue;

            // Skip if already processing
            if (state.m_IsProcessing) continue;

            // Skip if has active queue
            if (m_Module.HasQueueForContainer(container)) continue;

            // Find nearest player distance
            vector cPos = container.GetPosition();
            float nearestDistSq = 999999999.0;

            for (int p = 0; p < m_Players.Count(); p = p + 1)
            {
                Man man = m_Players[p];
                if (!man) continue;

                float dSq = vector.DistanceSq(cPos, man.GetPosition());
                if (dSq < nearestDistSq)
                    nearestDistSq = dSq;
            }

            bool playerInRestore = (nearestDistSq <= restoreRadSq);
            bool playerOutsideVirtualize = (nearestDistSq > virtualRadSq);

            // VIRTUALIZED + player within RestoreRadius -> restore
            if (state.m_State == LFV_State.VIRTUALIZED && playerInRestore)
            {
                m_Module.RequestRestore(container);
                continue;
            }

            // IDLE + has items + nobody within VirtualizeRadius + delay -> virtualize
            if (state.m_State == LFV_State.IDLE && playerOutsideVirtualize)
            {
                if (LFV_StateMachine.HasCargoOrAttachments(container))
                {
                    int elapsed = now - state.m_LastActivity;
                    if (elapsed > delayMs)
                    {
                        m_Module.RequestVirtualize(container);
                    }
                }
                continue;
            }

            // Update LastActivity if player is within restore radius
            // (prevents virtualizing while someone is nearby)
            if (playerInRestore)
            {
                state.m_LastActivity = now;
            }
        }

        // Advance bucket
        m_CurrentBucket = (m_CurrentBucket + 1) % 3;

        // Periodic rebuild
        m_TicksSinceRebuild = m_TicksSinceRebuild + 1;
        if (m_TicksSinceRebuild >= REBUILD_INTERVAL)
        {
            RebuildContainerList();
            m_TicksSinceRebuild = 0;
        }
    }
}
