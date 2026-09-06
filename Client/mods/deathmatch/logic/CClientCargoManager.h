#pragma once

#include <vector>
#include <CVector.h>

class CClientEntity;
class CClientPed;
class CClientObject;
class CLuaMain;

// Leases belong to the submitting Lua VM, not to the lifetime of its target
// elements. Stopping a resource must release even another resource's object.
class CClientCargoManager
{
public:
    static CClientCargoManager& GetSingleton();
    bool                        Start(CLuaMain* owner, CClientPed* ped, CClientObject* object);
    bool                        PutDown(CLuaMain* owner, CClientPed* ped);
    bool                        Cancel(CLuaMain* owner, CClientPed* ped);
    CClientObject*              GetObject(CClientPed* ped) const;
    const char*                 GetState(CClientPed* ped) const;
    void                        OnEntityDestroy(CClientEntity* entity);
    void                        OnLuaMainDestroy(CLuaMain* owner);
    void                        DoPulse();

private:
    struct Entry
    {
        CVector        recoveryPosition;
        CLuaMain*      owner;
        CClientPed*    ped;
        CClientObject* object;
        float          health;
        long long      started;
        long long      putDownRequested{};
        long long      airborneSince{};
        int            state{3};
        bool           frozen;
    };
    Entry*             Find(CClientPed* ped);
    void               Release(CClientPed* ped, const char* reason, bool notify);
    std::vector<Entry> m_entries;
};
