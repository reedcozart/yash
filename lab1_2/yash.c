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


#define EXIT_SUCCESS 0
#define EXIT_FAILURE -1
#define YASH_TOK_DELIM " "
#define YASH_TOK_BUFFSIZE 64

int shell_pgid;
int shell_terminal;
int token_count;
int status = 1;
int mypipe[2];

//Taken from gnu.org
typedef struct process{
	struct process* next;
	char** argv;
	pid_t pid;
	char completed;
	char stopped;
	int status;
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
	struct termios tmodes;
	int stdin, stdout, stderr;
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

void yash_init(){

	shell_terminal = STDIN_FILENO;
	while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
	        kill (- shell_pgid, SIGTTIN);

	signal (SIGINT, SIG_IGN);
	signal (SIGQUIT, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);
	signal (SIGTTIN, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);
	signal (SIGCHLD, SIG_IGN);

	shell_pgid = getpid();
	return;
}
job* first_job = NULL;


void yash_loop(){
	char* line;
	char** line_tokenized;
	job* j;
	int foreground = 1;//TODO fix when we get to BG tasks
	//TODO setup processes in chain
	do{
		line = readline("$ ");
		if(strcmp(line, "exit") == 0){
			return;
		}
		line_tokenized = yash_split_line(line, &token_count);
		if(line[0] != NULL){
			if(first_job == NULL){
				j = malloc(sizeof(job));
				j->first_process = NULL;
				j->next = NULL;
				first_job = j;
				yash_configure_job(j, line, line_tokenized, token_count, &foreground);
				yash_launch_job(j, foreground);
			}else{
				j = first_job;
				while(j->next != NULL){
					j = j->next;
				}
				j = malloc(sizeof(job));
				j->first_process = NULL;
				j->next = NULL;
				yash_configure_job(j, line, line_tokenized, token_count, &foreground);
				yash_launch_job(j, foreground);
			}
		}
	}while(status);
}

void yash_configure_job(job* j, char* line, char** line_tokenized, int token_count, int* foreground){
	int i;
	process* p;
	int argv_counter = 0;
	int mypipe[2];

	p = malloc(sizeof(process));
	p->infile = STDIN_FILENO;
	p->outfile = STDOUT_FILENO;
	p->errfile = STDERR_FILENO;
	p->argv = malloc(500);
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
	setpgid(pid, pgid);
	if(foreground) tcsetpgrp(shell_terminal, pgid); //set this process as the foreground process
    /* Set the handling for job control signals back to the default.  */
    signal (SIGINT, SIG_DFL);
    signal (SIGQUIT, SIG_DFL);
    signal (SIGTSTP, SIG_DFL);
    signal (SIGTTIN, SIG_DFL);
    signal (SIGTTOU, SIG_DFL);
    signal (SIGCHLD, SIG_DFL);

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
    perror("execvp failed");
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
		if(pid == 0){
			yash_launch_process(p, j->pgid,
								 foreground);
		}else if(pid < 0){
			perror("fork failed");
			_exit(1);
		}else{
			/* This is the parent process*/
			p->pid = pid;
			if(!j->pgid)
				j->pgid = pid;
			setpgid(pid, j->pgid);
			if(foreground){
				yash_put_job_in_fg(j, cont);
			}else{
				yash_put_job_in_bg(j, cont);
			}
		}
	}
}

void yash_put_job_in_fg(job* j, int cont){
//	tcsetpgrp(shell_terminal, j->pgid);
	process* p_handle;
	//wait for job
	//get last proscess, wait for that one
	for(p_handle = j->first_process; p_handle->next != NULL; p_handle=p_handle->next){}

	if(cont){
		if(kill(-j->pgid, SIGCONT) < 0)
			perror("kill (SIGCONT), couldn't put job in foreground");
	}

	waitpid(p_handle->pid, &status, 0);
	//return shell to foreground
//	tcsetpgrp(shell_terminal, shell_pgid);
	return;
}

void yash_put_job_in_bg(job* j, int cont){
	if(cont){
		if(kill(-j->pgid, SIGCONT) < 0){
			perror("kill (SIGCONT), couldn't put job in background");
		}
	}
}

char** yash_split_line(char* line, int* token_count){
    int         buff_size = 2048;
    int         position = 0;
    char**      tokens = malloc(buff_size * sizeof(char*));
    char*       token;
    int 		token_num = 0;

    if(!tokens){
        fprintf(stderr, "yash: Allocation error, not enough memory\n");
        _exit(EXIT_FAILURE);
    }

    token = strtok(line, YASH_TOK_DELIM);
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
