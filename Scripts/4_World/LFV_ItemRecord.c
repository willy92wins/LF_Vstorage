// =========================================================
// LF_VStorage -- ItemRecord model (v1.0)
//
// Represents a single item in the virtual storage tree.
// All fields are serialized manually via WriteItemRecord/
// ReadItemRecord in LFV_FileStorage.c -- NEVER use
// Write(wholeObject) or Write(array) automatic engine
// serialization.
//
// m_ItemRef is runtime-only, never written to disk.
//
// RULES:
// - NEVER change the order of existing fields
// - NEVER remove fields
// - New fields go at the END, guarded by formatVersion
// =========================================================

class LFV_ItemRecord
{
    // --- Serialized fields (campo por campo) ---
    string  m_Classname;
    int     m_InvType;       // LFV_InvType.CARGO or LFV_InvType.ATTACHMENT
    int     m_Row;
    int     m_Col;
    int     m_Idx;
    int     m_SlotId;
    bool    m_Flipped;
    float   m_Health;
    int     m_Quantity;
    int     m_LiquidType;
    int     m_FoodStage;
    int     m_AmmoCount;
    float   m_Temperature;
    float   m_Wetness;
    float   m_Energy;
    bool    m_HasEnergy;

    ref array<ref LFV_CartridgeData> m_ChamberRounds;
    ref array<ref LFV_CartridgeData> m_InternalMagRounds;
    ref array<ref LFV_CartridgeData> m_MagRounds;

    // --- FORMAT >= 3: new explicit fields (replacing StoreCtx) ---
    int     m_Agents;        // Disease bitmask (GetAgents)
    int     m_Combination;   // CombinationLock code (GetCombination)
    int     m_Cleanness;     // Disinfection state (GetCleanness)

    ref array<ref LFV_ItemRecord> m_Attachments;
    ref array<ref LFV_ItemRecord> m_Cargo;

    // --- Runtime only (NOT serialized) ---
    // MUST be protected: JsonFileLoader serializes all PUBLIC fields.
    // ItemBase is a native engine type that crashes JsonFileLoader
    // even when null. Protected hides it from the serializer.
    protected ItemBase m_ItemRef;

    void SetItemRef(ItemBase item) { m_ItemRef = item; }
    ItemBase GetItemRef() { return m_ItemRef; }

    void LFV_ItemRecord()
    {
        m_Classname = "";
        m_InvType = LFV_InvType.CARGO;
        m_Row = 0;
        m_Col = 0;
        m_Idx = 0;
        m_SlotId = -1;
        m_Flipped = false;
        m_Health = 1.0;
        m_Quantity = -1;
        m_LiquidType = 0;
        m_FoodStage = 0;
        m_AmmoCount = 0;
        m_Temperature = 15.0;
        m_Wetness = 0.0;
        m_Energy = 0.0;
        m_HasEnergy = false;

        m_ChamberRounds = new array<ref LFV_CartridgeData>();
        m_InternalMagRounds = new array<ref LFV_CartridgeData>();
        m_MagRounds = new array<ref LFV_CartridgeData>();
        m_Agents = 0;
        m_Combination = 0;
        m_Cleanness = 0;
        m_Attachments = new array<ref LFV_ItemRecord>();
        m_Cargo = new array<ref LFV_ItemRecord>();
        m_ItemRef = null;
    }

    // -----------------------------------------------------------
    // Safe ammo type lookup from config.
    //
    // Reads CfgMagazines/CfgWeapons static data -- NEVER touches
    // live entity internals, so it cannot crash the engine.
    //
    // Path 1: CfgMagazines >> className >> ammo  (magazines)
    // Path 2: CfgWeapons >> className >> magazines[] >> first
    //         compatible magazine's ammo type    (weapons)
    //
    // Trade-off: Internal magazine and standalone magazine rounds
    // are restored with the CONFIG ammo type (e.g. "Bullet_762x39")
    // and m_Damage=0. Per-cartridge damage is lost because the engine
    // API (GetCartridgeInfo) only works on chambers, not on internal
    // magazines or standalone mag stacks. Mods that add custom ammo
    // subtypes sharing the same magazine classname will restore as
    // the config default type. Chamber rounds DO preserve both
    // damage and ammo type (GetCartridgeInfo works for chambers).
    // -----------------------------------------------------------
    protected static string SafeGetAmmoType(string className)
    {
        // 1. Direct: CfgMagazines >> className >> ammo
        string magPath = "CfgMagazines " + className + " ammo";
        if (GetGame().ConfigIsExisting(magPath))
        {
            string ammoType = "";
            GetGame().ConfigGetText(magPath, ammoType);
            if (ammoType != "")
                return ammoType;
        }

        // 2. Weapon: get first compatible magazine, read ITS ammo
        TStringArray magNames = new TStringArray;

        string magsPath = "CfgWeapons " + className + " magazines";
        if (GetGame().ConfigIsExisting(magsPath))
            GetGame().ConfigGetTextArray(magsPath, magNames);

        // Fallback: chamberableFrom[]
        if (magNames.Count() == 0)
        {
            string chamberPath = "CfgWeapons " + className + " chamberableFrom";
            if (GetGame().ConfigIsExisting(chamberPath))
                GetGame().ConfigGetTextArray(chamberPath, magNames);
        }

        for (int i = 0; i < magNames.Count(); i++)
        {
            string compatPath = "CfgMagazines " + magNames[i] + " ammo";
            if (GetGame().ConfigIsExisting(compatPath))
            {
                string weapAmmo = "";
                GetGame().ConfigGetText(compatPath, weapAmmo);
                if (weapAmmo != "")
                    return weapAmmo;
            }
        }

        return "";
    }

    // -----------------------------------------------------------
    // Build from a live item in world (server-side capture)
    //
    // CRASH FIX: weapon/magazine per-cartridge engine calls
    // (GetInternalMagazineCartridgeInfo, GetCartridgeAtIndex)
    // can hard-crash on corrupt data from modded items.
    // Replaced with config-based ammo type lookup.
    //
    // - Chambers: 1 engine call per muzzle (max 9, safe)
    // - Internal mags: config ammo type + count (no iteration)
    // - External mags: config ammo type + count (no iteration)
    //
    // Trade-off: per-cartridge damage lost (set to 0).
    // In practice ammo damage is almost always 0 (pristine).
    // -----------------------------------------------------------
    static LFV_ItemRecord FromItem(ItemBase item, int invType, int row, int col, int idx, int slotId, bool flipped, int depth)
    {
        // Early validation: entity must be alive
        if (!item) return null;
        string className = item.GetType();
        if (className == "") return null;

        LFV_ItemRecord rec = new LFV_ItemRecord();
        rec.m_Classname = className;
        rec.m_InvType = invType;
        rec.m_Row = row;
        rec.m_Col = col;
        rec.m_Idx = idx;
        rec.m_SlotId = slotId;
        rec.m_Flipped = flipped;
        rec.m_ItemRef = item;

        // Health (clamp: never negative, cap at max)
        string dmgZone = "";
        string healthType = "Health";
        float rawHealth = item.GetHealth(dmgZone, healthType);
        float maxHealth = item.GetMaxHealth(dmgZone, healthType);
        float rawQty = 0;
        if (rawHealth < 0) rawHealth = 0;
        if (maxHealth > 0 && rawHealth > maxHealth) rawHealth = maxHealth;
        rec.m_Health = rawHealth;

        // Quantity (clamp: never negative)
        if (item.HasQuantity())
        {
            rawQty = item.GetQuantity();
            if (rawQty < 0) rawQty = 0;
            rec.m_Quantity = rawQty;
            rec.m_LiquidType = item.GetLiquidType();
        }

        // Food stage
        Edible_Base edible = Edible_Base.Cast(item);
        if (edible && edible.HasFoodStage())
        {
            FoodStage fs = edible.GetFoodStage();
            if (fs)
                rec.m_FoodStage = fs.GetFoodStageType();
        }

        // Ammo (ammo piles)
        if (item.IsAmmoPile())
        {
            Magazine ammoPile = Magazine.Cast(item);
            if (ammoPile)
                rec.m_AmmoCount = ammoPile.GetAmmoCount();
        }

        // Temperature
        rec.m_Temperature = item.GetTemperature();

        // Wetness
        rec.m_Wetness = item.GetWet();

        // Energy -- guard against null ComponentEnergyManager
        if (item.HasEnergyManager())
        {
            ComponentEnergyManager cem = item.GetCompEM();
            if (cem)
            {
                rec.m_HasEnergy = true;
                rec.m_Energy = cem.GetEnergy();
            }
        }

        // -------------------------------------------------------
        // WEAPON CARTRIDGE DATA -- SAFE VERSION
        //
        // Chamber: single GetCartridgeInfo per muzzle (low count).
        // Internal mag: config-based ammo type, skip per-cartridge
        //   engine calls that hard-crash on corrupt weapon data.
        // -------------------------------------------------------
        Weapon_Base weapon = Weapon_Base.Cast(item);
        if (weapon)
        {
            int muzzleCount = weapon.GetMuzzleCount();
            if (muzzleCount > 0 && muzzleCount < 10)
            {
                for (int m = 0; m < muzzleCount; m++)
                {
                    // Chamber: one engine call per muzzle (safe)
                    if (weapon.IsChamberFull(m))
                    {
                        float chamberDmg = 0;
                        string chamberType = "";
                        weapon.GetCartridgeInfo(m, chamberDmg, chamberType);
                        if (chamberType != "")
                        {
                            LFV_CartridgeData cd = new LFV_CartridgeData();
                            cd.m_MuzzleIdx = m;
                            cd.m_Damage = chamberDmg;
                            cd.m_AmmoType = chamberType;
                            rec.m_ChamberRounds.Insert(cd);
                        }
                    }

                    // Internal magazine: config ammo type + count
                    int internalCount = weapon.GetInternalMagazineCartridgeCount(m);
                    if (internalCount > 0)
                    {
                        if (internalCount > 500) internalCount = 500;
                        string intAmmoType = SafeGetAmmoType(className);
                        if (intAmmoType != "")
                        {
                            for (int ic = 0; ic < internalCount; ic++)
                            {
                                LFV_CartridgeData icd = new LFV_CartridgeData();
                                icd.m_MuzzleIdx = m;
                                icd.m_Damage = 0;
                                icd.m_AmmoType = intAmmoType;
                                rec.m_InternalMagRounds.Insert(icd);
                            }
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------
        // MAGAZINE ROUNDS -- SAFE VERSION
        //
        // Config-based ammo type, no per-cartridge engine calls.
        // -------------------------------------------------------
        Magazine mag = Magazine.Cast(item);
        if (mag && !item.IsAmmoPile() && !weapon)
        {
            int magCount = mag.GetAmmoCount();
            if (magCount > 0)
            {
                if (magCount > 500) magCount = 500;
                string magAmmoType = SafeGetAmmoType(className);
                if (magAmmoType != "")
                {
                    for (int mi = 0; mi < magCount; mi++)
                    {
                        LFV_CartridgeData mcd = new LFV_CartridgeData();
                        mcd.m_MuzzleIdx = 0;
                        mcd.m_Damage = 0;
                        mcd.m_AmmoType = magAmmoType;
                        rec.m_MagRounds.Insert(mcd);
                    }
                }
            }
        }

        // Agents (disease bitmask)
        rec.m_Agents = item.GetAgents();

        // CombinationLock
        CombinationLock lockItem = CombinationLock.Cast(item);
        if (lockItem)
            rec.m_Combination = lockItem.GetCombination();

        // Cleanness (disinfection state)
        rec.m_Cleanness = item.GetCleanness();

        // Attachments + Cargo (recursive with depth guard)
        GameInventory inv = item.GetInventory();
        if (inv && depth < LFV_Limits.MAX_ITEM_DEPTH)
        {
            int nextDepth = depth + 1;

            // Capture attachments with the ACTUAL slot ID of each attached
            // entity (via GetCurrentInventoryLocation.GetSlot).
            //
            // Do NOT use inv.GetAttachmentSlotId(attachmentIndex): that API
            // expects a slot INDEX (0..GetAttachmentSlotsCount()-1) and
            // returns the slot ID of that slot position, unrelated to
            // GetAttachmentFromIndex. Mixing the two causes a weapon's mag
            // (attachment #0) to be saved with the slot ID of the optics
            // slot on that weapon -- restore then tries to attach the mag
            // to the optics slot, fails, and the fallback chain drops it
            // on the ground. Same pattern for any other attachment.
            int attCount = inv.AttachmentCount();
            for (int a = 0; a < attCount; a++)
            {
                EntityAI attEnt = inv.GetAttachmentFromIndex(a);
                ItemBase attItem = ItemBase.Cast(attEnt);
                if (!attItem) continue;
                GameInventory attInv = attItem.GetInventory();
                if (!attInv) continue;
                InventoryLocation attLoc = new InventoryLocation();
                if (!attInv.GetCurrentInventoryLocation(attLoc)) continue;
                int attSlot = attLoc.GetSlot();
                LFV_ItemRecord attRec = LFV_ItemRecord.FromItem(attItem, LFV_InvType.ATTACHMENT, 0, 0, 0, attSlot, false, nextDepth);
                if (attRec)
                    rec.m_Attachments.Insert(attRec);
            }

            // Cargo (recursive)
            CargoBase cargo = inv.GetCargo();
            if (cargo)
            {
                int cargoCount = cargo.GetItemCount();
                for (int c = 0; c < cargoCount; c++)
                {
                    EntityAI cargoEnt = cargo.GetItem(c);
                    ItemBase cargoItem = ItemBase.Cast(cargoEnt);
                    if (cargoItem)
                    {
                        GameInventory cargoInv = cargoItem.GetInventory();
                        if (!cargoInv) continue;
                        InventoryLocation cargoLoc = new InventoryLocation();
                        // same fix as BuildContainerFile -- fallback to default
                        // position if GetCurrentInventoryLocation fails. Prevents silent
                        // data loss for nested cargo items (items inside items).
                        int cRow = 0;
                        int cCol = 0;
                        int cIdx = c;
                        bool cFlip = false;
                        if (cargoInv.GetCurrentInventoryLocation(cargoLoc))
                        {
                            cRow = cargoLoc.GetRow();
                            cCol = cargoLoc.GetCol();
                            cIdx = cargoLoc.GetIdx();
                            cFlip = cargoLoc.GetFlip();
                        }
                        else
                        {
                            string locWarnNested = "GetCurrentInventoryLocation failed for nested cargo[";
                            locWarnNested = locWarnNested + c.ToString();
                            locWarnNested = locWarnNested + "]: ";
                            locWarnNested = locWarnNested + cargoItem.GetType();
                            locWarnNested = locWarnNested + " -- using fallback position";
                            LFV_Log.Warn(locWarnNested);
                        }
                        LFV_ItemRecord cargoRec = LFV_ItemRecord.FromItem(cargoItem, LFV_InvType.CARGO, cRow, cCol, cIdx, -1, cFlip, nextDepth);
                        if (cargoRec)
                            rec.m_Cargo.Insert(cargoRec);
                    }
                }
            }
        }
        else if (inv && depth >= LFV_Limits.MAX_ITEM_DEPTH)
        {
            string depthMsg = "Max depth reached for ";
            depthMsg = depthMsg + className;
            depthMsg = depthMsg + " at depth ";
            depthMsg = depthMsg + depth.ToString();
            LFV_Log.Warn(depthMsg);
        }

        return rec;
    }
}
