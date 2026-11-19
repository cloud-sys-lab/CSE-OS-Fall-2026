#include <stdio.h>
#include <string.h>
#include "uthread.h"

static char order[16];
static int pos;
typedef struct {
    char name;
    int wait_amt;
} arguments;

static void worker(void* arg){
    arguments args = *(arguments*)arg;
    
    order[pos++] = args.name;
}

int main(void){
    
}