// =========================================================
// LF_VStorage -- Modded ActionOpenRaGItem (Fix A)
//
// Hooks RaG_Core's open action for non-barrel containers
// (e.g. rag_baseitems_military_locker_big).
//
// ActionOpenRaGItem extends ActionInteractBase and uses
// OnStartServer() (not OnExecuteServer). super.OnStartServer()
// calls ragitem.Open() which sets IsOpen()=true.
//
// Sequence: super FIRST (Open), then LFV restore.
// This ensures CanReceiveItemIntoCargo()=true during spawn.
// =========================================================

modded class ActionOpenRaGItem
{
    // -----------------------------------------------------------
    // Dynamic text: show manifest when container has virtual items
    // -----------------------------------------------------------
    // DISABLED: manifest preview in action text (showing items incorrectly)
    // TODO: re-enable once display is fixed
    override void OnActionInfoUpdate(PlayerBase player, ActionTarget target, ItemBase item)
    {
        m_Text = "#open";
    }

    // -----------------------------------------------------------
    // Server: restore virtual items AFTER opening
    // super.OnStartServer() -> ragitem.Open() -> IsOpen()=true
    // Then we call OnOpenContainer to restore items from .lfv
    // -----------------------------------------------------------
    override void OnStartServer(ActionData action_data)
    {
        // Open the container first (IsOpen=true needed for cargo)
        super.OnStartServer(action_data);

        ItemBase container = ItemBase.Cast(action_data.m_Target.GetObject());
        if (container && LFV_Registry.IsVirtualContainer(container.GetType()))
        {
            PlayerBase player = PlayerBase.Cast(action_data.m_Player);
            LFV_Module module = LFV_Module.GetModule();
            if (module)
            {
                if (!module.CheckRateLimit(player, container))
                    return;
                module.OnOpenContainer(container, player);
            }
        }
    }
}
