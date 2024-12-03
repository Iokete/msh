#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "include/parser.h"


#define YELLOW "\033[1;33m"
#define END "\033[0m"
#define BUFSIZE 1024

/*
 * Creates a child process to execute a command via execvp
 * We don't take as a parameter line->command because of the needed use of redirection info
 *
 */

void execute_pipeline(tline * line) {
	// TODO: Array dinámico de PIDs
	for (int i = 0; i < line->ncommands; i++) {
		if (line->commands[i].filename == NULL) {
			fprintf(stderr, "%s: command not found\n", line->commands[i].argv[0]);
			break;
		}

		int pipefd[2];
		pipe(pipefd);

		const pid_t pid = fork();

		if (pid == -1) { perror("fork"); exit(1); }
		if (pid == 0) {
			signal(SIGINT, SIG_DFL);

			/*
			 * pipefd[0] -> Read end
			 * pipefd[1] -> Write end
			 */

			if (i == 0) {
				// Si es el primero
				close(pipefd[0]); // unused
				dup2(pipefd[1], STDOUT_FILENO);
				close(pipefd[1]);
			}
			if ( i < line->ncommands-1 && i > 0 ) {
				dup2(pipefd[0], STDIN_FILENO);
			}


			// if it has a redirect input filename we redirect STDIN into the fd of the provided file
			if (line->redirect_input != NULL && i == 0) {
				const int input_fd = open(line->redirect_input, O_RDONLY);
				if (input_fd == -1) { perror("open"); exit(1); }
				dup2(input_fd, STDIN_FILENO); // Sustituye la entrada de STDIN (0) por input_fd
				close(input_fd); // Cierra input_fd (original)
			}

			// if it has a redirect output filename we redirect STDOUT into the fd of the new file
			if (line->redirect_output != NULL && i == line->ncommands - 1) {
				const int out_fd = open(line->redirect_output, O_CREAT | O_TRUNC | O_WRONLY, 0644);
				dup2(out_fd, STDOUT_FILENO);
				close(out_fd);
			}

			// if it has a redirect error filename we redirect STDERR into the fd of the new file
			if (line->redirect_error != NULL && i == line->ncommands - 1) {
				const int err_fd = open(line->redirect_error, O_CREAT | O_TRUNC | O_WRONLY, 0644);
				dup2(err_fd, STDERR_FILENO);
				close(err_fd);
			}

			execvp(line->commands[i].argv[0], line->commands[i].argv);

		}

	}

	// TODO: waitpid() for i in pids
}


void execute_command(const tline *line, const int idx) {

	if (line->commands[idx].filename == NULL) {
		fprintf(stderr, "%s: command not found\n", line->commands[idx].argv[0]);
		execute_command(line, idx + 1);
	}

	int pipefd[2];
	pipe(pipefd);


	const pid_t pid = fork();

	if (pid == -1) { perror("fork"); exit(1); }
	if (pid == 0) {
		signal(SIGINT, SIG_DFL);

		/*
		 * pipefd[0] -> Read end
		 * pipefd[1] -> Write end
		 */

		// if it has a redirect input filename we redirect STDIN into the fd of the provided file
		if (line->redirect_input != NULL && idx == 0) {
			const int input_fd = open(line->redirect_input, O_RDONLY);
			if (input_fd == -1) { perror("open"); exit(1); }
			dup2(input_fd, STDIN_FILENO); // Sustituye la entrada de STDIN (0) por input_fd
			close(input_fd); // Cierra input_fd (original)
		}

		// if it has a redirect output filename we redirect STDOUT into the fd of the new file
		if (line->redirect_output != NULL && idx == line->ncommands - 1) {
			const int out_fd = open(line->redirect_output, O_CREAT | O_TRUNC | O_WRONLY, 0644);
			dup2(out_fd, STDOUT_FILENO);
			close(out_fd);
		}

		// if it has a redirect error filename we redirect STDERR into the fd of the new file
		if (line->redirect_error != NULL && idx == line->ncommands - 1) {
			const int err_fd = open(line->redirect_error, O_CREAT | O_TRUNC | O_WRONLY, 0644);
			dup2(err_fd, STDERR_FILENO);
			close(err_fd);
		}

		execvp(line->commands[idx].argv[0], line->commands[idx].argv);

	} else {
		waitpid(pid, NULL, 0);
		if (idx < line->ncommands - 1) {
			execute_command(line, idx + 1);
		}

	}
}

/*
 * Definition of cd shell builtin
 *
 * arg: parser.h tline object
 * if the path provided is null, target_dir is the one inside HOME env variable
 * converts ~ into user's home directory
 *
 */

void cd(const tline *line) {
	char *home_dir = getenv("HOME");
	char *target_dir = NULL;


	if (home_dir == NULL) {
		fprintf(stderr, "cd: $HOME not found.\n");
		return;
	}

	if (line->commands[0].argv[1] == NULL) {
		target_dir = home_dir;
	} else if (line->commands[0].argv[1][0] == '~') {
		const size_t path_len = strlen(home_dir) + strlen(line->commands[0].argv[1]);
		target_dir = malloc(path_len + 1);
		snprintf(target_dir, path_len, "%s%s", home_dir, line->commands[0].argv[1]+1);

	} else {
		target_dir = line->commands[0].argv[1];
	}

	if (chdir(target_dir)) {
		fprintf(stderr, "cd: %s: no such file or directory\n", target_dir);
		return;
	}

	if (target_dir != home_dir && line->commands[0].argv[1] != target_dir) {
		memset(target_dir, '\0', sizeof(target_dir));
		free(target_dir);
	}
}

/*
 * For each command in ncommands
 * Check if it is cd/exit/bg/fg/jobs (builtins) and call the function
 * Else call execute_command(command[i])
 *
 */

void eval(const tline * line) {

	if (!strcmp(line->commands[0].argv[0], "cd")) {
		cd(line);
	} else if (!strcmp(line->commands[0].argv[0], "exit") || !strcmp(line->commands[0].argv[0], "quit")) {
		exit(0);
	} else {
		execute_command(line, 0);
	}

}



int main (void) 
{
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);


	while(1){
		char buf[BUFSIZE] = {0};
		char path[BUFSIZE] = {0};

		getcwd(path, sizeof(path)); // getcwd() to print it inside the prompt

    	printf("[:" YELLOW " %s " END "] msh> ", path); // yellow prompt

		fflush(stdout);
		if (fgets(buf, BUFSIZE, stdin) == NULL) {
			if (feof(stdin)) { printf("\nEnd of input. Exiting...\n");
				break;
			}
			if (ferror(stdin)) {
				perror("Error reading input"); clearerr(stdin);
			}
		} else {
			const tline *line = tokenize(buf);

			if(line == NULL) continue;

			eval(line);
		}

	}

	return 0;
}