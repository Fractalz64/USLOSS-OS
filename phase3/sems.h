typedef struct semaphore semaphore;
struct semaphore {
 	int 		id;
 	int 		value;
 	int 		startingValue;
 	procPtr     blockedProcPtr;
 	int 		priv_mBoxID;
 	int 		mutex_mBoxID;
 };