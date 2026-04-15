// =========================================================
// LF_VStorage -- Modded ActionCloseRaGItem (Fix A)
//
// Hooks RaG_Core's close action for non-barrel containers.
//
// ActionCloseRaGItem extends ActionInteractBase and uses
// OnStartServer(). super.OnStartServer() calls ragitem.Close().
//
// Sequence: LFV virtualize FIRST, then super (Close).
// Items must be captured while container is still open.
// =========================================================

modded class ActionCloseRaGItem
{
    // -----------------------------------------------------------
    // Server: virtualize items BEFORE closing
    // We call OnCloseContainer first, then super.OnStartServer()
    // which calls ragitem.Close() -> IsOpen()=false
    // -----------------------------------------------------------
    override void OnStartServer(ActionData action_data)
    {
        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        if (container && LFV_Registry.IsVirtualContainer(container.GetType()))
        {
            PlayerBase player = PlayerBase.Cast(action_data.m_Player);
            LFV_Module module = LFV_Module.GetModule();
            if (module)
                module.OnCloseContainer(container, player);
        }

        // Close the container after virtualizing
        super.OnStartServer(action_data);
    }
}
