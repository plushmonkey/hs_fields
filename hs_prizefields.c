#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hs_fields.h"
#include <string.h>
#include <math.h>

#define MODULE_NAME "hs_prizefields"

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Igame *game;
local Inet *net;
local Iprng *prng;
local Ihsfields *fields;
local Igame *game;
local Ihscoreitems *items;

typedef struct {
    ticks_t end_time;
} PrizePlayerData;

/*********************************/

#define PRIZE_TIME 100

/*********************************/

/**
 * Class property loader called by each field type created of this class.
 */
local void PrizePropertyLoader(Arena *arena, const char *section, HashTable *properties) {
    
}

/**
 * Frees up memory used by the field type. Called when the field type is removed from the arena.
 */
local void PrizePropertyCleanup(Arena *arena, HashTable *properties) {
    
}

/**
 * Called when a field instance is created.
 */
local void PrizeInstanceConstructor(HSFieldInstance *inst) {
    inst->data = HashAlloc();
}

/**
 * Called when a field instance gets updated.
 */
local void PrizeInstanceUpdate(HSFieldInstance *inst) {
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
        
        // Update if they are inside the field
        if (InSquare(p->arena, p->p_ship, inst->x, inst->y, inst->type->radius, p->position.x, p->position.y)) {
            int bounce = items->getPropertySum(p, p->p_ship, "bounce", 0);
            
            if (bounce > 0) continue;
            
            PrizePlayerData *pdata = HashGetOne(inst->data, p->name);
            
            if (!pdata) {
                // Only prize them if they aren't already prized
                pdata = amalloc(sizeof(PrizePlayerData));
                HashAdd(inst->data, p->name, pdata);
            
                Target target;
                target.type = T_PLAYER;
                target.u.p = p;
                
                game->GivePrize(&target, 10, 1);
            }
            
            // set or reset end timer if they are inside the field
            pdata->end_time = current_ticks() + PRIZE_TIME;
        }
        
        // Check if any players need to be deprized
        PrizePlayerData *remdata = HashGetOne(inst->data, p->name);
        if (remdata) {
            if (current_ticks() >= remdata->end_time) {
                HashRemove(inst->data, p->name, remdata);
                afree(remdata);
                Target target;
                target.type = T_PLAYER;
                target.u.p = p;
                game->GivePrize(&target, -10, -1);
            }
        }
    }
    pd->Unlock();
}

/**
 * Called when a field instance is destroyed.
 */
void PrizeInstanceDestructor(HSFieldInstance *inst) {
    // remove any prizes
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
            
        PrizePlayerData *pdata = HashGetOne(inst->data, p->name);
        if (pdata) {
            Target target;
            target.type = T_PLAYER;
            target.u.p = p;
            game->GivePrize(&target, -10, -1);
        }
    }
    pd->Unlock();
    
    HashEnum(inst->data, hash_enum_afree, NULL);
    HashFree(inst->data);
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
        pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
        game = mm->GetInterface(I_GAME, ALLARENAS);
        net = mm->GetInterface(I_NET, ALLARENAS);
        prng = mm->GetInterface(I_PRNG, ALLARENAS);
        game = mm->GetInterface(I_GAME, ALLARENAS);
        items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
        
        fields = mm->GetInterface(I_HSFIELDS, ALLARENAS);

        return mm && lm && cfg && pd && game && net && prng && fields && game && items;
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
        mm->ReleaseInterface(pd);
        mm->ReleaseInterface(game);
        mm->ReleaseInterface(net);
        mm->ReleaseInterface(prng);
        mm->ReleaseInterface(fields);
        mm->ReleaseInterface(game);
        mm->ReleaseInterface(items);

        mm = NULL;
    }
}

HSFieldClass prize_class = {
    PrizePropertyLoader,
    PrizePropertyCleanup,
    PrizeInstanceConstructor,
    PrizeInstanceUpdate,
    PrizeInstanceDestructor
};

EXPORT const char info_hs_prizefields[] = "v1.0 by monkey, based on hs_field v1.01 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_prizefields(int action, Imodman *mm_, Arena *arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
            if (!GetInterfaces(mm_)) {
                lm->Log(L_ERROR, "<%s> Unable to get required interfaces.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }

            fields->RegisterFieldClass("prize", &prize_class);
            rv = MM_OK;

        break;
        case MM_ATTACH:
            rv = MM_OK;

        break;
        case MM_DETACH:
            rv = MM_OK;

        break;
        case MM_UNLOAD:
            fields->UnregisterFieldClass("prize");
            ReleaseInterfaces();
            rv = MM_OK;

        break;
    }

    return rv;
}