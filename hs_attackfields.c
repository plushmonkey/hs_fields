#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hs_fields.h"
#include <string.h>
#include <math.h>

#define MODULE_NAME "hs_attackfields"

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Igame *game;
local Inet *net;
local Iprng *prng;
local Ihsfields *fields;

/*********************************/

local int RandomRotation() {
    return prng->Number(0, 39);
}

local int DetermineRotation(int xspeed, int yspeed) {
    yspeed *= -1;

    if (!xspeed) {
        if (yspeed >= 0)
            return 0;
        else
            return 20;
    } else if (!yspeed) {
        if (xspeed >= 0)
            return 10;
        else
            return 30;
    } else {
        double degrees;
        int ssru;
        double theta = -atan((double)yspeed / (double)xspeed);
        theta += (M_PI / 2);

        if (xspeed < 0)
            theta += M_PI;

        degrees = theta * 57.2957795130823;
        ssru = (int)(degrees / 9.0);

        return ssru;
    }
    return 0;
}

local void FireWeapon(Player *victim, HSFieldInstance *inst) {
    unsigned status = STATUS_STEALTH | STATUS_CLOAK | STATUS_UFO;
    struct S2CWeapons packet = {
        S2C_WEAPON, victim->position.rotation, current_ticks() & 0xFFFF, victim->position.x, victim->position.yspeed,
        inst->fake->pid, victim->position.xspeed, 0, status, 0,
        victim->position.y, 10
    };

    int *track = (int *)HashGetOne(&inst->type->properties, "track");

    if (track && *track)
        packet.rotation = RandomRotation();
    else
        packet.rotation = DetermineRotation(packet.xspeed, packet.yspeed);

    inst->fake->position.x = packet.x;
    inst->fake->position.y = packet.y;

    packet.weapon = *((struct Weapons *)HashGetOne(&inst->type->properties, "weapon"));

    game->DoWeaponChecksum(&packet);
    net->SendToOne(victim, (byte *)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);

    // Clear fake player position so players don't hit it with their own weapons
    packet.x = 0;
    packet.y = 0;
    packet.xspeed = 0;
    packet.yspeed = 0;
    packet.rotation = 0;
    packet.weapon.type = W_NULL;
    packet.time = TICK_MAKE(packet.time + 1) & 0xFFFF;
    inst->fake->position.x = 0;
    inst->fake->position.y = 0;

    game->DoWeaponChecksum(&packet);
    net->SendToOne(victim, (byte *)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
}

local int ParseWeapon(const char *weapon, struct Weapons *wpn) {
    if (weapon) {
        if (strstr(weapon, "level2"))
            wpn->level = 1;
        else if (strstr(weapon, "level3"))
            wpn->level = 2;
        else if (strstr(weapon, "level4"))
            wpn->level = 3;

        if (strstr(weapon, "gun")) {
            if (strstr(weapon, "bounce"))
                wpn->type = W_BOUNCEBULLET;
            if (strstr(weapon, "multi"))
                wpn->alternate = 1;
        } else if (strstr(weapon, "bomb")) {
            if (strstr(weapon, "thor"))
                wpn->type = W_THOR;
            else if (strstr(weapon, "prox"))
                wpn->type = W_PROXBOMB;
            else
                wpn->type = W_BOMB;

            if (strstr(weapon, "shrap")) {
                wpn->shrap = 31;

                if (strstr(weapon, "shrap2"))
                    wpn->shraplevel = 1;
                else if (strstr(weapon, "shrap3"))
                    wpn->shraplevel = 2;
                else if (strstr(weapon, "shrap4"))
                    wpn->shraplevel = 3;

                if (strstr(weapon, "bounce"))
                    wpn->shrapbouncing = 1;
            }
        } else if (strstr(weapon, "repel")) {
            wpn->type = W_REPEL;
        } else if (strstr(weapon, "burst")) {
            wpn->type = W_BURST;
        }

        return 1;
    }

    return 0;
}

/*********************************/

local void AttackPropertyLoader(Arena *arena, const char *section, HashTable *properties) {
    const char *weapon = cfg->GetStr(arena->cfg, section, "weapon");
    struct Weapons *wpn = amalloc(sizeof(struct Weapons));

    wpn->alternate = 0;
    wpn->level = 0;
    wpn->shrap = 0;
    wpn->shrapbouncing = 0;
    wpn->shraplevel = 0;
    wpn->type = W_BULLET;

    if (!ParseWeapon(weapon, wpn))
        lm->LogA(L_INFO, MODULE_NAME, arena, "No weapon property defined for attack field %s.", section);

    HashAdd(properties, "weapon", wpn);

    int *track = amalloc(sizeof(int));

    *track = cfg->GetInt(arena->cfg, section, "track", 0);

    HashAdd(properties, "track", track);
}

local void AttackPropertyCleanup(Arena *arena, HashTable *properties) {
    struct Weapons *wpn = HashGetOne(properties, "weapon");
    int *track = HashGetOne(properties, "track");

    HashRemoveAny(properties, "weapon");
    HashRemoveAny(properties, "track");

    afree(wpn);
    afree(track);
}

local void AttackInstanceConstructor(HSFieldInstance *inst) {
    
}

local void AttackInstanceTimer(HSFieldInstance *inst) {
    Player *p;
    Link *link;

    pd->Lock();
    FOR_EACH_PLAYER_IN_ARENA(p, inst->arena) {
        if (HS_IS_SPEC(p))
            continue;
        if (HS_IS_ON_FREQ(p, inst->arena, inst->player->pkt.freq))
            continue;
        if (p->flags.is_dead)
            continue;
      if (InSquare(p->arena, p->p_ship, inst->x, inst->y, inst->type->radius, p->position.x, p->position.y))
          FireWeapon(p, inst);
    }
    pd->Unlock();
}

void AttackInstanceDestructor(HSFieldInstance *inst) {
    
}

/*******************************/

local int GetInterfaces(Imodman *mm_) {
    if (mm_ && !mm) {
        mm = mm_;

        lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
        cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
        pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
        game = mm->GetInterface(I_GAME, ALLARENAS);
        net = mm->GetInterface(I_NET, ALLARENAS);
        prng = mm->GetInterface(I_PRNG, ALLARENAS);
        
        fields = mm->GetInterface(I_HSFIELDS, ALLARENAS);

        return mm && lm && cfg && pd && game && net && prng && fields;
    }

    return 0;
}

local void ReleaseInterfaces() {
    if (mm) {
        mm->ReleaseInterface(lm);
        mm->ReleaseInterface(cfg);
        mm->ReleaseInterface(pd);
        mm->ReleaseInterface(game);
        mm->ReleaseInterface(net);
        mm->ReleaseInterface(prng);
        mm->ReleaseInterface(fields);

        mm = NULL;
    }
}

HSFieldClass attack_class = {
    AttackPropertyLoader,
    AttackPropertyCleanup,
    AttackInstanceConstructor,
    AttackInstanceTimer,
    AttackInstanceDestructor
};

EXPORT const char info_hs_attackfields[] = "v1.0 by monkey, based on hs_field v1.01 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_attackfields(int action, Imodman *mm_, Arena *arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
            if (!GetInterfaces(mm_)) {
                lm->Log(L_ERROR, "<%s> Unable to get required interfaces.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }

            fields->RegisterFieldClass("attack", &attack_class);
            rv = MM_OK;

        break;
        case MM_ATTACH:
            rv = MM_OK;

        break;
        case MM_DETACH:
            rv = MM_OK;

        break;
        case MM_UNLOAD:
            fields->UnregisterFieldClass("attack");
            ReleaseInterfaces();
            rv = MM_OK;

        break;
    }

    return rv;
}