#include<stdio.h>
#include<stdlib.h>
#include<sys/types.h>
#include<unistd.h>
#include<fcntl.h>
#include<ctype.h>
#include<string.h>
#include<sys/wait.h>
#define MAX_SIZE 1024

#define ERROR do { \
	fprintf(stderr, "Error!\n"); \
	return -1; \
} while (0)

#define MINE_STR_CPY(CPY_IN) \
	do {                       \
		(CPY_IN) = malloc(sizeof(char) * (factors->len + 1)); \
		memcpy(CPY_IN, factors->str, sizeof(char) * factors->len); \
		(CPY_IN)[factors->len] = '\0'; \
	} while(0)
int cnt_cmds = 0;
char buf[MAX_SIZE];

typedef struct {
	char *input; //command's input of file discriptor
	char *output;//command's output to which file discriptor
	char *append;//command appending to which file discriptor 
	char **args; // command's args
	char *cmd;   //command's name
} Command;

// corresponds with '|', '<', '>', '>>', cmd or args, EndTag
typedef enum{
	PIPE, LEFT, RIGHT, R2, CMD, END
} FType;

typedef struct {
	FType type;
	char *str; // should not free???
	int len;   //str.length
} Factor;

void add(Factor *factors, FType type, char *str) {
	factors->type = type;
	factors->str = str;
	factors->len = type == R2 ? 2 : 1;
}

int buf2factors(char *buf, Factor *factors) {
	while (*buf) {
		while (*buf && isspace(*buf)) ++buf;
		if (!*buf) break;

		if (*buf == '|') add(factors, PIPE, buf), ++buf;
		else if (*buf == '>' || *buf == '<') {
			FType type = *buf == '<' ? LEFT :
				*buf == '>' && *(buf + 1) == '>' ? R2 :
				RIGHT;
			++buf;
			//we need to check '>>'
			if(*buf == '>') ++buf;
			add(factors, type, NULL);
		} else if(*buf == '\'' || *buf == '\"'){
			factors->type = CMD;
			buf++;
			factors->str = buf;
			char *start = buf;
			while(*buf && *buf != '\'' && *buf != '\"') buf++;
			factors->len = buf - start;
			buf++;
		} else { // symbol
			factors->type = CMD;
			factors->str = buf;

			char *start = buf;
			while (*buf && !isspace(*buf) &&
					*buf != '|' && *buf != '<' && *buf != '>')
				++buf;
			factors->len = buf - start;
		}
		++factors;
	}
	add(factors, END, NULL);
	return 0;
}

int factors2cmds(Factor *factors, Command *commands) {
	while (factors->type != END) {
		int argsNum = 0;
		Factor *start = factors;
		while (start->type != END && start->type != PIPE) {
				start++;
				argsNum++;
		};
		char **args = commands->args = malloc(sizeof(char *) * (argsNum+1));
		int has_cmd = 0;

		while (factors->type != END && factors->type != PIPE) {
			if (factors->type == CMD) {
				if (!has_cmd) {
					if (factors->type == CMD) {
						has_cmd = 1;
						MINE_STR_CPY(commands->cmd);
						//int execvp(const char *file, char * const argv []), 
						//argv[0] should be the name of cmd, so we should execute alloc for *args again 
					} else ERROR;
				}
				MINE_STR_CPY(*args);
				factors++;
				args++;
			} else if (factors->type == LEFT ||
					factors->type == RIGHT ||
					factors->type == R2) {
				char **dstp = factors->type == LEFT ? &commands->input : //redirection for <
					factors->type == RIGHT ? &commands->output ://redirection for >
					&commands->append;//redirection for >>
				++factors;
				if (factors->type != CMD) ERROR;
				MINE_STR_CPY(*dstp);
				++factors;
			}
		}

		*args = malloc(sizeof(char));
		*args = '\0';
		commands++;
		cnt_cmds++;
		if (factors->type == PIPE) {
			++factors; 
			if (factors->type == END) ERROR; // pipe followed by '\n'
		}
	}
	return 0;
}


int parse_commands(char *buf, Command *commands) {// buf is input
	Factor *factors = malloc(MAX_SIZE * sizeof(Factor));//token is the factor of one command
	if (factors == NULL) return -1;

	buf2factors(buf, factors);//decompose buf into factors
	int ret = factors2cmds(factors, commands); //make commands using factors
	if(ret == -1) return -1;
	free(factors);				
	return 0;
}



void execute(Command *command, int fd_in, int fd_out) {
	if (strcmp(command->cmd, "exit") == 0) exit(0); // user input exit means finishing the env 

	pid_t pid = fork();

	if (pid == 0) {//son
		if (command->input) { //input is from redirection 
			int in = open(command->input, O_RDONLY);
			dup2(in, STDIN_FILENO);//stdin 
		} else if (fd_in > 0) { //input is from pipe
			dup2(fd_in, STDIN_FILENO);
		}

		if (command->output) { //output need to be redirected
			int out = open(command->output, O_RDWR | O_CREAT | O_TRUNC, 0666);

			dup2(out, STDOUT_FILENO);
		} else if (command->append) {//output need to be appended
			int append = open(command->append, O_WRONLY | O_CREAT | O_APPEND, 0666);
			dup2(append, STDOUT_FILENO);
		} else if (fd_out > 0) {//output to pipe
			dup2(fd_out, STDOUT_FILENO);
		}

		int ret = execvp(command->cmd, command->args);

		if(ret == -1){
			fprintf(stderr, "Error!\n"); 
			exit(1);
		}
	} else {
		int status;
		waitpid(pid, &status, 0);
	}
}

int main()
{
	int i;
	char nowDir[MAX_SIZE];
	printf("welcome to LShell!\n");
	while(1){
		printf("LShell->");
		getcwd(nowDir, MAX_SIZE);
		printf("%s$", nowDir); //print now directory

		if (fgets(buf, MAX_SIZE, stdin) != NULL) { //read a cmd line
			buf[strlen(buf) - 1] = '\0'; // get rid of '\n' in the end of line
			cnt_cmds = 0;//cnt_cmds is the count of commands

			Command *commands = calloc(1, MAX_SIZE * sizeof(Command));
			int r = parse_commands(buf, commands);
			if (r == 0) {
				//execute cmds
				int filedes[2], preFd0 = -1;
				for (int i = 0; i < cnt_cmds; ++i) {
					pipe(filedes);
					//execute cmd
					execute(commands + i, i == 0 ? -1 : preFd0, i == cnt_cmds - 1 ? -1 : filedes[1]);
					//filedes[1] is a file descriptor, output to next cmd
					close(filedes[1]);
					if (preFd0 > 0) close(preFd0);
					//filedes[0] is a file descriptor, input for next cmd
					preFd0 = filedes[0];
				}
				if (preFd0 > 0) close(preFd0);
				
				//free cmds
				for(i=0;i<cnt_cmds;i++){
					Command *temp = &commands[i];
					free(temp->input);
					free(temp->output);
					free(temp->append);
					free(temp->cmd);
					for (char **args = temp->args; *args; ++args) free(*args);
					free(temp->args);
				}
				free(commands);
			}
		} 
	}


	return 0 ;
}


