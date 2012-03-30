/* main.c
 * 
 * stash
 * stone age shell, basic implementation of a terminal shell
 *
 * Created by Thomas J. Sell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define PROMPT "[StASh]: "

//structure for a job
typedef struct {
	pid_t pid;
	char* status;
	char* name;
} job;

//functions
void add_job(job list[], int ppid, char* pname, char* pstatus);
void update_status(job list[], int ppid, char* pstatus);
void print_jobs(job list[]);
bool find_job(job list[], int ppid);
void kill_jobs(job list[]);
void remove_job(job list[], int pid);
int tokenize(char *string, char *args[]);
void mask_sigs(bool mask);

//global variables
int job_count = 0;

//main!
int main (void)
{
    char input[1024] = "null";
    
    // strings for built in commands
    char * quit = "quit", * exit_c = "exit"; // _c avoids conflict with exit 
    char * jobs = "jobs", * bg = "bg", * fg = "fg";
    job job_list[50];
    pid_t child_pid;
    int status, str_count;
    bool must_wait = false;
    
    //print initial promt and await input
    printf("\nWelcome to the Stone Age Shell\n");
    
    while(1)
    {
        //print prompt and accept input
        printf("%s", PROMPT);
        if(fgets(input, 1024, stdin) == NULL) break; //exits on CTRL + d
		input[strlen(input) - 1] = '\0';
        
        //if quit or exit, break
        if(strcmp(input, quit) == 0 || strcmp(input, exit_c) == 0)
        {
            kill_jobs(job_list);
            break;
        }
        
        char *strs[25];
        bool background = false;
        
        //check if command will be background
        if(input[strlen(input)-1] == '&') background = true; 
        
        str_count = tokenize(input, strs);
        
        // jobs command
        if(strcmp(input, jobs) == 0)
        {
            print_jobs(job_list);
        }
        
        // bg command
        else if(strcmp(strs[0], bg) == 0)
        {
            if (str_count<2 || atoi(strs[1])<1 ) {
				printf("Usage: bg pid\n");
			}
			else {
				int job = atoi(strs[1]);
				if (find_job(job_list, job)) {
					kill(job, SIGCONT);
					update_status(job_list,job, "Running");
				}
				else {
					printf("job: %d does not exist\n",job);
				}
			}
        }
        
        //fg command
        else if (strcmp(strs[0], fg) == 0)
        {
			if (str_count < 2 || atoi(strs[1]) < 1) 
            {
				printf("Usage: fg pid\n");
			}
			else
            {
				int job = atoi(strs[1]);
				if(tcsetpgrp(STDOUT_FILENO, job))
					printf("job: %d does not exist\n",job);
				
				kill(job, SIGCONT);
				update_status(job_list, job, "Running");
				must_wait = true;
				bool wait = true, child_cont = false;
                
				while(wait)
				{
					child_cont = false;
					pid_t changed = waitpid(WAIT_ANY, &status, WUNTRACED | WCONTINUED);
                    
					// if the child stopped, update the table
					if(WIFSTOPPED(status))
					{
						if(changed == job)
						{
							update_status(job_list, changed, "Stopped");
						}
					}
					// if the child received the continue signal, don't break
					else if(WIFCONTINUED(status) && changed == job)
					{
						child_cont = true;
						
					}
					// if the child exited, update table
					else if(WIFEXITED(status))
					{
						remove_job(job_list, changed);
					}
					// if child was signaled, update table
					else if(WIFSIGNALED(status))
					{
						remove_job(job_list, changed);
					}
					// if the child was signaled and not continued, break
					if(changed == job && !child_cont)
					{
						wait = false;
					}
				}
				mask_sigs(true);
				tcsetpgrp(STDOUT_FILENO, getpid());
				mask_sigs(false);
			}
		}
        
        else
        {
            child_pid = fork();
            
            // something fails
            if(child_pid < 0)
            {
                perror("Unable to fork");
                exit(errno);
            }
            
            //child
            else if(child_pid == 0)
            {
                //put yourself in new child proces group
                setpgid(getpid(),getpid());
                //exec command
                execvp(strs[0], &strs[0]);
                printf("%s failed\n", strs[0]);//only happens on error
                exit(EXIT_FAILURE);
            }
            
            // parent
            else
            {
                //put child in new process group
                setpgid(child_pid, child_pid);
                
                if(background)
                {
                    add_job(job_list, child_pid, strdup(strs[0]), "Running");
					must_wait=false;
                }
                else
                {
                    tcsetpgrp(STDOUT_FILENO, child_pid);
                    add_job(job_list, child_pid, strdup(strs[0]), "Running");
                    must_wait = true;
                }
            }
            
            while(must_wait)
            {
                bool cont = false;
				pid_t changed = waitpid(WAIT_ANY, &status, WUNTRACED | WCONTINUED);
				
				// if the child stopped, update the table
				if(WIFSTOPPED(status))
				{
					update_status(job_list, child_pid, "Stopped");
				}
				else if(WIFCONTINUED(status))
				{
					cont = true;
				}
				// if the child exited, update the table
				else if(WIFEXITED(status))
				{
					remove_job(job_list, changed);
				}
				// if the child was signaled, update the table
				else if(WIFSIGNALED(status))
				{
					remove_job(job_list, changed);
				}
				// if the fg child is done, break
				if(changed == child_pid && !cont)
				{
					must_wait = false;
				}
            }
            
            //regain the controlling terminal
            mask_sigs(true);
			tcsetpgrp(STDOUT_FILENO, getpid());
            mask_sigs(false);
        }
    }
    
    printf("Goodbye!\n");
    return 1;
}

// create adds job to given list
void add_job(job list[], int ppid, char * pname, char * pstatus)
{
	list[job_count].pid = ppid;
	list[job_count].name = pname;
	list[job_count].status = pstatus;
	job_count++;
}

// updates a jobs status
void update_status(job list[], int ppid, char * pstatus)
{
	int marker, i;
	//find job
	for (i = 0; i < job_count; i++) 
    {
		if (list[i].pid==ppid) 
        {
			marker = i;
		}
	}
	list[marker].status = pstatus;
}

// prints out list of jobs
void print_jobs(job list[])
{
	if(job_count < 1)
	{
		printf("The jobs are a lie! (there are none)\n");
	}
	int i;
	for(i = 0; i < job_count; i++)
	{
		printf("%d\t%s\t%s\n", list[i].pid, list[i].name, list[i].status);
	}
}

//finds job with given pid
bool find_job(job list[], int ppid)
{
	int i;
	for (i = 0; i < job_count; i++) 
    {
		if (list[i].pid == ppid)
        {
			return true;
		}
	}
	return false;
}

// kills all jobs in list
void kill_jobs(job list[])
{
	int i, len = job_count;
	for(i = 0; i < len; i++)
	{
		kill(list[0].pid, SIGKILL);
		remove_job(list, list[0].pid);
	}
}

// kills desired job in list
void remove_job(job list[], int pid)
{
	int marker, i;
	//find job
	for (i = 0; i < job_count; i++) 
    {
		if (list[i].pid == pid) 
        {
			marker = i;
		}
	}
	for (i = marker; i < job_count; i++) 
    {
		list[i] = list[i+1];
	}
	job_count--;
}

// breaks up input, returns number of tokens
int tokenize(char *string, char *args[])
{
    char *tok;
    tok = strtok(string, " &");
    int count = 0;
    while (tok && count < 24)
    {
        args[count] = tok; 
        count++;
        tok = strtok(NULL, " &");
    }
    args[count] = NULL;
	return count;
}

//toggles signal masking
void mask_sigs(bool mask)
{
    sigset_t x;
    sigemptyset (&x);
    sigaddset(&x, SIGTTOU);
    if(mask)
    {
        sigprocmask(SIG_BLOCK, &x, NULL);
    }
    else
    {
        sigprocmask(SIG_UNBLOCK, &x, NULL);
    }
}