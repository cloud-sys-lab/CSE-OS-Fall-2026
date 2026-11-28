#define _POSIX_C_SOURCE 200809L
#include "control.h"
#include "search.h"
#include "system_info.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int passed, failed;
#define CHECK(label, condition) do { \
    if (condition) { ++passed; printf("PASS %s\n", label); } \
    else { ++failed; fprintf(stderr, "FAIL %s (line %d)\n", label, __LINE__); } \
} while (0)

static void wait_ms(int ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) { }
}

static pid_t sleeping_child(void) {
    int ready[2];
    if (pipe(ready)) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(ready[0]);
        prctl(PR_SET_NAME, "mt-test) task", 0, 0, 0);
        size_t size = 8 * 1024 * 1024;
        volatile unsigned char *memory = malloc(size);
        if (!memory) _exit(3);
        for (size_t i = 0; i < size; i += 4096) memory[i] = 7;
        if (write(ready[1], "r", 1) != 1) _exit(4);
        close(ready[1]);
        for (;;) pause();
    }
    close(ready[1]);
    char byte;
    ssize_t received = read(ready[0], &byte, 1);
    close(ready[0]);
    if (pid < 0 || received != 1) return -1;
    return pid;
}

int main(void) {
    Process source[] = {
        {.pid=30, .name="zeta", .cpu_percent=10, .mem_kb=20},
        {.pid=10, .name="Alpha", .cpu_percent=80, .mem_kb=50},
        {.pid=20, .name="alpha", .cpu_percent=80, .mem_kb=10},
        {.pid=120, .name="worker", .cpu_percent=0, .mem_kb=50}
    }, work[4], filtered[4];
    memcpy(work, source, sizeof work);
    sort_processes(work, 4, SORT_CPU);
    CHECK("CPU descending with PID tiebreak", work[0].pid==10 && work[1].pid==20 && work[3].pid==120);
    sort_processes(work, 4, SORT_MEM);
    CHECK("RSS descending with PID tiebreak", work[0].pid==10 && work[1].pid==120 && work[3].pid==20);
    sort_processes(work, 4, SORT_PID);
    CHECK("PID ascending", work[0].pid==10 && work[1].pid==20 && work[3].pid==120);
    sort_processes(work, 4, SORT_NAME);
    CHECK("Name case-insensitive and deterministic", work[0].pid==10 && work[1].pid==20 && work[2].pid==120);
    Process singleton = source[0];
    sort_processes(&singleton, 1, SORT_CPU);
    sort_processes(NULL, 0, SORT_PID);
    CHECK("Empty and one-row sorts", singleton.pid==30);
    CHECK("Substring search ignores case", search_processes(source, 4, "LPH", filtered)==2);
    CHECK("Numeric query matches exact PID", search_processes(source, 4, "20", filtered)==1 && filtered[0].pid==20);
    CHECK("Trimmed numeric query", search_processes(source, 4, "  120  ", filtered)==1 && filtered[0].pid==120);
    CHECK("Empty query returns all", search_processes(source, 4, "", filtered)==4);
    CHECK("Whitespace query returns all", search_processes(source, 4, "   ", filtered)==4);
    CHECK("Missing name returns no match", search_processes(source, 4, "does-not-exist", filtered)==0);
    CHECK("Huge numeric query is rejected", search_processes(source, 4, "999999999999999999999999", filtered)==0);
    memcpy(work, source, sizeof work);
    CHECK("In-place filtering", search_processes(work, 4, "alpha", work)==2 && work[0].pid==10 && work[1].pid==20);
    CHECK("Invalid search arguments", search_processes(NULL, 1, "x", filtered)==-1);
    CHECK("State labels", !strcmp(process_state_name('Z'), "Zombie") && !strcmp(process_state_name('?'), "Unknown"));

    ControlState control;
    control_init(&control);
    control_command(&control, "sort mem");
    CHECK("Sort command", control.sort==SORT_MEM);
    control_command(&control, "search two words");
    CHECK("Search retains spaces", !strcmp(control.query, "two words"));
    control_command(&control, "clear");
    CHECK("Clear command", control.query[0]=='\0');
    control_command(&control, "interval 0.5");
    CHECK("Fractional refresh interval", control.interval_ms==500);
    control_command(&control, "interval nan");
    CHECK("NaN interval rejected", control.interval_ms==500);
    control_command(&control, "interval 0");
    CHECK("Zero interval rejected", control.interval_ms==500);
    control_command(&control, "pause");
    CHECK("Pause command", control.paused==1);
    control_command(&control, "refresh");
    CHECK("Manual refresh while paused", control.needs_refresh==1 && control.paused==1);
    CHECK("PID 1 protected", terminate_process(1,0)==-EINVAL);
    CHECK("Self protected", terminate_process(getpid(),1)==-EINVAL);
    CHECK("Zero and negative PIDs protected", terminate_process(0,0)==-EINVAL && terminate_process(-1,1)==-EINVAL);
    CHECK("Permission error explained", strstr(signal_error_message(-EPERM), "Permission denied")!=NULL);
    CHECK("Missing process error explained", strstr(signal_error_message(-ESRCH), "not found")!=NULL);
    control_command(&control, "kill 1");
    CHECK("Protected target creates no confirmation", !control.pending);

    SystemInfo system;
    CHECK("System stats readable", get_system_info(&system)==0);
    CHECK("Memory invariant", system.mem_total_kb>0 && system.mem_used_kb+system.mem_available_kb==system.mem_total_kb);
    CHECK("System CPU bounds", isfinite(system.cpu_percent) && system.cpu_percent>=0 && system.cpu_percent<=100);
    CHECK("CPU and process counts", system.num_cpus>=1 && system.num_processes>=1);
    Process *list = calloc(8192, sizeof *list);
    CHECK("Allocate process list", list!=NULL);
    if (!list) return 1;
    cleanup_process_cache();
    int count = get_process_list(list,8192), found_self=0, all_zero=1;
    for (int i=0;i<count;++i) {
        if (list[i].pid==getpid()) found_self=1;
        if (list[i].cpu_percent!=0.0) all_zero=0;
    }
    CHECK("Live list includes this test", count>0 && found_self);
    CHECK("First scan has baseline CPU values", all_zero);
    CHECK("Invalid list arguments rejected", get_process_list(NULL,1)==-1 && get_process_list(list,0)==-1);

    pid_t child = sleeping_child();
    CHECK("Create controlled child", child>1);
    if (child>1) {
        Process p;
        CHECK("Parse name with space and parenthesis", get_process_info(child,&p)==0 && !strcmp(p.name,"mt-test) task"));
        CHECK("Resident allocation visible", p.mem_kb>=4096 && p.mem_percent>=0.0);
        kill(child,SIGSTOP);
        int status;
        waitpid(child,&status,WUNTRACED);
        CHECK("Stopped state visible", get_process_info(child,&p)==0 && p.state=='T');
        kill(child,SIGCONT);
        kill_selected(&control,child,0);
        CHECK("SIGTERM requires confirmation", control.pending && kill(child,0)==0);
        control_command(&control,"yes 1");
        CHECK("Wrong PID cannot confirm", control.pending && kill(child,0)==0);
        control_command(&control,"no");
        CHECK("Cancellation preserves process", !control.pending && kill(child,0)==0);
        kill_selected(&control,child,0);
        control.target.start_time_ticks++;
        char command[64];
        snprintf(command,sizeof command,"yes %ld",(long)child);
        control_command(&control,command);
        CHECK("Changed identity refuses signal", !control.pending && strstr(control.message,"another process") && kill(child,0)==0);
        kill_selected(&control,child,0);
        control_command(&control,command);
        waitpid(child,&status,0);
        CHECK("Confirmed SIGTERM reaches owned child", WIFSIGNALED(status) && WTERMSIG(status)==SIGTERM);
        CHECK("Exited process returns ESRCH", terminate_process(child,0)==-ESRCH);
        CHECK("Exited process read fails", get_process_info(child,&p)==-1);
        CHECK("Exit disappears from list", (count=get_process_list(list,8192))>=0 && search_processes(list,count,command+4,list)==0);
    }
    child=sleeping_child();
    CHECK("Create forced-termination child",child>1);
    if(child>1) {
        int status;
        CHECK("SIGKILL call succeeds",terminate_process(child,1)==0);
        waitpid(child,&status,0);
        CHECK("SIGKILL reaches owned child",WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);
    }
    pid_t busy=fork();
    if(busy==0) { volatile unsigned long value=1; for(;;) value=value*1664525UL+1013904223UL; }
    CHECK("Create controlled CPU worker",busy>1);
    if(busy>1) {
        wait_ms(100);
        double cpu=get_process_cpu_usage(busy,600);
        printf("MEASURE CPU worker %.2f%% (one core = 100%%)\n",cpu);
        CHECK("Busy process accumulates measurable CPU",isfinite(cpu)&&cpu>0.0);
        terminate_process(busy,1);
        waitpid(busy,NULL,0);
    }
    free(list);
    cleanup_process_cache();
    printf("RESULT %d passed, %d failed\n",passed,failed);
    printf("NOTE Cross-user EPERM delivery requires a separate account; message mapping is tested here.\n");
    return failed?1:0;
}
