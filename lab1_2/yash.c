/*
 * yash.c
 *
 *  Created on: Sep 4, 2015
 *      Author: rcozart
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>


#define EXIT_SUCCESS 0
#define EXIT_FAILURE -1
#define YASH_TOK_DELIM " "
#define YASH_TOK_BUFFSIZE 64

int shell_pgid;
int shell_terminal;
int token_count;
int mypipe[2];
int job_num = 1;

//Taken from gnu.org
typedef struct process{
	struct process* next;
	char** argv;
	pid_t pid;
	char completed;             /* true if process has completed */
	char stopped;               /* true if process has stopped */
	int status;                 /* reported status value */
	int infile;
	int outfile;
	int errfile;
} process;

typedef struct job {
	struct job* next;
	process* first_process;
	char* command;
	pid_t pgid;
	char notified;
	int job_is_bg;
	int stopped;
	int need_to_print;
	struct termios tmodes;
	int stdin, stdout, stderr;
	int num;
} job;

void yash_init();
void yash_loop();
void yash_launch_process(process* p, pid_t pgid,
						 int foreground);
void yash_launch_job(job* j, int foreground);
char** yash_split_line(char* line, int* token_count);
void yash_configure_job(job* j, char* line, char** line_tokenized, int token_count, int* foreground);
void yash_put_job_in_fg(job* j, int cont);
void yash_put_job_in_bg(job* j, int cont);
void yash_sigtstp_handler(int signo);
void yash_sigint_handler(int signo);
void yash_sigchld_handler(int signo);
void wait_for_job(job* j);
int job_is_completed (job *j);
int job_is_stopped (job *j);
int mark_process_status (pid_t pid, int status);
job* find_recent_bg_stopped(job* first_job);
job * find_job (pid_t pgid);
int job_is_completed (job *j);
int job_is_stopped (job *j);
void continue_job (job *j, int foreground);
void mark_job_as_running (job *j);



void yash_init(){

	shell_terminal = STDIN_FILENO;
	//while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
	//        kill (- shell_pgid, SIGTTIN);

	signal (SIGINT, yash_sigint_handler);
	//signal (SIGQUIT, SIG_IGN);
	signal (SIGTSTP, yash_sigtstp_handler);
	//signal (SIGTTIN, SIG_DFL);
	signal (SIGTTOU, SIG_DFL);
	signal (SIGCHLD, yash_sigchld_handler);

	shell_pgid = getpid();
	return;
}
job* first_job = NULL;

void yash_sigint_handler(int signo){
	//get most recent job
	process* p;
	job* j;
	//fprintf(stdout, "Yash SIGTSTP handler triggered");
	if(first_job == NULL) return;
	for(j= first_job; j->next; j=j->next){
	}
	if(job_is_completed(j)) return;
	for(p = j->first_process; p; p = p->next){
		if(kill(p->pid, SIGINT) < 0)
			perror("yash: kill(SIGTSTP)");
		p->completed = 1;
	}

	fflush(stdout);
	fflush(stdin);
}

void yash_sigtstp_handler(int signo){
	//get most recent job
	process* p;
	job* j;
	//fprintf(stdout, "Yash SIGTSTP handler triggered");
	signal(SIGTSTP, yash_sigtstp_handler);
	for(j= first_job; j->next; j=j->next){
	}

	if(j->job_is_bg || job_is_completed(j)) return;

	for(p = j->first_process; p; p = p->next){
		if(kill(p->pid, SIGTSTP) < 0)
			perror("yash: kill(SIGTSTP)");
		p->stopped = 1;
	}
	if(!job_is_stopped(j))
		perror("yash: unable to stop job");
	if(job_is_stopped(j))
		j->stopped = 1;
	fflush(stdout);
	fflush(stdin);
}

void yash_sigchld_handler(int signo){
	pid_t pid_r;
	pid_t pid;
	int status;
	job* j, j2, j3;
	process* p, p2, p3;
	int fbreak = 0;

	do{
		signal(SIGCHLD, yash_sigchld_handler);
		pid = waitpid(-1, &status, WUNTRACED | WNOHANG);
		for(j = first_job; j; j=j->next){
			if(fbreak) break;
			for(p = j->first_process; p; p=p->next){
				if(p->pid == pid){
					if(WIFEXITED (status) && j->job_is_bg){
						j->need_to_print = 1;
						j->stopped = 0;
						for(p = j->first_process; p; p=p->next){
							p->completed = 1;
						}
						return;
					}

				}
			}
		}

	}while(pid > 0);


	fflush(stdout);
	fflush(stdin);
}

void yash_loop(){
	char* line;
	char** line_tokenized;
	job* j;
	job* jprint;
	int foreground = 1;

	do{
		signal(SIGTSTP, yash_sigtstp_handler);
		fflush(stdout);
		fflush(stdin);
		line = readline("$ ");
		for(jprint = first_job; jprint; jprint=jprint->next){
			char most_recent;
			if(jprint->next == NULL)
				most_recent = '+';
			else
				most_recent = '-';

			if(jprint->need_to_print){
				fprintf(stdout, "[%d] %c Done %s \n", jprint->num, most_recent, jprint->command );
			}
			jprint->need_to_print = 0;
		}

		if(line == NULL){
			//printf("line == null, EOF encountered, closing terminal");
			return;
		}
		if(strcmp(line, "fg") == 0){
			job* bg_or_stopped = NULL;
			job* j;
			for(j = first_job; j; j=j->next){
				if(j->stopped || j->job_is_bg){
					bg_or_stopped = j;
				}
			}
			if(bg_or_stopped != NULL){
				continue_job(bg_or_stopped, 1);
				fprintf(stdout, "\n%s\n", bg_or_stopped->command);
			}else{
				perror("yash: no background or stopped jobs to bring to foreground\n");
			}
			continue;
		}
		if(strcmp(line, "bg") == 0){
			job* stopped = NULL;
			for(j = first_job; j; j=j->next){
				if(job_is_stopped(j) && !job_is_completed(j)){
					stopped = j;
				}
			}
			if(stopped == NULL){
				perror("yash: no stopped jobs to send to background");
				continue;
			}
			continue_job(stopped, 0);
			fflush(stdout);
			fprintf(stdout, "%s &\n", stopped->command);
			continue;
		}
		if(strcmp(line, "jobs") == 0){
			char* running_stopped;
			char most_recent;
			job* j;
			for(j = first_job; j; j=j->next){
				if(j->stopped && !job_is_completed(j)){
					running_stopped = "Stopped";
				}else if(!job_is_completed(j)){
					running_stopped = "Running";
				}

				if(j->next == NULL){
					most_recent = '+';
				}else{
					most_recent = '-';
				}
				if(j == NULL) break;
				if(job_is_completed(j)) continue;
				printf("[%d] %c %s		%s\n", j->num, most_recent, running_stopped, j->command);


			}
			continue;
		}
		line_tokenized = yash_split_line(line, &token_count);
		if(line[0] != NULL){
			if(first_job == NULL){
				j = malloc(sizeof(job));
				j->num = job_num;
				j->first_process = NULL;
				j->next = NULL;
				j->need_to_print = 0;
				j->job_is_bg = 0;
				//j->pgid = job_num;
				first_job = j;
				yash_configure_job(j, line, line_tokenized, token_count, &foreground);
				yash_launch_job(j, foreground);
			}else{
				job* next_j;
				job* j;

				for(j = first_job; j->next; j=j->next){}
				next_j = malloc(sizeof(job));
				j->next = next_j;
				next_j->num = job_num;
				//next_j->pgid = job_num;
				next_j->first_process = NULL;
				next_j->need_to_print = 0;
				next_j->next = NULL;
				next_j->job_is_bg = 0;
				yash_configure_job(next_j, line, line_tokenized, token_count, &foreground);
				yash_launch_job(next_j, foreground);
			}
		}
		job_num++;
	}while(1);
}

void yash_configure_job(job* j, char* line, char** line_tokenized, int token_count, int* foreground){
	int i;
	process* p;
	int argv_counter = 0;
	int mypipe[2];
	j->command = line;

	p = malloc(sizeof(process));
	p->infile = STDIN_FILENO;
	p->outfile = STDOUT_FILENO;
	p->errfile = STDERR_FILENO;
	p->completed = 0;
	p->argv = malloc(500);
	p->next = NULL;
	p->pid = 0;
	if(j->first_process == NULL){
		j->first_process = p;
	}else{

	}
	p->next = NULL;
	for(i = 0; i < token_count; i++){
		if(strcmp(line_tokenized[i], "<") == 0){
			//TODO configure this process for input redirect
			p->infile = open(line_tokenized[i+1], 0 );
			i++;
			if (p->infile < 0){
				perror("yash: cannot open file for input redirection");
				p->infile = STDIN_FILENO;
			}
			continue;
		}
		if(strcmp(line_tokenized[i], ">") == 0){
			//TODO configure this process for output redirect
			p->outfile = open(line_tokenized[i+1], O_RDWR | O_CREAT );
			if(p->outfile < 0){
				perror("yash: cannot open or create file for output redirection");
				p->outfile = STDOUT_FILENO;
			}
			if(chmod(line_tokenized[i+1], S_IRWXU) < 0){
				perror("yash: couldn't set read permissions for outfile");
			}
			i++;
			continue;
		}
		if(strcmp(line_tokenized[i], "2>") == 0){
			//TODO configure this process for err redirect
			p->errfile = open(line_tokenized[i+1], O_CREAT);
			if(p->errfile < 0){
				perror("yash: cannot open or create file for err redirection");
				p->errfile = STDERR_FILENO;
			}
			i++;
			continue;
		}
		if(strcmp(line_tokenized[i], "2>&1") == 0){
			p->errfile = STDOUT_FILENO;
			continue;
		}
		if(strcmp(line_tokenized[i], "|") == 0){
			//TODO we're doine with this process.
			//Make a new process and link to p
			process* new_p = malloc(sizeof(process));
			new_p->infile = STDIN_FILENO;
			new_p->outfile = STDOUT_FILENO;
			new_p->errfile = STDERR_FILENO;
			new_p->next = NULL;
			new_p->pid = 0;
			new_p->completed = 0;
			new_p->argv = malloc(sizeof(500));
			argv_counter = 0;
			p->next = new_p;
			if(pipe(mypipe) < 0){
				perror("yash: pipe failure");
				_exit(1);
			}
			p->outfile = mypipe[1];
			new_p->infile = mypipe[0];
			p = new_p;
			continue;
		}
		if(strcmp(line_tokenized[i], "&") == 0){
			*foreground = 0;
			continue;
		}
		p->argv[argv_counter] = line_tokenized[i];
		argv_counter ++;
	}
}

void yash_launch_process(process* p, pid_t pgid, int foreground){
	pid_t pid;

	pid = getpid();
	if(pid == 0) pgid = pid;
	//setpgid(pid, pgid);
	//fflush(stdout);
	//fprintf(stdout, "IM A CHILD MY PID IS %d GPID IS %d status %d\n", pid, pgid, p->status);
	//if(foreground) tcsetpgrp(shell_terminal, pgid); //set this process as the foreground process

	/* Set the handling for job control signals back to the default.  */
    signal (SIGINT, yash_sigint_handler);
    signal (SIGQUIT, SIG_DFL);
    signal (SIGTSTP, yash_sigtstp_handler);
    signal (SIGTTIN, SIG_IGN);
    signal (SIGTTOU, SIG_IGN);
    signal (SIGCHLD, yash_sigchld_handler);

    if(p->infile != STDIN_FILENO){
    	if(dup2(p->infile, STDIN_FILENO) < 0)
    		perror("dup2 failure: infile");
    	close(p->infile);
    }
    if(p->outfile != STDOUT_FILENO){
    	if(dup2(p->outfile, STDOUT_FILENO) < 0)
    		perror("dup2 failure: outfile");
    	close(p->outfile);
    }
    if(p->errfile != STDERR_FILENO){
    	if(dup2(p->errfile, STDERR_FILENO) < 0)
    		perror("dup2 failure: errfile");
    	close(p->errfile);
    }
    execvp(p->argv[0], p->argv);
    fprintf(stderr, "yash: %s: command not found\n", p->argv[0]);

    _exit(1);
}

void yash_launch_job(job* j, int foreground){
	process* p;
	pid_t pid;
	int cont = 0;

	//infile = j->stdin;
	for(p = j->first_process; p; p=p->next){


		/* Fork the child process*/
		pid = fork();
		p->pid = pid;
		if(pid == 0){
			yash_launch_process(p, j->pgid,
								 foreground);
		}else if(pid < 0){
			perror("fork failed");
			_exit(1);
		}else{
			/* This is the parent process*/

			signal(SIGTSTP, yash_sigtstp_handler);
			if(!j->pgid)
				j->pgid = pid;
			setpgid(pid, j->pgid);
			//fflush(stdout);
			//fprintf(stdout, "Im a parrent: pid %d pgid %d\n p->status %d\n", pid, j->pgid, p->status);
			//clean up after pipes
			if (p->outfile != STDOUT_FILENO)
				close (p->outfile);
			if(p->infile != STDIN_FILENO)
				close(p->infile);

		}
	}
	if(foreground){
		yash_put_job_in_fg(j, cont);
	}else{
		yash_put_job_in_bg(j, cont);
	}
}

void yash_put_job_in_fg(job* j, int cont){
	//tcsetpgrp(shell_terminal, j->pgid);
	//get last process, wait for that one
//	if(cont){
//
//		if(kill(-j->pgid, SIGCONT) < 0)
//			perror("kill (SIGCONT), couldn't put job in foreground");
//	}
	process* p;
	if(cont){
		for(p = j->first_process; p; p=p->next){
			if(kill(p->pid, SIGCONT) < 0){
				perror("kill (SIGCONT) couldnt put job in foreground\n");
			}
		}
	}
	mark_job_as_running(j);
	j->job_is_bg = 0;
	//wait for each process in the job to complete before continuing.
	wait_for_job(j);

	//return shell to foreground
	//tcsetpgrp(shell_terminal, shell_pgid);
	return;
}

void yash_put_job_in_bg(job* j, int cont){

	process* p;
	if(cont){
		for(p = j->first_process; p; p=p->next){
			if(kill(p->pid, SIGCONT) < 0){
				perror("kill (SIGCONT), couldn't put job in background");
				_exit(1);
			}

		}
	}
	j->job_is_bg = 1;
	mark_job_as_running(j);
}
void wait_for_job(job *j){
    int status;
    pid_t pid;
    process* p;

    for(p= j->first_process; p; p=p->next){
    	pid = waitpid(p->pid, &status, WUNTRACED);
    	mark_process_status(pid, status);
    	if(job_is_stopped(j))
    		break;
    	if(job_is_completed(j))
    		break;
    }




//    do{
//        pid = waitpid(p->pid, &status, WUNTRACED); //WAIT_ANY is -1
//
//        if(pid<0){
//        	fprintf(stderr, "yash: waitpid returned in error: errno - %d ",errno  );
//        	//_exit(1);
//        }
//    } while(!mark_process_status(pid, status) && !job_is_stopped(j) && !job_is_completed(j));

}

int mark_process_status (pid_t pid, int status){
  job *j;
  process *p;
  if (pid > 0){
      /* Update the record for the process.  */
      for (j = first_job; j; j = j->next)
        for (p = j->first_process; p; p = p->next)
          if (p->pid == pid){
              p->status = status;
              if (WIFSTOPPED (status))
                p->stopped = 1;
              else{
                  p->completed = 1;
                  //if (WIFSIGNALED (status))
                    //fprintf (stderr, "%d: Terminated by signal %d.\n",
                             //(int) pid, WTERMSIG (p->status));
                }
              return 0;
             }
      fprintf (stderr, "No child process %d.\n", pid);
      return -1;
    }
  else if (pid == 0 || errno == ECHILD)
    /* No processes ready to report.  */
    return -1;
  else {
    /* Other weird errors.  */
    perror ("waitpid");
    return -1;
  }
}


char** yash_split_line(char* line, int* token_count){
    int         buff_size = 2048;
    int         position = 0;
    char**      tokens = malloc(buff_size * sizeof(char*));
    char*       token;
    int 		token_num = 0;
    char* 		line_copy = malloc(2000);

    strcpy(line_copy, line);

    if(!tokens){
        fprintf(stderr, "yash: Allocation error, not enough memory\n");
        _exit(EXIT_FAILURE);
    }

    token = strtok(line_copy, YASH_TOK_DELIM);
    while(token != NULL){
        tokens[position] = token;
        position++;

        if(position >= buff_size){
            buff_size += YASH_TOK_BUFFSIZE;
            tokens = realloc(tokens, buff_size * sizeof(char*));
            if (!tokens){
                fprintf(stderr, "yash: Allocation error, not enough memory\n");
                _exit(EXIT_FAILURE);
            }
        }
        token_num++;
        token = strtok(NULL, YASH_TOK_DELIM);
    }
    *token_count = token_num;
    tokens[position] = NULL;
    return tokens;
}

void mark_job_as_running (job *j){
  process *p;

  for (p = j->first_process; p; p = p->next)
    p->stopped = 0;
  j->stopped = 0;
  j->notified = 0;
}
/* Continue the job J.  */

void continue_job (job *j, int foreground){
	if (foreground)
    yash_put_job_in_fg (j, 1);
  else
    yash_put_job_in_bg (j, 1);

}

/* Return true if all processes in the job have stopped or completed.  */
int job_is_stopped (job *j){
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed && !p->stopped)
      return 0;
  return 1;
}

/* Return true if all processes in the job have completed.  */
int job_is_completed (job *j){
  process *p;

  for (p = j->first_process; p; p = p->next)
    if (!p->completed)
      return 0;
  return 1;
}

job * find_job (pid_t pgid){
  job *j;

  for (j = first_job; j; j = j->next)
    if (j->pgid == pgid)
      return j;
  return NULL;
}

/*******************  Main Loop  ************************/
int main(int argc, char** argv){
    //welcome message
    printf("Hello! Welcome to Reed's Project 1 shell\n");

    //initialize the shell;
    yash_init();
    //run the shell loop
    yash_loop();

    return EXIT_SUCCESS;
}
