//Author:	Reed Cozart
//EID: 	  	rc27727
//PROJECT 1 - A BASIC SHELL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MBSH_TOK_BUFFSIZE 64
#define MBSH_TOK_DELIM " "

//function declarations for built-in shell commands
int mbsh_cd(char** args);
int mbsh_help(char** args);
int mbsh_exit(char** args);

char* builtin_str[] = {
    "cd",
    "help",
    "exit"
};

int (*builtin_func[])(char**) = {
    &mbsh_cd,
    &mbsh_help,
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

    printf("$ ");
    do {
        line = mbsh_readline();
        args = mbsh_split_line(line);
        status = mbsh_execute(args);
        
        free(line);
        free(args);
    }while (status);
}

char* mbsh_readline(void){
    char* line;
    ssize_t buffsize = 2000;
    getline(&line, &buffsize, stdin);
    return line;   
}

char** mbsh_split_line(char* line){
    int         buff_size = MBSH_TOK_BUFFSIZE;
    int         position = 0;   
    char**      tokens = malloc(buff_size * sizeof(char*));
    char*       token;
    
    if(!tokens){
        fprintf(stderr, "mbsh: Allocation error, not enough memory\n");
        exit(EXIT_FAILURE);
    }  
    
    token = strtok(line, MBSH_TOK_DELIM);
    while(token != NULL){
        tokens[position] = token;
        position++;

        if(position >= buffsize){
            buffsize += MBSH_TOK_BUFFSIZE;
            tokens = realloc(tokens, buffsize * sizeof(char*));
            if (!tokens){
                printf(stderr, "mbsh: Allocation error, not enough memory\n");
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
            perror("mbsh");
        }
    }else if(pid < 0){
        //error forking
        perror("mbsh");
    }else{
        //parent process
        do{
            wpid = waitpid(pid, &status, WUNTRACED); //TODO CHANGE HERE WHEN WE GET TO &
        }while(!WIFEXITED(status) && !WIFSIGNALED(status))
    }

    return 1;
}

int mbsh_execute(char** args){
    int i;

    if(args[0] == NULL){
        //empty command.
        return 1;
    }    
    for(i = 0; i < num_builtins(); i++){
        if(strcmp(args[0], builtin_str[i] == 0)){
            return (*builtin_func[i])(args);
        }
    }

    mbsh_launch(args);
}

/*******************  Main Loop  ************************/
int main(int argc, char** argv){
    
    //welcome message
    printf("Hello! Welcome to Reed's Project 1 shell (mbsh)");
    
    //run the shell loop
    mbsh_loop();

    return EXIT_SUCCESS;
}














