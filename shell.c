#define _POSIX_SOURCE
#define _XOPEN_SOURCE_EXTENDED 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>

#define WAIT_ANY -1

pid_t shell_pgid;
int shell_terminal;
int shell_is_interactive;
struct termios shell_tmodes;

	/*	Make sure the shell is running interactively as the
	foreground job before proceeding	*/
void init_shell () {     
       /* See if we are running interactively.  */
	shell_terminal = STDIN_FILENO;
	shell_is_interactive = isatty (shell_terminal);

	if (shell_is_interactive) {
		while (tcgetpgrp (shell_terminal) != (shell_pgid = getpgrp ()))
			kill (- shell_pgid, SIGTTIN);	
		signal (SIGQUIT, SIG_IGN);
		signal (SIGTSTP, SIG_IGN);
		signal (SIGTTIN, SIG_IGN);
		signal (SIGTTOU, SIG_IGN);
		signal (SIGCHLD, SIG_IGN);
		shell_pgid = getpid();
		if (setpgid (shell_pgid, shell_pgid) < 0)
			printf("ERROR: Couldn't put the shell in its own process group\n");
		/*	Grab control of the terminal	*/
		tcsetpgrp(shell_terminal, shell_pgid);
		/*	Save default terminal attributes for shell. */
		tcgetattr (shell_terminal, &shell_tmodes);
	}	
}

typedef struct process {
	struct process *next;       /* next process in pipeline */
	char **argv;                /* for exec */
	pid_t pid;                  /* process ID */
} process;
     
     /* A job is a pipeline of processes.  */
typedef struct job {
	struct job *next;           /* next active job */
	process *first_process;     /* list of processes in this job */
	pid_t pgid;                 /* process group ID */
	struct termios tmodes;      /* saved terminal modes */
	char * input;		 /* file i/o channels */
	char * output; 
} job;

/* The active jobs are linked into a list.  This is its head.   */
job *first_job = NULL;

	/*  Initializing a job */
job * job_initialize (char **argv, int  num_tokens, int *foreground) {
	job * j;
	process * p;
	char ** command;
	int i, counter,test;
	
	j = (job *) malloc (sizeof(job));
	j->first_process = NULL;
	j->input = NULL;
	j->output = NULL;
	command = (char **) malloc (sizeof(char **) * (num_tokens + 1));
	
	/*	Checks if argument is intended to run in the background */
	if (strcmp(argv[num_tokens - 1], "&") == 0) {
		*foreground = 0;
		num_tokens--;
	}
	else 
		*foreground = 1;

	/*	Check incoming parsed input for piping and or redirection */
	counter = 0;		//  Used to keep track of individual command tokens
	for ( i = 0; i < num_tokens; i++) {		
		if (strcmp(argv[i], "|") == 0) {
			if (!j->first_process) {
				j->first_process = (process *) malloc (sizeof(process));
				j->first_process->argv = (char ** ) malloc (counter * 100);  // arbitrarily large
				for ( test = 0; test < counter; test++) 
					j->first_process->argv[test] = command[test];
				j->first_process->argv[test] = '\0';
				j->first_process->next = NULL;
			}	
			else {
				p = j->first_process; 
				while (p->next)
					p = p->next;
				p->next = (process *) malloc (sizeof(process));
				p->next->argv = (char ** ) malloc (counter * 100);
				p = p->next;
				for ( test = 0; test < counter; test++) 
					p->argv[test] = command[test];
				p->argv[test] = '\0';
				p->next = NULL;				
			}
			/*	Clear data stored in command and begin storing */
			memset(command, '\0', sizeof(char**) * num_tokens);	
			counter = 0;
		}		
		else if(strcmp(argv[i],  "<") == 0) {
			if((j->first_process)  || (num_tokens <= i + 1 )) {
				printf("ERROR: Unable to redirect files in this manner\n");
				return NULL;
			}			
			j->input = argv[++i];
			if (num_tokens == i + 1) {
				j->first_process = (process *) malloc (sizeof(process));
				j->first_process->argv = (char ** ) malloc (counter * 100);
				for ( test = 0; test < counter; test++) 
					j->first_process->argv[test] = command[test];
				j->first_process->argv[test] = '\0';
				j->first_process->next = NULL;
				return j;
			}
		}
		else if(strcmp(argv[i],  ">") == 0) {
			if ( i + 2 == num_tokens){		//  There was a token specified for redirection
				j->output = argv[i + 1];
				command[counter] = '\0';
				if (!j->first_process) {
					j->first_process = (process *) malloc (sizeof(process));
					j->first_process->argv = command;
				}
				else {
					p = j->first_process; 
					while (p->next)
						p = p->next;
					p->next = (process *) malloc (sizeof(process));
					p = p->next;
					p->next = NULL;
					p->argv = command;
				} 
				return j;
			}
			else {
				printf("ERROR: Incorrect specification of files\n");
				return NULL;
			}
		} 
		else
			command[counter++] = argv[i];
	}
	command[counter] = '\0';
	if (!j->first_process) {
		j->first_process = (process *) malloc (sizeof(process));
		j->first_process->argv = command;
	}
	else {
		p = j->first_process; 
		while (p->next)
			p = p->next;
		p->next = (process *) malloc (sizeof(process));
		p->next->argv = command;
	} 
	return j;
}


void  parse(char *line, char **argv, int  *tokens) {
	*tokens = 0;
	while (*line != '\0') {       /* if not the end of line ....... */ 

		while (isspace(*line) || iscntrl(*line)) {
			if (*line == '\0') {
				*argv = '\0';			
				return;
			}
			*line++ = '\0';     /* replace white spaces with NULL terminator   */
		}
	         /* save the argument position     */ 
		if(*line != '<' && *line != '>' && *line != '|') {
			*argv++ = line; 
			(*tokens)++;
		}
		while (isalnum(*line) || ispunct(*line)) {
			if (*line == '<') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = "<";
				break;
			}
			else if (*line == '>') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = ">";
				break;
			}
			else if (*line == '|') {
				(*tokens)++;
				*line++ = '\0';
				*argv++ = "|";
				break;
			}
			else
				line++;             /* skip the argument until ...    */
		}
	}
	*argv = '\0';                 /* mark the end of argument list  */
}


void put_job_in_foreground (job *j) {
       /* Put the job into the foreground.  */
	tcsetpgrp (shell_terminal, j->pgid);
   
       /* Wait for it to report.  */
	waitpid (WAIT_ANY, 0, WUNTRACED);

       /* Put the shell back in the foreground.  */
	tcsetpgrp (shell_terminal, shell_pgid);
       /* Restore the shell's terminal modes.  */
	tcgetattr (shell_terminal, &j->tmodes);
	tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes);
}


    /* Store the status of the process pid that was returned by waitpid.
        Return 0 if all went well, nonzero otherwise.  */	

void launch_job (job *j, int foreground) {
	process *p;
	pid_t pid;
	int mypipe[2], infile, outfile;
    
  	if (j->input){
  		if((infile = open(j->input, O_RDONLY))< 0) {
  			printf("ERROR: Could not open read file\n");
			return;
		}
	}
  	else
		infile = STDIN_FILENO;
	for (p = j->first_process; p; p = p->next) {
           /* Set up pipes, if necessary.  */
		if (p->next){
			if (pipe (mypipe) < 0) {  /*	If pipe fails */
				printf("ERROR: Unable to pipe input\n");
				return;
			}
			outfile = mypipe[1];
		}
		else if (j->output) {
			outfile = open(j->output, O_RDWR | O_CREAT | O_TRUNC, S_IWUSR | S_IRUSR);
		}
		else
			outfile = STDOUT_FILENO;
           /* Fork the child processes.  */
		pid = fork ();
		if (pid == 0) {
     	       /* This is the child process.  */
			if (shell_is_interactive) {
				signal (SIGINT, SIG_DFL);
				signal (SIGQUIT, SIG_DFL);
				signal (SIGTSTP, SIG_DFL);
				signal (SIGTTIN, SIG_DFL);
				signal (SIGTTOU, SIG_DFL);
				signal (SIGCHLD, SIG_DFL);					
				pid = getpid ();
				if (j->pgid == 0) 
					j->pgid = pid;
				setpgid (pid, j->pgid);
				if (foreground)
					tcsetpgrp (shell_terminal, j->pgid);			
			}
			if (infile != STDIN_FILENO) {
				if(close(STDIN_FILENO) < 0) 
					printf("ERROR: Could not close STDIN\n");
				if(dup(infile) != STDIN_FILENO)
					printf("ERROR: Could not dup infile\n");
			}
			if (outfile != STDOUT_FILENO) {
				if (close(STDOUT_FILENO) < 0)
					printf("ERROR: Could not close STDOUT\n");			
				if (dup (outfile) != STDOUT_FILENO)
					printf("ERROR: dup outfile\n");
			}	
			if (execvp (p->argv[0], p->argv) < 0) 
				printf("ERROR: Could not execute command\n");
			exit (1);
		}
		else if (pid < 0) {
     	          /* The fork failed.  */
			printf("ERROR: forking child process failed\n");
			exit (1);
		}
		else {
     	         /*  This is the parent process.  */ 	      
			p->pid = pid;
			if (shell_is_interactive) {
				if (!j->pgid)
	         			j->pgid = pid;
         			setpgid (pid, j->pgid); 	    
			}
		}
	if (infile != STDIN_FILENO)
		close(infile);
	if (outfile != STDOUT_FILENO)
		close(outfile);
	infile = mypipe[0];
	}
	if (foreground)
		put_job_in_foreground(j);
}

      /*    Delete terminated jobs from the active job list.  */
void free_job(job * j) {
	free (j);
}

 
void cd (char * dir) {
	char path[100];
	
	if (dir != NULL) {
		getcwd(path, sizeof(path));     //  Grabs current working directory
		strncat(path, "/", 1);
		strncat(path, dir, strlen(dir));		//  append desired change in directory to cwd
		if (chdir(path) < 0)		//  Changes cwd and checks for failure
			printf("ERROR: No such file or directory %s\n", dir);			
		return;		
	}
}

int  main(int argc, char ** argvFILE) {
	char  line[1024];             /* the input line                 */
	char  *argv[64];              /* the command line argument      */
	char * p;
	int tokens, foreground;
	int * ptokens =&tokens;
	int * pforeground =&foreground;

	int input_from_file = ftell(stdin);		/* check if input is coming from file */
	
	init_shell();	
	while (1) {                   /* repeat until done ....         */
		if(input_from_file < 0)		/*	stdin is coming from user not file */
			printf("sish:>");        /*   display a prompt             */   	             	
		memset (line, '\0', sizeof(line));		// zero line, (fills array with null terminator)
          memset (argv, '\0', sizeof(argv));
          if (!fgets(line, sizeof(line), stdin)) 	{printf("\n"); return 0;}	// Exit upon ctrl-D
     	if(strlen(line) == 1) {
     		continue;	//	 check for empty string aka 'enter' is pressed without input
          }	
          if ((p = strchr(line, '\n')) != NULL)	//	remove '\n' from the end of 'line'
			*p = '\0';
		parse (line, argv, ptokens);		// parse input to shell
		if (argv[0] == '\0')
			continue;
		else if (strcmp(argv[0], "exit") == 0)  /* is it an "exit"?     */      
			return 0;            /*   exit if it is                */        
		else if (strcmp(argv[0], "cd") == 0) 
			cd (argv[1]);	             	
		else {
			if ((first_job = job_initialize(argv, tokens, pforeground)) != NULL) {
				launch_job	(first_job, foreground);
				free_job(first_job);
			}
		}
	}
}
