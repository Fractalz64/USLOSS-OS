#include <stdio.h>
#include <phase1.h>
#include <phase2.h>

extern int debugflag2;
extern void disableInterrupts(void);
extern void enableInterrupts(void);
extern void requireKernelMode(char *);

int IOmailboxes[7]; // mboxIDs for the IO devices
int IOblocked = 0; // number of processes blocked on IO mailboxes

/* Does a receive operation on the mailbox associated with the given unit of the device type. */
int waitDevice(int type, int unit, int *status) 
{
    disableInterrupts();
    requireKernelMode("waitDevice()");

    IOblocked++;
    MboxReceive(IOmailboxes[type+unit], status, sizeof(int));
    IOblocked--;

    enableInterrupts(); // re-enable interrupts
    // return -1 if we were zapped while waiting, return 0 otherwise
    return isZapped() ? -1 : 0; 
} /* nullsys */


/* an error method to handle invalid syscalls */
void nullsys(systemArgs *args)
{
    USLOSS_Console("nullsys(): Invalid syscall. Halting...\n");
    USLOSS_Halt(1);
} /* nullsys */


void clockHandler2(int dev, void *arg)
{
    disableInterrupts();
    requireKernelMode("clockHandler2()");
    if (DEBUG2 && debugflag2)
      USLOSS_Console("clockHandler2(): called\n");

    // make sure this is the clock device, return otherwise
    if (dev != USLOSS_CLOCK_DEV) {
      if (DEBUG2 && debugflag2)
        USLOSS_Console("clockHandler2(): called by other device, returning\n");
      return;
    }

    // send message every 5 interrupts
    static int count = 0;
    count++;
    if (count == 5) {
      int status;
      USLOSS_DeviceInput(dev, 0, &status); // get the status
      MboxCondSend(IOmailboxes[dev], &status, sizeof(int));
      count = 0;
    }

    timeSlice(); // call timeSlice()
    enableInterrupts(); // re-enable interrupts
} /* clockHandler */


void diskHandler(int dev, void *arg)
{
    disableInterrupts();
    requireKernelMode("diskHandler()");
    if (DEBUG2 && debugflag2)
      USLOSS_Console("diskHandler(): called\n");

    // make sure this is the disk device, return otherwise
    if (dev != USLOSS_DISK_DEV) {
      if (DEBUG2 && debugflag2)
        USLOSS_Console("diskHandler(): called by other device, returning\n");
      return;
    }

    // get the device status
    long unit = (long)arg;
    int status;
    int valid = USLOSS_DeviceInput(dev, unit, &status);

    // make sure the unit number was valid
    if (valid == USLOSS_DEV_INVALID) {
      if (DEBUG2 && debugflag2)
        USLOSS_Console("diskHandler(): unit number invalid, returning\n");
      return;
    }

    // conditionally send to the device's mailbox
    MboxCondSend(IOmailboxes[dev+unit], &status, sizeof(int));
    enableInterrupts(); // re-enable interrupts
} /* diskHandler */


void termHandler(int dev, void *arg)
{
    disableInterrupts();
    requireKernelMode("termHandler()");
    if (DEBUG2 && debugflag2)
      USLOSS_Console("termHandler(): called\n");

    // make sure this is the disk device, return otherwise
    if (dev != USLOSS_TERM_DEV) {
      if (DEBUG2 && debugflag2)
        USLOSS_Console("termHandler(): called by other device, returning\n");
      return;
    }

    // get the device status
    long unit = (long)arg;
    int status;
    int valid = USLOSS_DeviceInput(dev, unit, &status);

    // make sure the unit number was valid
    if (valid == USLOSS_DEV_INVALID) {
      if (DEBUG2 && debugflag2)
        USLOSS_Console("termHandler(): unit number invalid, returning\n");
      return;
    }

    // conditionally send to the device's mailbox
    MboxCondSend(IOmailboxes[dev+unit], &status, sizeof(int));
    enableInterrupts(); // re-enable interrupts
} /* termHandler */


void syscallHandler(int dev, void *arg)
{
    disableInterrupts();
    requireKernelMode("syscallHandler()");
   if (DEBUG2 && debugflag2)
      USLOSS_Console("syscallHandler(): called\n");

    // call nullsys for now
    nullsys((systemArgs*)arg);
} /* syscallHandler */

/* Returns 1 if there are processes blocked on IO, 0 otherwise */
int check_io() {
    return IOblocked > 0 ? 1 : 0;
}