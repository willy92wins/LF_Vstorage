// =========================================================
// LF_VStorage -- CartridgeData model (v1.0)
//
// Data for a single cartridge in a chamber, internal mag,
// or magazine. Serialized campo por campo.
// =========================================================

class LFV_CartridgeData
{
    int     m_MuzzleIdx;
    float   m_Damage;
    string  m_AmmoType;

    void LFV_CartridgeData()
    {
        m_MuzzleIdx = 0;
        m_Damage = 0.0;
        m_AmmoType = "";
    }
}
