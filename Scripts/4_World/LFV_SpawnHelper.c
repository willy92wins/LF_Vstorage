// =========================================================
// LF_VStorage -- SpawnHelper (v1.1)
//
// Unified spawn API wrapping LocationCreateEntity.
// RestoreQueue items are persistent (engine saves them).
// DropQueue items use SpawnOnGround (always persistent).
// Anti-duplication relies on immediate .lfv deletion in OnComplete,
// NOT on NOPERSISTENCY flags.
//
// Fallback chain: original position -> auto-place -> ground.
//
// Also handles applying item properties (health, quantity,
// temperature, wetness, energy, ammo, food stage) and
// recursive restoration of attachments/cargo.
// =========================================================

class LFV_SpawnHelper
{
    // -----------------------------------------------------------
    // Spawn in cargo at exact grid position
    // -----------------------------------------------------------
    static ItemBase SpawnInCargo(ItemBase container, LFV_ItemRecord record)
    {
        InventoryLocation loc = new InventoryLocation();
        loc.SetCargo(container, null, record.m_Idx, record.m_Row, record.m_Col, record.m_Flipped);
        int flags = LFV_ECE.IN_INVENTORY;
        EntityAI ent = GameInventory.LocationCreateEntity(loc, record.m_Classname, flags, LFV_RF.DEFAULT);
        return ItemBase.Cast(ent);
    }

    // -----------------------------------------------------------
    // Spawn as attachment in specific slot
    // -----------------------------------------------------------
    static ItemBase SpawnAsAttachment(ItemBase container, LFV_ItemRecord record)
    {
        InventoryLocation loc = new InventoryLocation();
        loc.SetAttachment(container, null, record.m_SlotId);
        int flags = LFV_ECE.IN_INVENTORY;
        EntityAI ent = GameInventory.LocationCreateEntity(loc, record.m_Classname, flags, LFV_RF.DEFAULT);
        return ItemBase.Cast(ent);
    }

    // -----------------------------------------------------------
    // Spawn on ground near position (persistent).
    // Used by RestoreQueue fallback and DropQueue.
    // unified from two identical methods.
    // -----------------------------------------------------------
    static ItemBase SpawnOnGround(string classname, vector pos)
    {
        InventoryLocation loc = new InventoryLocation();
        vector mat[4];
        Math3D.MatrixIdentity4(mat);
        mat[3] = pos;
        loc.SetGround(null, mat);
        int flags = LFV_ECE.PLACE_ON_SURFACE;
        EntityAI ent = GameInventory.LocationCreateEntity(loc, classname, flags, LFV_RF.DEFAULT);
        return ItemBase.Cast(ent);
    }

    // -----------------------------------------------------------
    // Spawn with fallback: 5-level chain to prevent item loss
    //
    // Level 1:   Exact saved position (row/col/idx/flip)
    // Level 1.5: Opposite orientation (flip retry)
    // Level 2:   FindFirstFreeLocationForNewEntity (vanilla)
    // Level 2.5: Brute-force cargo grid scan (workaround vanilla bug)
    // Level 3:   Ground drop (last resort)
    //
    // FindFirstFreeLocationForNewEntity has a known vanilla bug
    // (confirmed by DayZ-Expansion: "Added CreateInInventoryEx to
    // circumvent vanilla issues with FindFirstFreeLocationForNewEntity").
    // Level 2.5 covers this gap by trying every grid position.
    // -----------------------------------------------------------
    static ItemBase SpawnWithFallback(ItemBase container, LFV_ItemRecord record)
    {
        ItemBase item = null;

        // Level 1: Try exact saved position
        if (record.m_InvType == LFV_InvType.ATTACHMENT)
        {
            item = SpawnAsAttachment(container, record);
        }
        else
        {
            item = SpawnInCargo(container, record);
        }

        // Level 1.5: Try opposite orientation (flip)
        if (!item && record.m_InvType == LFV_InvType.CARGO)
        {
            InventoryLocation flipLoc = new InventoryLocation();
            bool flipped = !record.m_Flipped;
            flipLoc.SetCargo(container, null, record.m_Idx, record.m_Row, record.m_Col, flipped);
            int flipFlags = LFV_ECE.IN_INVENTORY;
            EntityAI flipEnt = GameInventory.LocationCreateEntity(flipLoc, record.m_Classname, flipFlags, LFV_RF.DEFAULT);
            item = ItemBase.Cast(flipEnt);
        }

        // Level 2: FindFirstFreeLocationForNewEntity (vanilla, has known bug)
        if (!item)
        {
            InventoryLocation autoLoc = new InventoryLocation();
            int findFlags = FindInventoryLocationType.CARGO | FindInventoryLocationType.ATTACHMENT;
            if (container.GetInventory().FindFirstFreeLocationForNewEntity(record.m_Classname, findFlags, autoLoc))
            {
                int eceFlags = LFV_ECE.IN_INVENTORY;
                EntityAI autoEnt = GameInventory.LocationCreateEntity(autoLoc, record.m_Classname, eceFlags, LFV_RF.DEFAULT);
                item = ItemBase.Cast(autoEnt);
            }
        }

        // Level 2.5: Brute-force cargo grid scan (workaround vanilla bug)
        if (!item)
        {
            item = BruteForceCargo(container, record.m_Classname);
            if (item)
            {
                string bfMsg = record.m_Classname;
                bfMsg = bfMsg + " placed via brute-force grid scan";
                LFV_Log.Info(bfMsg);
            }
        }

        // Level 3: Ground drop (last resort)
        if (!item)
        {
            item = SpawnOnGround(record.m_Classname, container.GetPosition());
            if (item)
            {
                string dropMsg = record.m_Classname;
                dropMsg = dropMsg + " dropped to ground -- no space in container";
                LFV_Log.Warn(dropMsg);
            }
        }

        return item;
    }

    // -----------------------------------------------------------
    // Brute-force cargo grid scan.
    //
    // Workaround for vanilla FindFirstFreeLocationForNewEntity bug.
    // Reads cargo grid dimensions from CfgVehicles.itemsCargoSize,
    // then tries every (row, col) with both orientations until
    // LocationCreateEntity succeeds.
    //
    // Performance: worst case is gridW*gridH*2 attempts, but this
    // only runs as a last-resort fallback. Typical barrel grids
    // are 10x5 (100 attempts max) or 10x100 (2000 max).
    // -----------------------------------------------------------
    static ItemBase BruteForceCargo(ItemBase container, string classname)
    {
        CargoBase cargo = container.GetInventory().GetCargo();
        if (!cargo) return null;

        // Read cargo grid dimensions from config
        TIntArray cargoSize = new TIntArray;
        string cfgPath = "CfgVehicles " + container.GetType() + " itemsCargoSize";
        GetGame().ConfigGetIntArray(cfgPath, cargoSize);
        if (cargoSize.Count() < 2) return null;
        int gridW = cargoSize[0];
        int gridH = cargoSize[1];

        // early-exit if cargo is full (avoids gridW*gridH*2 engine calls)
        int maxSlots = gridW * gridH;
        if (cargo.GetItemCount() >= maxSlots)
            return null;

        int eceFlags = LFV_ECE.IN_INVENTORY;

        // Try every position with both orientations
        for (int row = 0; row < gridH; row++)
        {
            for (int col = 0; col < gridW; col++)
            {
                // Try without flip
                InventoryLocation loc1 = new InventoryLocation();
                loc1.SetCargo(container, null, 0, row, col, false);
                EntityAI ent1 = GameInventory.LocationCreateEntity(loc1, classname, eceFlags, LFV_RF.DEFAULT);
                if (ent1)
                    return ItemBase.Cast(ent1);

                // Try with flip
                InventoryLocation loc2 = new InventoryLocation();
                loc2.SetCargo(container, null, 0, row, col, true);
                EntityAI ent2 = GameInventory.LocationCreateEntity(loc2, classname, eceFlags, LFV_RF.DEFAULT);
                if (ent2)
                    return ItemBase.Cast(ent2);
            }
        }

        return null;
    }

    // -----------------------------------------------------------
    // Apply properties to a spawned item from its record
    // -----------------------------------------------------------
    static void ApplyProperties(ItemBase item, LFV_ItemRecord record)
    {
        if (!item) return;

        // Health
        string emptyStr = "";
        string healthStr = "Health";
        item.SetHealth(emptyStr, healthStr, record.m_Health);

        // Quantity + liquid
        if (item.HasQuantity())
        {
            // All params false: prevent auto-destroy, client notify, and stack clamping during restore
            item.SetQuantity(record.m_Quantity, false, false, false, false);
            if (record.m_LiquidType > 0)
                item.SetLiquidType(record.m_LiquidType);
        }

        // Food stage
        Edible_Base edible = Edible_Base.Cast(item);
        if (edible && edible.HasFoodStage())
        {
            FoodStage fs = edible.GetFoodStage();
            if (fs)
                fs.SetFoodStageType(record.m_FoodStage);
        }

        // Temperature
        item.SetTemperature(record.m_Temperature);

        // Wetness
        item.SetWet(record.m_Wetness);

        // Energy
        if (record.m_HasEnergy && item.HasEnergyManager())
        {
            item.GetCompEM().SetEnergy(record.m_Energy);
        }

        // Ammo (ammo piles)
        if (item.IsAmmoPile())
        {
            Magazine ammoPile = Magazine.Cast(item);
            if (ammoPile)
                ammoPile.ServerSetAmmoCount(record.m_AmmoCount);
        }

        // Magazine rounds (not ammo pile, not weapon)
        Weapon_Base weapon = Weapon_Base.Cast(item);
        Magazine mag = Magazine.Cast(item);
        if (mag && !item.IsAmmoPile() && !weapon)
        {
            // Clear existing rounds first
            mag.ServerSetAmmoCount(0);
            // Re-insert saved rounds
            for (int mi = 0; mi < record.m_MagRounds.Count(); mi++)
            {
                LFV_CartridgeData mcd = record.m_MagRounds[mi];
                mag.ServerStoreCartridge(mcd.m_Damage, mcd.m_AmmoType);
            }
        }

        // Weapon chamber + internal magazine
        if (weapon)
        {
            // Chamber rounds
            for (int ci = 0; ci < record.m_ChamberRounds.Count(); ci++)
            {
                LFV_CartridgeData crd = record.m_ChamberRounds[ci];
                weapon.PushCartridgeToChamber(crd.m_MuzzleIdx, crd.m_Damage, crd.m_AmmoType);
            }

            // Internal magazine rounds
            for (int ii = 0; ii < record.m_InternalMagRounds.Count(); ii++)
            {
                LFV_CartridgeData ird = record.m_InternalMagRounds[ii];
                weapon.PushCartridgeToInternalMagazine(ird.m_MuzzleIdx, ird.m_Damage, ird.m_AmmoType);
            }
        }

        // Agents (disease bitmask) -- FORMAT >= 3
        if (record.m_Agents != 0)
            item.TransferAgents(record.m_Agents);

        // CombinationLock -- FORMAT >= 3
        CombinationLock lockItem = CombinationLock.Cast(item);
        if (lockItem && record.m_Combination != 0)
            lockItem.SetCombination(record.m_Combination);

        // Cleanness (disinfection) -- FORMAT >= 3
        if (record.m_Cleanness != 0)
            item.SetCleanness(record.m_Cleanness);
    }

}
