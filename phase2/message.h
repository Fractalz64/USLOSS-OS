
#define DEBUG2 1

typedef struct mailSlot *slotPtr;
typedef struct mailbox   mailbox;
typedef struct mailSlot  mailSlot;
typedef struct mboxProc mboxProc;
typedef struct mboxProc *mboxProcPtr;
typedef struct queue queue;

struct mboxProc {
    mboxProcPtr     nextMboxProc;
    int             pid;     // process ID
    void            *msg_ptr; // where to put received message
    int             msg_size;
    slotPtr         messageReceived; // mail slot containing message we've received
};

#define SLOTQUEUE 0
#define PROCQUEUE 1

struct queue {
    void   *head;
    void   *tail;
    int     size;
    int     type; // tells what kind of pointers to use
};

struct mailbox {
    int       mboxID;
    int       status;
    int       totalSlots;
    int       slotSize; 
    queue     slots; // queue of mailSlots in this mailbox
    queue     blockedProcsSend; // processes blocked on a send
    queue     blockedProcsReceive; // processes blocked on a receive
};

struct mailSlot {
    int       mboxID;
    int       status;
    int       slotID;
    slotPtr   nextSlotPtr;
    char      message[MAX_MESSAGE];
    int       messageSize;
};

// define mailbox status constants
#define INACTIVE 0
#define ACTIVE 1

// mail slot status constants
#define EMPTY 0
#define USED 1

// define process status constants
#define FULL_BOX 11
#define NO_MESSAGES 12

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
