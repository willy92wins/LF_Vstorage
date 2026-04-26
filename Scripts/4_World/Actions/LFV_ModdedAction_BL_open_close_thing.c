// =========================================================
// LF_VStorage -- Modded Action_BL_open_close_thing
//
// Hooks Boomlay's furniture toggle action (bl_shared_data).
// Covers every bl_thing subclass that is a container:
//   bl_pallet_cabinet_xs / _s / _m / _l   (this PBO)
//   bl_pallet_box*, bl_pallet_bed*, bl_fridge*, bl_stove*,
//   bl_deposit*, bl_rain_collector*, bl_specials*, ...
// (all share the same Action_BL_open_close_thing path)
//
// Action uses OnExecuteServer (NOT OnStartServer) to call
// action_target.Open() / Close() — verified against
// bl_shared_data source. Single action handles both directions.
//
// Detection: wasOpen snapshot before super.
//   wasOpen=true  → super will close → virtualize BEFORE super
//   wasOpen=false → super will open  → restore AFTER super
//
// Auto-close timer (bl_AutomaticClose) and restart CallLater
// bypass this hook; LFV's periodic scan safety net (IDLE+items
// >5 min in LFV_Module_Server.OnPeriodicScanTick) re-virtualizes
// them, matching MMG behavior.
//
// Gated behind the bl_shared_data addon define so servers
// without Boomlay loaded compile this file as empty.
// =========================================================

#ifdef bl_shared_data
modded class Action_BL_open_close_thing
{
    override void OnExecuteServer(ActionData action_data)
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

        // Boomlay toggle: IsOpen ? Close() : Open()
        super.OnExecuteServer(action_data);

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
