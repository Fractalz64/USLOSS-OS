/* Patrick's DEBUG printing constant... */
#define DEBUG 1

typedef struct procStruct procStruct;

typedef struct procStruct * procPtr;

/* Queue struct for the Ready Lists */
typedef struct procQueue procQueue;
#define READYLIST 0
#define CHILDREN 1
#define DEADCHILDREN 2
#define ZAP 3

struct procQueue {
	procPtr head;
	procPtr tail;
	int 	size;
	int 	type; /* which procPtr to use for next */
};

/* Process struct */
struct procStruct {
	procPtr         nextProcPtr;
	procPtr         nextSiblingPtr;
	char            name[MAXNAME];     /* process's name */
	char            startArg[MAXARG];  /* args passed to process */
	USLOSS_Context  state;             /* current context for process */
	short           pid;               /* process id */
	int             priority;
	int (* startFunc) (char *);   /* function where process begins -- launch */
	char           *stack;
	unsigned int    stackSize;
	int             status;        /* READY, BLOCKED, QUIT, etc. */
	/* other fields as needed... */
	procPtr         parentPtr;
	procQueue 		childrenQueue;  /* queue of the process's children */
	int 			quitStatus;		/* whatever the process returns when it quits */
	procQueue		deadChildrenQueue;	/* list of children who have quit in the order they have quit */
	procPtr 		nextDeadSibling;
	int				zapStatus; // 1 zapped; 0 not zapped
	procQueue		zapQueue;
	procPtr			nextZapPtr;
	int 			timeStarted; // the time the current time slice started
	int 			cpuTime; // the total amount of time the process has been running	
	int 			sliceTime; // how long the process has been running in the current time slice
};

#define TIMESLICE 80000

/* process statuses */
#define EMPTY 0
#define READY 1
#define RUNNING 2
#define QUIT 4
#define JBLOCKED 5
#define ZBLOCKED 6

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

/* Some useful constants.  Add more as needed... */
#define NO_CURRENT_PROCESS NULL
#define MINPRIORITY 5
#define MAXPRIORITY 1
#define SENTINELPID 1
#define SENTINELPRIORITY (MINPRIORITY + 1)