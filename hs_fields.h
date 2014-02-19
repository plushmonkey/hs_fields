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
typedef void(*HSFieldInstanceConstruct)(struct HSFieldInstance *inst);
typedef void(*HSFieldInstanceTimer)(struct HSFieldInstance *inst);
typedef void(*HSFieldInstanceDestruct)(struct HSFieldInstance *inst);

typedef struct HSFieldClass {
    HSFieldLoader loader;               // Called when field type is loaded
    HSFieldCleanup cleanup;             // Called when field type is unloaded
    HSFieldInstanceConstruct construct; // Called when field instance is created
    HSFieldInstanceTimer timer;         // Called when field instance is updated
    HSFieldInstanceDestruct destruct;   // Called when field instance is destroyed
} HSFieldClass;

typedef struct HSField {
    char name[32];                  // Name of the field type
    char className[32];             // Field class has to be a registered class
    HSFieldClass *fieldClass;       // Class functions
    short delay;                    // How often the field gets updated
    char event[16];                 // Event to call when the field is used
    int duration;                   // How long the field stays in game
    int property;                   // The required property on ship to use
    short radius;                   // The radius of the field
    short updateDelay;              // How often the field gets updated / fires
    int LVZIdBase[CornerCount];     // The lower of the LVZ id range
    short nextLVZId[CornerCount];   // The next LVZ id to use
    i8 maxLVZIds;                   // Number of LVZ ids to cycle through
    i8 LVZSize;                     // Size of the LVZ graphic
    HashTable properties;           // Map of properties for the specific field class
} HSField;

typedef struct HSFieldInstance {
    Arena *arena;
    Player *player;
    Player *fake;
    HSField *type;
    ticks_t endTime;
    short LVZIds[CornerCount];
    short x, y;
    HashTable data;
} HSFieldInstance;

int InSquare(Arena *arena, int ship, int sx, int sy, int r, int x, int y);

#define HS_IS_SPEC(p) ((p->p_ship == SHIP_SPEC))
#define HS_IS_ON_FREQ(p,a,f) ((p->arena == a) && (p->p_freq == f))

#define I_HSFIELDS "hs_fields-1"
typedef struct Ihsfields {
    INTERFACE_HEAD_DECL

    /*
        Construct is called when a field instance is created.
        Timer is called when the field is updated.
        Destruct is called when a field instance is being removed.
    */
    int(*RegisterFieldClass)(const char *className, HSFieldClass *fieldClass);

    void(*UnregisterFieldClass)(const char *className);
} Ihsfields;

#endif
