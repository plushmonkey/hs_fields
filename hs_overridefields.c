#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hs_fields.h"
#include "hscore_spawner.h"
#include <string.h>
#include <math.h>

#define MODULE_NAME "hs_overridefields"

local Imodman *mm;
local Ilogman *lm;
local Iarenaman *aman;
local Iconfig *cfg;
local Iplayerdata *pd;
local Igame *game;
local Inet *net;
local Iprng *prng;
local Ihsfields *fields;
local Ichat *chat;

typedef struct {
    char name[80];
    int new_value;
} OverrideData;
typedef struct {
    ticks_t end_time;
} InstancePlayerData;

typedef struct {
    HashTable *overrides;
} OverridePlayerData;
local int pdkey = -1;

typedef struct {
    Ihscorespawner *spawner;
} OverrideArenaData;
local int adkey = -1;

/*********************************/

/*********************************/

/**
 * Class property loader called by each field type created of this class.
 */
local void OverridePropertyLoader(Arena *arena, const char *section, HashTable *properties) {
    LinkedList *overrides = LLAlloc();
    
    OverrideData *data = amalloc(sizeof(OverrideData));
    strcpy(data->name, "speed_actual");
    data->new_value = 200;
    
    LLAdd(overrides, data);
    
    data = amalloc(sizeof(OverrideData));
    strcpy(data->name, "maxspeed_actual");
    data->new_value = 200;
    LLAdd(overrides, data);
    
    HashAdd(properties, "overrides", overrides);
}

/**
 * Frees up memory used by the field type. Called when the field type is removed from the arena.
 */
local void OverridePropertyCleanup(Arena *arena, HashTable *properties) {
    LinkedList *overrides = HashGetOne(properties, "overrides");
    
    if (overrides)
        LLFree(overrides);
}

/**
 * Called when a field instance is created.
 */
local void OverrideInstanceConstructor(HSFieldInstance *inst) {
    inst->data = HashAlloc();
}

local void AddOverrides(Player *p, LinkedList *overrides) {
    OverridePlayerData *pdata = PPDATA(p, pdkey);
    
    if (!pdata->overrides)
        pdata->overrides = HashAlloc();
        
    Link *link;
    OverrideData *data;
    
    FOR_EACH(overrides, data, link) {
        HashReplace(pdata->overrides, data->name, data);
    }
}

local void RemoveOverrides(Player *p, LinkedList *overrides) {
    OverridePlayerData *pdata = PPDATA(p, pdkey);
    
    if (!pdata->overrides)
        pdata->overrides = HashAlloc();
        
    Link *link;
    OverrideData *data;
    
    FOR_EACH(overrides, data, link) {
        HashRemove(pdata->overrides, data->name, data);
    }
}

/**
 * Called when a field instance gets updated.
 */
local void OverrideInstanceUpdate(HSFieldInstance *inst) {
    Player *p;
    Link *link;

    pd->Lock();
    FOR_EACH_PLAYER_IN_ARENA(p, inst->arena) {
        if (HS_IS_SPEC(p))
            continue;
        if (!HS_IS_ON_FREQ(p, inst->arena, inst->player->pkt.freq))
            continue;
        if (p->flags.is_dead)
            continue;
        
        OverrideArenaData *adata = P_ARENA_DATA(inst->arena, adkey);
        
        if (!adata->spawner) continue;
        
        // Update if they are inside the field
        if (InSquare(p->arena, p->p_ship, inst->x, inst->y, inst->type->radius, p->position.x, p->position.y)) {
            InstancePlayerData *ipdata = HashGetOne(inst->data, p->name);
            
            if (!ipdata) {
                ipdata = amalloc(sizeof(InstancePlayerData));
                HashAdd(inst->data, p->name, ipdata);
            
                ipdata->end_time = current_ticks() + 100;
                
                LinkedList *overrides = HashGetOne(&inst->type->properties, "overrides");
                
                AddOverrides(p, overrides);
                
                adata->spawner->resendOverrides(p);
            }
            
            ipdata->end_time = current_ticks() + 100;
        }
        
        InstancePlayerData *ipdata = HashGetOne(inst->data, p->name);
        
        if (ipdata && current_ticks() >= ipdata->end_time) {
            LinkedList *overrides = HashGetOne(&inst->type->properties, "overrides");
            
            RemoveOverrides(p, overrides);
            adata->spawner->resendOverrides(p);
            
            HashRemove(inst->data, p->name, ipdata);
            afree(ipdata);
        }
    }
    pd->Unlock();
}

/**
 * Called when a field instance is destroyed.
 */
void OverrideInstanceDestructor(HSFieldInstance *inst) {
    Player *p;
    Link *link;
    pd->Lock();
    FOR_EACH_PLAYER_IN_ARENA(p, inst->arena) {
        if (HS_IS_SPEC(p))
            continue;
        if (!HS_IS_ON_FREQ(p, inst->arena, inst->player->pkt.freq))
            continue;
        if (p->flags.is_dead)
            continue;
        
        OverrideArenaData *adata = P_ARENA_DATA(inst->arena, adkey);
        if (!adata->spawner) continue;        
        
        InstancePlayerData *ipdata = HashGetOne(inst->data, p->name);
        LinkedList *overrides = HashGetOne(&inst->type->properties, "overrides");
        
        RemoveOverrides(p, overrides);
        adata->spawner->resendOverrides(p);
    }
    pd->Unlock();
    
    HashEnum(inst->data, hash_enum_afree, NULL);
    HashFree(inst->data);
}

local void OnPlayerAction(Player *p, int action, Arena *a) {
    OverridePlayerData *pdata = PPDATA(p, pdkey);
    
    if (action == PA_ENTERARENA) {
        pdata->overrides = HashAlloc();
    } else if (action == PA_LEAVEARENA) {
        HashEnum(pdata->overrides, hash_enum_afree, NULL);
        HashFree(pdata->overrides);
    }
}

local int GetOverrideValue(Player *p, int ship, int shipset, const char *prop, int init_value) {
    OverridePlayerData *pdata = PPDATA(p, pdkey);
    if (!pdata->overrides)
        pdata->overrides = HashAlloc();
    OverrideData *data = HashGetOne(pdata->overrides, prop);
    int rv = init_value;
    
    if (data) {
        rv = data->new_value;
     //   chat->SendMessage(p, "Overriding %s from %d to %d.", prop, init_value, rv);
    }
    
    return rv;
}

/*******************************/

/**
 * Gets all the required interfaces.
 */
local int GetInterfaces(Imodman *mm_) {
    if (mm_ && !mm) {
        mm = mm_;

        lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
        cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
        aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
        pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
        game = mm->GetInterface(I_GAME, ALLARENAS);
        net = mm->GetInterface(I_NET, ALLARENAS);
        prng = mm->GetInterface(I_PRNG, ALLARENAS);
        fields = mm->GetInterface(I_HSFIELDS, ALLARENAS);
        chat = mm->GetInterface(I_CHAT, ALLARENAS);

        return mm && lm && cfg && aman && pd && game && net && prng && fields && chat;
    }

    return 0;
}

/**
 * Releases all of the used interfaces.
 */
local void ReleaseInterfaces() {
    if (mm) {
        mm->ReleaseInterface(lm);
        mm->ReleaseInterface(cfg);
        mm->ReleaseInterface(aman);
        mm->ReleaseInterface(pd);
        mm->ReleaseInterface(game);
        mm->ReleaseInterface(net);
        mm->ReleaseInterface(prng);
        mm->ReleaseInterface(fields);
        mm->ReleaseInterface(chat);

        mm = NULL;
    }
}

HSFieldClass override_class = {
    OverridePropertyLoader,
    OverridePropertyCleanup,
    OverrideInstanceConstructor,
    OverrideInstanceUpdate,
    OverrideInstanceDestructor
};

local Ahscorespawner myspawner = {
    ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)
    
    GetOverrideValue
};

EXPORT const char info_hs_overridefields[] = "v1.0 by monkey, based on hs_field v1.01 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_overridefields(int action, Imodman *mm_, Arena *arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
            if (!GetInterfaces(mm_)) {
                lm->Log(L_ERROR, "<%s> Unable to get required interfaces.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }
            
            adkey = aman->AllocateArenaData(sizeof(OverrideArenaData));
            if (adkey == -1) {
                ReleaseInterfaces();
                break;
            }
            
            pdkey = pd->AllocatePlayerData(sizeof(OverridePlayerData));
            if (pdkey == -1) {
                aman->FreeArenaData(adkey);
                ReleaseInterfaces();
                break;
            }

            fields->RegisterFieldClass("override", &override_class);
            
            rv = MM_OK;
        break;
        case MM_ATTACH:
        {
            OverrideArenaData *adata = P_ARENA_DATA(arena, adkey);
            
            adata->spawner = mm->GetArenaInterface(I_HSCORE_SPAWNER, arena);
            mm->RegCallback(CB_PLAYERACTION, OnPlayerAction, arena);
            
            mm->RegAdviser(&myspawner, arena);
            rv = MM_OK;

        }
        break;
        case MM_DETACH:
        {
            OverrideArenaData *adata = P_ARENA_DATA(arena, adkey);
            
            mm->ReleaseArenaInterface(adata->spawner, arena);
            
            mm->UnregCallback(CB_PLAYERACTION, OnPlayerAction, arena);
            
            mm->UnregAdviser(&myspawner, arena);
            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
            aman->FreeArenaData(adkey);
            pd->FreePlayerData(pdkey);
            
            fields->UnregisterFieldClass("override");
            ReleaseInterfaces();
            rv = MM_OK;

        break;
    }

    return rv;
}