// =========================================================
// LF_VStorage -- Modded ActionOpenCloseCrate_noLock (Phase 2)
//
// Hooks MMG's toggle action for containers without codelock
// support (mmg_storage_openable_noLock_base subclasses).
// Same toggle pattern as ActionMMGCloseAndOpen.
// =========================================================

#ifdef mmg_storage
modded class ActionOpenCloseCrate_noLock
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

        if (isVirtual && wasOpen)
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                PlayerBase player = PlayerBase.Cast(action_data.m_Player);
                module.OnCloseContainer(container, player);
            }
        }

        super.OnStartServer(action_data);

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
