#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_items.h"
#include "hs_fields.h"
#include <stdio.h>
#include <strings.h> // strcasecmp
#include <ctype.h> // tolower

#define MODULE_NAME "hs_fields"

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Icmdman *cmd;
local Ichat *chat;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;
local Iobjects *obj;
local Ifake *fake;
local Ihscoreitems *items;

/*********************************/

typedef signed char i8;
typedef unsigned char u8;

#define HSFIELD_NAME_SIZE 32

typedef struct HSFieldArenaData {
    LinkedList fields;
    LinkedList instances;
    int cfgShipRadius[8];
} HSFieldArenaData;
local int adkey;

typedef struct HSFieldPlayerData {
    u8 dead     : 1;
    u8 buffer   : 7;

    ticks_t lastField;
} HSFieldPlayerData;
local int pdkey;

/*******************************/

local pthread_mutexattr_t pthread_attr;
local pthread_mutex_t pthread_mutex;

local HashTable g_fieldClasses;

/*******************************/

// Field iterate functions
typedef int(*HSFieldIterateFunc)(LinkedList *fields, HSField *field, const void *extra);
local HSField *HSFieldIterate(LinkedList *fields, HSFieldIterateFunc func, const void *extra);

local int GetFieldByName(LinkedList *fields, HSField *field, const void *name);
local int GetFieldByPropertyValue(LinkedList *fields, HSField *field, const void *value);
local int UpdateNextLVZId(LinkedList *fields, HSField *field, const void *array);
local int UnloadFields(LinkedList *list, HSField *field, const void *arena);

// Field instance iterate functions
typedef int(HSFieldInstanceFunc)(LinkedList *list, HSFieldInstance *instance, const void *extra);
local HSFieldInstance *HSFieldInstanceIterate(LinkedList *, HSFieldInstanceFunc func, const void *extra);

local int GetOneInstanceFromPlayer(LinkedList *, HSFieldInstance *, const void *player);
local int RemoveAllInstancesFromPlayer(LinkedList *, HSFieldInstance *, const void *player);

// Other functions
local void BeginFieldInstance(Arena *arena, Player *p, HSField *type);
local void EndFieldInstance(Arena *arena, HSFieldInstance *inst);
local int HandleRespawn(void *_p);
local int UpdateFieldInstance(void *param);
local int LoadField(Arena *arena, char *cfgname);
local int LoadFields(Arena *arena);

// Callbacks
local void OnShipFreqChange(Player *p, int newShip, int oldShip, int newFreq, int oldFreq);
local void OnPlayerAction(Player *p, int action, Arena *arena);
local void OnPlayerKill(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);

// Interface functions
local int RegisterFieldClass(const char *className, HSFieldClass *fieldClass);
local void UnregisterFieldClass(const char *className);

/********************************/

local HSField *HSFieldIterate(LinkedList *list, HSFieldIterateFunc func, const void *extra) {
    HSField *result = NULL, *data = NULL;
    Link *link;

    pthread_mutex_lock(&pthread_mutex);
    FOR_EACH(list, data, link) {
        int found = func(list, data, extra);
        if (found) {
            result = data;
            break;
        }
    }
    pthread_mutex_unlock(&pthread_mutex);
    return result;
}

local int GetFieldByName(LinkedList *fields, HSField *field, const void *name) {
    return strcasecmp(field->name, (char *)name) == 0;
}

local int GetFieldByPropertyValue(LinkedList *fields, HSField *field, const void *value) {
    int *pVal = (int *)value;
    return field->property == *pVal;
}

local int UpdateNextLVZId(LinkedList *fields, HSField *field, const void *array) {
    int *LVZId = (int *)array;
    
    for (int i = 0; i < 4; i++) {
        if (field->LVZIdBase[i] != LVZId[i])
            continue;
        field->nextLVZId[i]++;
        if (field->nextLVZId[i] >= field->LVZIdBase[i] + field->maxLVZIds)
            field->nextLVZId[i] = field->LVZIdBase[i];
    }
    return 0;
}

local int UnloadFields(LinkedList *list, HSField *field, const void *arena) {
    afree(field);
    return 0;
}

/********************************/

local HSFieldInstance *HSFieldInstanceIterate(LinkedList *list, HSFieldInstanceFunc func, const void *extra) {
    HSFieldInstance *result = NULL, *data = NULL;
    Link *link;

    pthread_mutex_lock(&pthread_mutex);
    FOR_EACH(list, data, link) {
        int found = func(list, data, extra);
        if (found) {
            result = data;
            break;
        }
    }
    pthread_mutex_unlock(&pthread_mutex);
    return result;
}

local int GetOneInstanceFromPlayer(LinkedList *list, HSFieldInstance *instance, const void *player) {
    return instance->player == player;
}

local int RemoveAllInstancesFromPlayer(LinkedList *list, HSFieldInstance *instance, const void *player) {
    if (player && (instance->player != player))
        return 0;

    EndFieldInstance(instance->arena, instance);
    return 0;
}

/********************************/

int InSquare(Arena *arena, int ship, int sx, int sy, int r, int x, int y) {
    HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);
    
    if (ship < SHIP_WARBIRD || ship > SHIP_SHARK)
        return 0;

    int R = adata->cfgShipRadius[ship];

    if ((x + R) < (sx - r))
        return 0;
    if ((x - R) > (sx + r))
        return 0;
    if ((y + R) < (sy - r))
        return 0;
    if ((y - R) > (sy + r))
        return 0;

    return 1;
}

/*******************************/

local int LoadField(Arena *arena, char *cfgname) {
    HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);
    HSField *field = amalloc(sizeof(HSField));
    char buffer[256];

    sprintf(buffer, "field-%s", cfgname);

    // Get the common properties from config
    field->delay                    = cfg->GetInt(arena->cfg, buffer, "firedelay", 50);
    field->duration                 = cfg->GetInt(arena->cfg, buffer, "duration", 1000);
    field->property                 = cfg->GetInt(arena->cfg, buffer, "property", 1);
    field->radius                   = cfg->GetInt(arena->cfg, buffer, "radius", 64);

    field->LVZSize                  = cfg->GetInt(arena->cfg, buffer, "lvzsize", 32);
    field->maxLVZIds                = cfg->GetInt(arena->cfg, buffer, "maxlvzids", 20);

    field->LVZIdBase[UpperLeft]     = cfg->GetInt(arena->cfg, buffer, "lvzidbase-ul", 0);
    field->LVZIdBase[UpperRight]    = cfg->GetInt(arena->cfg, buffer, "lvzidbase-ur", 0);
    field->LVZIdBase[LowerRight]    = cfg->GetInt(arena->cfg, buffer, "lvzidbase-lr", 0);
    field->LVZIdBase[LowerLeft]     = cfg->GetInt(arena->cfg, buffer, "lvzidbase-ll", 0);

    for (int i = 0; i < 4; i++)
        field->nextLVZId[i] = field->LVZIdBase[i];

    // Get the event from config
    const char *str = cfg->GetStr(arena->cfg, buffer, "event");
    if (str) {
        astrncpy(field->event, str, 16);
    } else {
        lm->LogA(L_ERROR, MODULE_NAME, arena, "Loaded field with unknown event (%s)", cfgname);
        astrncpy(field->event, cfgname, 16);
    }

    // Get the field name from config
    str = cfg->GetStr(arena->cfg, buffer, "name");
    if (str) {
        astrncpy(field->name, str, HSFIELD_NAME_SIZE);
    } else {
        lm->LogA(L_ERROR, MODULE_NAME, arena, "Loaded field with unknown name (%s)", cfgname);
        astrncpy(field->name, "unknown", HSFIELD_NAME_SIZE);
    }

    // Get the class name from config
    str = cfg->GetStr(arena->cfg, buffer, "class");
    if (str)
        astrncpy(field->className, str, 32);
    else
        astrncpy(field->className, "attack", 32);

    // Get the field class from the hashtable
    HSFieldClass *fieldClass = HashGetOne(&g_fieldClasses, field->className);
    if (!fieldClass) {
        lm->LogA(L_ERROR, MODULE_NAME, arena, "Error loading %s field with class %s", cfgname, field->className);
        afree(field);
        return 0;
    }

    field->fieldClass = fieldClass;

    // Call the property loader for the class
    HashInit(&field->properties);

    if (field->fieldClass->loader)
        field->fieldClass->loader(arena, buffer, &field->properties);

    // Add the new field to arena field list
    pthread_mutex_lock(&pthread_mutex);
    LLAdd(&adata->fields, field);
    pthread_mutex_unlock(&pthread_mutex);

    lm->LogA(L_INFO, MODULE_NAME, arena, "Added field type %s", field->name);

    return 1;
}

local int LoadFields(Arena *arena) {
    const char *fieldsStr = cfg->GetStr(arena->cfg, "hs_field", "fields");
    const char *temp = 0;
    char buffer[512];

    if (fieldsStr) {
        while (strsplit(fieldsStr, " ,\n\t", buffer, 511, &temp))
            LoadField(arena, buffer);
    }

    return 1;
}

local int UpdateFieldInstance(void *param) {
    HSFieldInstance *inst = (HSFieldInstance *)param;
    if (!inst)
        return 0;
    if (current_ticks() > inst->endTime) {
        // Remove field from game
        EndFieldInstance(inst->arena, inst);
        return 0;
    }

    // Update instance timer in field class
    if (inst->type && inst->type->fieldClass && inst->type->fieldClass->timer)
        inst->type->fieldClass->timer(inst);

    return 1;
}

local void BeginFieldInstance(Arena *arena, Player *p, HSField *type) {
    HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);
    HSFieldInstance *newInst = amalloc(sizeof(HSFieldInstance));
    char nameBuffer[24];
    Target t; 

    t.type = T_ARENA; 
    t.u.arena = arena;

    snprintf(nameBuffer, sizeof(nameBuffer)-1, "<%i-%.17s>", p->pid, type->name);
    nameBuffer[sizeof(nameBuffer) - 1] = 0;

    for (int i = 1; i < sizeof(nameBuffer); i++)
        nameBuffer[i] = tolower(nameBuffer[i]);

    newInst->fake = fake->CreateFakePlayer(nameBuffer, p->arena, SHIP_SHARK, p->p_freq);
    newInst->player = p;
    newInst->arena = arena;
    newInst->type = type;
    newInst->endTime = current_ticks() + type->duration;
    newInst->x = p->position.x;
    newInst->y = p->position.y;

    HashInit(&newInst->data);

    if (*type->event)
        items->triggerEvent(p, p->p_ship, type->event);

    pthread_mutex_lock(&pthread_mutex);

    for (int i = 0; i < 4; i++) {
        newInst->LVZIds[i] = type->nextLVZId[i];

        obj->Toggle(&t, newInst->LVZIds[i], 1);

        switch (i) {
            case UpperLeft:
                obj->Move(&t, newInst->LVZIds[i], newInst->x - type->radius, newInst->y - type->radius, 0, 0);
            break;
            case UpperRight:
                obj->Move(&t, newInst->LVZIds[i], newInst->x + type->radius - type->LVZSize, newInst->y - type->radius, 0, 0);
            break;
            case LowerRight:
                obj->Move(&t, newInst->LVZIds[i], newInst->x + type->radius - type->LVZSize, newInst->y + type->radius - type->LVZSize, 0, 0);
            break;
            case LowerLeft:
                obj->Move(&t, newInst->LVZIds[i], newInst->x - type->radius, newInst->y + type->radius - type->LVZSize, 0, 0);
            break;
        }
    }

    HSFieldIterate(&adata->fields, UpdateNextLVZId, type->LVZIdBase);
    LLAdd(&adata->instances, newInst);

    // Call instance constructor for field class
    if (type->fieldClass && type->fieldClass->construct)
        type->fieldClass->construct(newInst);

    pthread_mutex_unlock(&pthread_mutex);

    ml->SetTimer(UpdateFieldInstance, type->delay, type->delay, newInst, newInst);
}

local void EndFieldInstance(Arena *arena, HSFieldInstance *inst) {
    HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);
    short ids[4] = { inst->LVZIds[0], inst->LVZIds[1], inst->LVZIds[2], inst->LVZIds[3] };
    char ons[4] = { 0, 0, 0, 0 };
    Target t;
    
    t.type = T_ARENA; 
    t.u.arena = arena;

    // Stop updating the field instance
    ml->ClearTimer(UpdateFieldInstance, inst);
    // Turn off the field lvz
    obj->ToggleSet(&t, ids, ons, 4);

    lm->LogA(L_DRIVEL, MODULE_NAME, arena, "Destroyed field instance %s", inst->fake->name);
    fake->EndFaked(inst->fake);

    // Remove field instance from arena instance list
    pthread_mutex_lock(&pthread_mutex);
    LLRemove(&adata->instances, inst);
    pthread_mutex_unlock(&pthread_mutex);

    // Call destructor in field class
    if (inst->type && inst->type->fieldClass && inst->type->fieldClass->destruct)
        inst->type->fieldClass->destruct(inst);

    HashDeinit(&inst->data);
    afree(inst);
}

local int HandleRespawn(void *_p) {
    Player *p = (Player *)_p;
    HSFieldArenaData *adata = P_ARENA_DATA(p->arena, adkey);
    HSFieldPlayerData *pdata = PPDATA(p, pdkey);

    if (!p->arena)
        return 0;

    HSFieldInstanceIterate(&adata->instances, RemoveAllInstancesFromPlayer, p);
    pdata->dead = 0;

    return 0;
}

/*******************************/

local void OnShipFreqChange(Player *p, int newShip, int oldShip, int newFreq, int oldFreq) {
    HSFieldArenaData *adata = P_ARENA_DATA(p->arena, adkey);

    HSFieldInstanceIterate(&adata->instances, RemoveAllInstancesFromPlayer, p);
}

local void OnPlayerAction(Player *p, int action, Arena *arena) {
    if ((p->type != T_VIE) && (p->type != T_CONT))
        return;

    if (arena) {
        HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);
        HSFieldPlayerData *pdata = PPDATA(p, pdkey);

        if (action == PA_ENTERARENA) {
            pdata->dead = 0;
            pdata->lastField = 0;
        } else if (action == PA_LEAVEARENA) {
            ml->ClearTimer(HandleRespawn, p);
            HSFieldInstanceIterate(&adata->instances, RemoveAllInstancesFromPlayer, p);
        }
    }
}

local void OnPlayerKill(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green) {
    int enterDelay = cfg->GetInt(killed->arena->cfg, "Kill", "EnterDelay", 0);

    if (enterDelay > 0) {
        HSFieldPlayerData *pdata = PPDATA(killed, pdkey);
        pdata->dead = 1;
        ml->SetTimer(HandleRespawn, enterDelay + 100, 0, killed, killed);
    }
}

/*******************************/

local int RegisterFieldClass(const char *className, HSFieldClass *fieldClass) {
    lm->Log(L_INFO, "Registered field class %s", className);

    HashAdd(&g_fieldClasses, className, fieldClass);

    return 1;
}

local void UnregisterFieldClass(const char *className) {
    LinkedList *classes = HashGet(&g_fieldClasses, className);
    if (!classes || LLCount(classes) == 0)
        return;

    // Remove all of the classes with the name className
    HSFieldClass *fClass;
    Link *link;
    FOR_EACH(classes, fClass, link)
        HashRemove(&g_fieldClasses, className, fClass);
    LLFree(classes);
}

local Ihsfields fields_interface = {
    INTERFACE_HEAD_INIT(I_HSFIELDS, "hsfields")

    RegisterFieldClass,
    UnregisterFieldClass
};

/********************************/

local helptext_t field_help =
"Targets: arena\n"
"Syntax:\n"
"  ?field [fieldtype]\n"
"Spawns an attack field around your ship of the specified name.\n"
"If you specify no name, ?field will pick a field you own.\n";
local void Cfield(const char *cmd, const char *params, Player *p, const Target *target) {
    HSFieldArenaData *adata = P_ARENA_DATA(p->arena, adkey);
    HSFieldPlayerData *pdata = PPDATA(p, pdkey);
    HSField *type = NULL;

    if (HS_IS_SPEC(p))
        return;

    if (!items->getPropertySum(p, p->p_ship, "fieldlauncher", 0)) {
        chat->SendMessage(p, "You need a Field Launcher to use fields!");
        return;
    }

    if (pdata->dead) {
        chat->SendMessage(p, "You cannot launch a field while you're dead!");
        return;
    }

    if (pdata->lastField != 0 && TICK_DIFF(current_ticks(), pdata->lastField) <
        items->getPropertySum(p, p->p_ship, "fielddelay", 0)) {
        chat->SendMessage(p, "Your Field Launcher is currently recharging!");
        return;
    }

    HSFieldInstance *prev = HSFieldInstanceIterate(&adata->instances, GetOneInstanceFromPlayer, p);
    if (prev) {
        chat->SendMessage(p, "You may only launch one field at a time!");
        return;
    }

    if (*params)
        type = HSFieldIterate(&adata->fields, GetFieldByName, params);

    if (type) {
        if (items->getPropertySum(p, p->p_ship, "field", 0) & type->property) {
            pdata->lastField = current_ticks();
            BeginFieldInstance(p->arena, p, type);
            chat->SendMessage(p, "%s Field Created.", type->name);
        } else {
            chat->SendMessage(p, "You do not have that type of field available.");
            return;
        }
    } else if (!*params) {
        int sum = items->getPropertySum(p, p->p_ship, "field", 0);
        if (sum) {
            for (int i = 1; i <= sum; i *= 2) {
                if (sum & i) {
                    int propertyVal = i;
                    type = HSFieldIterate(&adata->fields, GetFieldByPropertyValue, &propertyVal);
                    if (type) {
                        pdata->lastField = current_ticks();
                        BeginFieldInstance(p->arena, p, type);
                        chat->SendMessage(p, "%s field created.", type->name);
                        return;
                    }
                }
            }
            lm->LogP(L_ERROR, MODULE_NAME, p, "Somehow failed to get field even though the property value is non-zero!");
        } else {
            chat->SendMessage(p, "You don't have any fields!");
            return;
        }
    } else {
        chat->SendMessage(p, "That type of field doesn't exist.");
        return;
    }
}

/*******************************/

local int GetInterfaces(Imodman *mm_) {
    if (mm_ && !mm) {
        mm = mm_;

        chat = mm->GetInterface(I_CHAT, ALLARENAS);
        lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
        cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
        cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
        pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
        aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
        ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
        obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
        fake = mm->GetInterface(I_FAKE, ALLARENAS);
        items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);

        return mm && chat && lm && cmd && pd && aman && ml && obj && fake && items;
    }

    return 0;
}

local void ReleaseInterfaces() {
    if (mm) {
        mm->ReleaseInterface(chat);
        mm->ReleaseInterface(lm);
        mm->ReleaseInterface(cfg);
        mm->ReleaseInterface(cmd);
        mm->ReleaseInterface(pd);
        mm->ReleaseInterface(aman);
        mm->ReleaseInterface(ml);
        mm->ReleaseInterface(obj);
        mm->ReleaseInterface(fake);
        mm->ReleaseInterface(items);

        mm = NULL;
    }
}

EXPORT const char info_hs_fields[] = "v2.0 by monkey, based on hs_field v1.01 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_fields(int action, Imodman *mm_, Arena *arena) {
    int rv = MM_FAIL;

    switch (action) {
        case MM_LOAD:
            if (!GetInterfaces(mm_)) {
                lm->Log(L_ERROR, "<%s> Unable to get required interfaces.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }

            adkey = aman->AllocateArenaData(sizeof(HSFieldArenaData));
            if (adkey == -1) {
                lm->Log(L_ERROR, "<%s> Unable to allocate arena data.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }

            pdkey = pd->AllocatePlayerData(sizeof(HSFieldPlayerData));
            if (pdkey == -1) {
                aman->FreeArenaData(adkey);
                lm->Log(L_ERROR, "<%s> Unable to allocate player data.", MODULE_NAME);
                ReleaseInterfaces();
                break;
            }

            pthread_mutexattr_init(&pthread_attr);
            pthread_mutexattr_settype(&pthread_attr, PTHREAD_MUTEX_RECURSIVE);
            
            if (pthread_mutex_init(&pthread_mutex, &pthread_attr) != 0) {
                pd->FreePlayerData(pdkey);
                aman->FreeArenaData(adkey);
                lm->Log(L_ERROR, "<%s> Unable to create pthread_mutex.", MODULE_NAME);
                ReleaseInterfaces();
            }

            HashInit(&g_fieldClasses);

            mm->RegInterface(&fields_interface, ALLARENAS);

            rv = MM_OK;

        break;
        case MM_ATTACH:
        {
            HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);

            LLInit(&adata->fields);
            LLInit(&adata->instances);

            for (int i = 0; i < 8; i++) {
                adata->cfgShipRadius[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "radius", 14);
                if (!adata->cfgShipRadius[i])
                    adata->cfgShipRadius[i] = 14;
            }

            LoadFields(arena);

            mm->RegCallback(CB_SHIPFREQCHANGE, OnShipFreqChange, arena);
            mm->RegCallback(CB_PLAYERACTION, OnPlayerAction, arena);
            mm->RegCallback(CB_KILL, OnPlayerKill, arena);

            cmd->AddCommand("field", Cfield, arena, field_help);

            rv = MM_OK;
        }
        break;
        case MM_DETACH:
        {
            HSFieldArenaData *adata = P_ARENA_DATA(arena, adkey);

            mm->UnregCallback(CB_SHIPFREQCHANGE, OnShipFreqChange, arena);
            mm->UnregCallback(CB_PLAYERACTION, OnPlayerAction, arena);
            mm->UnregCallback(CB_KILL, OnPlayerKill, arena);

            cmd->RemoveCommand("field", Cfield, arena);

            HSFieldInstanceIterate(&adata->instances, RemoveAllInstancesFromPlayer, 0);
            HSFieldIterate(&adata->fields, UnloadFields, 0);

            LLEmpty(&adata->instances);
            LLEmpty(&adata->fields);

            rv = MM_OK;
        }
        break;
        case MM_UNLOAD:
            if (mm->UnregInterface(&fields_interface, ALLARENAS)) {
                lm->Log(L_ERROR, "<%s> Unable to unregister HS Fields interface.", MODULE_NAME);
                break;
            }

            HashDeinit(&g_fieldClasses);

            aman->FreeArenaData(adkey);
            pd->FreePlayerData(pdkey);

            pthread_mutexattr_destroy(&pthread_attr);
            pthread_mutex_destroy(&pthread_mutex);

            ReleaseInterfaces();
            rv = MM_OK;

        break;
    }

    return rv;
}