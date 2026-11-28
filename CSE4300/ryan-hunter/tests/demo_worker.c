#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

static volatile sig_atomic_t stop;
static void finish(int signum) { (void)signum; stop=1; }
int main(int argc,char **argv) {
    const char *mode=argc>1?argv[1]:"sleep";
    int busy=!strcmp(mode,"cpu");
    int memory=!strcmp(mode,"memory");
    int stubborn=!strcmp(mode,"stubborn");
    if(!busy&&!memory&&!stubborn&&strcmp(mode,"sleep")) {
        fprintf(stderr,"Usage: %s cpu|memory|sleep|stubborn\n",argv[0]); return 2;
    }
    const char *name=busy?"mt-demo-cpu":memory?"mt-demo-memory":stubborn?"mt-demo-stub":"mt-demo-sleep";
    prctl(PR_SET_NAME,name,0,0,0);
    struct sigaction action={0};
    sigemptyset(&action.sa_mask);
    action.sa_handler=finish;
    sigaction(SIGINT,&action,NULL);
    action.sa_handler=stubborn?SIG_IGN:finish;
    sigaction(SIGTERM,&action,NULL);
    volatile unsigned char *allocation=NULL;
    if(memory) {
        size_t bytes=32*1024*1024;
        allocation=malloc(bytes);
        if(!allocation) return 1;
        for(size_t i=0;i<bytes;i+=4096) allocation[i]=1;
    }
    printf("%s PID=%ld\n",name,(long)getpid()); fflush(stdout);
    volatile unsigned long value=1;
    while(!stop) {
        if(busy) value=value*1664525UL+1013904223UL;
        else pause();
    }
    free((void *)allocation);
    return 0;
}
