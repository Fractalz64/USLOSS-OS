/* Queue struct for processes */
typedef struct procStruct procStruct;
typedef struct procStruct * procPtr;

// #define BLOCKED 0
// #define CHILDREN 1
// #define SLEEP 2

// struct procQueue {
// 	procPtr head;
// 	procPtr tail;
// 	int 	 size;
// 	int 	 type; /* which procPtr to use for next */
// };

/* Heap */
typedef struct heap heap;
struct heap {
  int size;
  procPtr procs[MAXPROC];
};

/* 
* Process struct for phase 4
*/
struct procStruct {
  int         pid;
  int 		  mboxID; 
  int         blockSem;
  int		  wakeTime;
  //procPtr   nextSleepPtr;
};
