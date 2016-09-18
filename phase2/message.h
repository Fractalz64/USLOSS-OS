
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mailSlot   mailSlot;
typedef struct mboxProc *mboxProcPtr;
typedef struct slotQueue slotQueue;

// queue for mailSlots
struct slotQueue {
    slotPtr head;
    slotPtr tail;
    int     size;
};

struct mailbox {
    int       mboxID;
    // other items as needed...
    int       status;
    int       totalSlots;
    int       slotsTaken;
    int       slotSize;
    int       nextSlot;
    slotQueue slots;
};

struct mailSlot {
    int       mboxID;
    int       status;
    // other items as needed...
    int       slotID;
    slotPtr   nextSlotPtr;
    char      message[MAX_MESSAGE];
};

// define mailbox status constants
#define INACTIVE 0
#define ACTIVE 1

// mail slot status constants
#define EMPTY 0
#define FULL 1

// define process status constants
#define FULLBOX 11

struct psrBits {
    unsigned int curMode:1;
    unsigned int curIntEnable:1;
    unsigned int prevMode:1;
    unsigned int prevIntEnable:1;
    unsigned int unused:28;
};

union psrValues {
    struct psrBits bits;
    unsigned int integerPart;
};
