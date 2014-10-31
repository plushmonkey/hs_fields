#ifndef HS_FIELDS_H_
#define HS_FIELDS_H_

enum Corners {
    UpperLeft = 0,
    UpperRight,
    LowerRight,
    LowerLeft,

    CornerCount
};

struct HSField;
struct HSFieldInstance;

typedef void(*HSFieldLoader)(Arena *arena, const char *section, HashTable *properties);
typedef void(*HSFieldCleanup)(Arena *arena, HashTable *properties);
typedef void(*HSFieldInstanceConstructor)(struct HSFieldInstance *inst);
typedef void(*HSFieldInstanceUpdate)(struct HSFieldInstance *inst);
typedef void(*HSFieldInstanceDestructor)(struct HSFieldInstance *inst);

/**
 * Structure of functions for each field class.
 */
typedef struct HSFieldClass {
    /**
     * A loader function that is called to load anything specific to the field class.
     */
    HSFieldLoader loader;
    
    /**
     * Used when the field class is unloaded to cleanup anything.
     */
    HSFieldCleanup cleanup;
    
    /**
     * Called when the field instance is created.
     */
    HSFieldInstanceConstructor constructor;
    
    /**
     * Called on a fixed timer to update the field instance.
     */
    HSFieldInstanceUpdate update;
    
    /**
     * Called when the field instance is destroyed.
     */
    HSFieldInstanceDestructor destructor;
} HSFieldClass;

/**
 * Structure for each field type. Each type will have a field class.
 */
typedef struct HSField {
    /** 
     * The arena where this field type was created.
     */
    Arena *arena;
    /**
     * The name of the field type.
     */
    char name[32];
    
    /**
     * The field classname of this field type.
     */
    char className[32];
    
    /**
     * The field class of this field type.
     */
    HSFieldClass *fieldClass;
    
    /**
     * The amount of ticks between each update.
     */
    short delay;
    
    /**
     * An event to fire when a field instance is created.
     */
    char event[16];
    
    /**
     * How many ticks the field stays in game.
     */
    int duration;
    
    /**
     * The required "field" property on ship to use. Uses bitmask.
     */
    int property;
    
    /**
     * The radius of the field.
     */
    short radius;
    
    /**
     * The base object ID for each corner of the field.
     */
    int LVZIdBase[CornerCount];
    
    /**
     * The next object ID to use for each corner of the field
     */
    short nextLVZId[CornerCount];
    
    /**
     * The number of object IDs to cycle through.
     */
    i8 maxLVZIds;
    
    /**
     * The size of the LVZ graphic in pixels.
     */
    i8 LVZSize;
    
    /**
     * Any properties for the specific field class.
     */
    HashTable properties;
} HSField;

/**
 * Structure for individual field instances.
 */
typedef struct HSFieldInstance {
    /**
     * The arena where the field instance was created.
     */
    Arena *arena;
    /**
     * The player that created the field instance.
     */
    Player *player;
    /**
     * The fake player acting as the field instance.
     */
    Player *fake;
    
    /**
     * The field type of the field instance.
     */
    HSField *type;
    
    /**
     * When the field instance will be destroyed.
     */
    ticks_t endTime;
    
    /**
     * The object ID for each corner of the field instance.
     */
    short LVZIds[CornerCount];
    
    /**
     * The x position of the field instance.
     */
    short x;
    
    /**
     * The y position of the field instance.
     */
    short y;
    
    /**
     * Data stored per field instance.
     * Needs to be manually allocated in the constructor.
     */
    HashTable *data;
} HSFieldInstance;

int InSquare(Arena *arena, int ship, int sx, int sy, int r, int x, int y);

#define HS_IS_SPEC(p) ((p->p_ship == SHIP_SPEC))
#define HS_IS_ON_FREQ(p,a,f) ((p->arena == a) && (p->p_freq == f))

#define I_HSFIELDS "hs_fields-1"
typedef struct Ihsfields {
    INTERFACE_HEAD_DECL

    /**
     * Registers a field class globally.
     * @param className     The name of the field class to register.
     * @param fieldClass    The field class structure that is being registered.
     * @return              Returns 1 if successful.
     */
    int(*RegisterFieldClass)(const char *className, HSFieldClass *fieldClass);
    
    /**
     * Unregisters a field class.
     * @param className     The name of the field class to be unregistered.
     */
    void(*UnregisterFieldClass)(const char *className);
} Ihsfields;

#endif
