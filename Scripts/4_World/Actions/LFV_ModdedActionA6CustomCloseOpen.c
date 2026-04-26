// =========================================================
// LF_VStorage -- Modded ActionA6CustomCloseOpen (Fix C)
//
// Hooks A6_Base_Storage's toggle open/close action.
// A6 uses a SINGLE action for both open and close,
// unlike barrels which have separate ActionOpenBarrel/CloseBarrel.
//
// Detection: We use the protected member `opencloseState` which
// is set by ActionCondition before OnStartServer is called:
//   "#open" / "Uncover" = container is closed, about to open
//   "#close" / "Cover"  = container is open, about to close
//
// Sequence:
//   CLOSE: virtualize BEFORE super (items captured while open)
//   OPEN:  restore AFTER super (CanReceiveItemIntoCargo needs IsOpen)
//
// Phase 2: gated behind the A6_Base_Storage addon define so servers
// without A6 loaded compile this file as empty (no hard dependency).
// =========================================================

#ifdef A6_Base_Storage
modded class ActionA6CustomCloseOpen
{
    // DISABLED: manifest preview in action text (showing items incorrectly)
    // TODO: re-enable once display is fixed

    // -----------------------------------------------------------
    // Server: virtualize/restore around the A6 toggle
    //
    // opencloseState tells us what's about to happen:
    //   "#open" / "Uncover" → container closed → will OPEN
    //   "#close" / "Cover"  → container open → will CLOSE
    // -----------------------------------------------------------
    override void OnStartServer(ActionData action_data)
    {
        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        bool isVirtual = false;
        if (container)
            isVirtual = LFV_Registry.IsVirtualContainer(container.GetType());

        // Detect intent from opencloseState (set by ActionCondition)
        bool aboutToClose = (opencloseState == "#close" || opencloseState == "Cover");
        bool aboutToOpen = (opencloseState == "#open" || opencloseState == "Uncover");

        // PRE-CLOSE: virtualize items before super closes the container
        if (isVirtual && aboutToClose)
        {
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                PlayerBase player = PlayerBase.Cast(action_data.m_Player);
                module.OnCloseContainer(container, player);
            }
        }

        // Execute A6 toggle (Open or Close with code lock checks)
        super.OnStartServer(action_data);

        // POST-OPEN: restore items after super opened the container
        if (isVirtual && aboutToOpen)
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
