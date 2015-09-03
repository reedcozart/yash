//Author:	Reed Cozart
//EID: 	  	rc27727
//PROJECT 1 - A BASIC SHELL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/wait.h>

#define MBSH_TOK_BUFFSIZE 64
#define MBSH_TOK_DELIM " \n"

//function declarations for built-in shell commands
//int mbsh_cd(char** args);
//int mbsh_help(char** args);
int mbsh_exit(char** args);

char* mbsh_readline(void);
char** mbsh_split_line(char* line);
int mbsh_launch(char** args);
int mbsh_execute(char** args);
int mbsh_exit(char**);

char* builtin_str[1] = {
    //"cd", //TODO Update when compiling
    //"help",
    "exit"
};

int (*builtin_func[])(char**) = {
    //&mbsh_cd,
    //&mbsh_help,
    &mbsh_exit
};


/*********************** SUBROUTINES ************************/
int num_builtins(){
    return sizeof(builtin_str) / sizeof(char*);
}

void mbsh_loop(void){
    char* line;
    char** args;
    int status;


    do {
    	printf("$ ");
        line = mbsh_readline();
        args = mbsh_split_line(line);
        status = mbsh_execute(args);

        free(line);
        free(args);
    }while (status);
}

char* mbsh_readline(void){
	char* line = malloc(2000 * sizeof(char));
    ssize_t buff_size = 2000;
    if(fgets(line, buff_size, stdin) != NULL){
    	//all good, fgets worked fine.
    }else{
        printf("Unable to get user input!\n");
        exit(EXIT_FAILURE);
    }
    return line;
}

char** mbsh_split_line(char* line){
    int         buff_size = 2048;
    int         position = 0;
    char**      tokens = malloc(buff_size * sizeof(char*));//SEGFAULT HERE
    char*       token;

    if(!tokens){
        fprintf(stderr, "mbsh: Allocation error, not enough memory\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(line, MBSH_TOK_DELIM);
    while(token != NULL){
        tokens[position] = token;
        position++;

        if(position >= buff_size){
            buff_size += MBSH_TOK_BUFFSIZE;
            tokens = realloc(tokens, buff_size * sizeof(char*));
            if (!tokens){
                fprintf(stderr, "mbsh: Allocation error, not enough memory\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, MBSH_TOK_DELIM);
    }
    tokens[position] = NULL;
    return tokens;
}

int mbsh_launch(char** args){
    pid_t pid, wpid;
    int status;

    pid = fork();
    if(pid == 0){
        //I'm a child!
        if(execvp(args[0], args) == -1){
            perror("mbsh: unable to execute command");
        }
    }else if(pid < 0){
        //error forking
        perror("mbsh: Error forking");
    }else{
        //parent process
        do{
            wpid = waitpid(pid, &status, WUNTRACED); //TODO CHANGE HERE WHEN WE GET TO &
        }while(!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

int mbsh_execute(char** args){
    int i;
    int n = num_builtins();

    if(args[0] == NULL){
        //empty command.
        return 1;
    }
    for(i = 0; i < n; i++){
        if(strcmp(args[0], builtin_str[i]) == 0){
            return (*builtin_func[i])(args);
        }
    }

    return mbsh_launch(args);
}

int mbsh_exit(char** args){
    return 0;
}

/*******************  Main Loop  ************************/
int main(int argc, char** argv){

    //welcome message
    printf("Hello! Welcome to Reed's Project 1 shell (mbsh - My Basic Shell)\n");

    //register the signal handlers
    //signal(SIGINT, signal_callback_handler);
    //signal(SIGTSTP, signal_callback_handler);
    //signal(SIGCHLD, signal_callback_handler);

    //run the shell loop
    mbsh_loop();

    return EXIT_SUCCESS;
}














