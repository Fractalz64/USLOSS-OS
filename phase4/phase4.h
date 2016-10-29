/*
 * These are the definitions for phase 4 of the project (support level, part 2).
 */

#ifndef _PHASE4_H
#define _PHASE4_H

/*
 * Maximum line length
 */

#define MAXLINE         80

/*
 * Function prototypes for this phase.
 */

extern  int  Sleep(int seconds);

extern  int  DiskRead (void *diskBuffer, int unit, int track, int first, 
                       int sectors, int *status);
extern  int  DiskWrite(void *diskBuffer, int unit, int track, int first,
                       int sectors, int *status);
extern  int  DiskSize (int unit, int *sector, int *track, int *disk);
extern  int  TermRead (char *buffer, int bufferSize, int unitID,
                       int *numCharsRead);
extern  int  TermWrite(char *buffer, int bufferSize, int unitID,
                       int *numCharsRead);

extern  int  start4(char *);

#define ERR_INVALID             -1
#define ERR_OK                  0

/* Queue struct for processes */
typedef struct procStruct procStruct;
typedef struct procStruct * procPtr;
typedef struct procQueue procQueue;

#define BLOCKED 0
#define CHILDREN 1
#define SLEEP 2

struct procQueue {
	procPtr head;
	procPtr tail;
	int 	 size;
	int 	 type; /* which procPtr to use for next */
};

/* 
* Process struct for phase 4
*/
struct procStruct {
    int             pid;
    int 		    mboxID; /* 0 slot mailbox belonging to this process */
	int (* startFunc) (char *);   /* function where process begins */
    procPtr     nextProcPtr;
    procPtr     nextSiblingPtr;
    procPtr     parentPtr;
	procQueue 	childrenQueue;

	int			wakeTime;
	procPtr		nextSleepPtr;
};

typedef struct semaphore semaphore;
struct semaphore {
 	int 		id;
 	int 		value;
 	int 		startingValue;
 	procQueue   blockedProcs;
 	int 		priv_mBoxID;
 	int 		mutex_mBoxID;
 };

#endif /* _PHASE4_H */
