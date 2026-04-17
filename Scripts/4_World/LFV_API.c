// =========================================================
// LF_VStorage -- Public API for third-party mod integration
//
// Use case: your mod defines a custom container type that
// LF_VStorage does not natively hook (SFM-style always-open
// containers, custom actions, sealed/unsealed mechanics, etc.)
// and you want its contents virtualized when "closed" and
// restored when "opened".
//
// Pattern: inside your mod's close/open action (OnStartServer
// or OnExecuteServer), call LFV_API.NotifyClosed(target) or
// NotifyOpened(target). LFV handles everything downstream:
// serialization, disk write, item delete, restore, cleanup.
//
// Safety:
//   - Server-only (guarded with #ifdef SERVER internally)
//   - No-ops until startup gate opens (OnMissionLoaded)
//   - No-ops for classnames in m_NeverVirtualize
//   - No-ops if LFV disabled due to Expansion/VSM conflict
//
// No-ops are silent. Call it freely from every code path you
// think might fire — LFV filters.
// =========================================================

class LFV_API
{
    // -----------------------------------------------------------
    // Notify that `container` has just been opened. LFV will
    // restore any virtualized items back into it.
    // -----------------------------------------------------------
    static void NotifyOpened(ItemBase container)
    {
        #ifdef SERVER
        if (!container) return;

        LFV_Module module = LFV_Module.GetModule();
        if (!module) return;
        if (!module.IsStartupComplete()) return;

        if (!LFV_Registry.IsVirtualContainer(container.GetType())) return;

        module.OnOpenContainer(container, null);
        #endif
    }

    // -----------------------------------------------------------
    // Notify that `container` is about to be closed. LFV will
    // capture its current inventory to disk and delete the items
    // from the world.
    //
    // Call BEFORE your close logic runs (so the items are still
    // in the container when LFV captures).
    // -----------------------------------------------------------
    static void NotifyClosed(ItemBase container)
    {
        #ifdef SERVER
        if (!container) return;

        LFV_Module module = LFV_Module.GetModule();
        if (!module) return;
        if (!module.IsStartupComplete()) return;

        if (!LFV_Registry.IsVirtualContainer(container.GetType())) return;

        module.OnCloseContainer(container, null);
        #endif
    }
}
