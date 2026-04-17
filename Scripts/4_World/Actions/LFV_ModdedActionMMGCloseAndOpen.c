// =========================================================
// LF_VStorage -- Modded ActionMMGCloseAndOpen (Phase 2)
//
// Hooks MMG Storage's toggle open/close action.
// MMG uses a SINGLE action for both (ActionMMGCloseAndOpen)
// that checks crate.IsOpen() inside OnStartServer.
//
// Detection: read IsOpen() BEFORE super.
//   IsOpen()=true  → super will close → virtualize BEFORE super
//   IsOpen()=false → super will open  → restore AFTER super
//
// Covers every mmg_storage_openable_base subclass (lockers,
// crates, safes). MMG already has its own restart auto-close
// (AfterStoreLoad + CallLater 5s), so no restart hook needed.
// =========================================================

#ifdef mmg_storage
modded class ActionMMGCloseAndOpen
{
    override void OnStartServer(ActionData action_data)
    {
        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        bool isVirtual = false;
        bool wasOpen = false;
        if (container)
        {
            isVirtual = LFV_Registry.IsVirtualContainer(container.GetType());
            wasOpen = container.IsOpen();
        }

        // PRE-CLOSE: capture items while still open
        if (isVirtual && wasOpen)
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                PlayerBase player = PlayerBase.Cast(action_data.m_Player);
                module.OnCloseContainer(container, player);
            }
        }

        // MMG toggle: flips IsOpen via Open()/Close()
        super.OnStartServer(action_data);

        // POST-OPEN: restore after container is open
        if (isVirtual && !wasOpen)
        {
            LFV_Module module2 = LFV_Module.GetModule();
            if (module2)
            {
                PlayerBase player2 = PlayerBase.Cast(action_data.m_Player);
                if (!module2.CheckRateLimit(player2, container))
                    return;
                module2.OnOpenContainer(container, player2);
            }
        }
    }
}
#endif
