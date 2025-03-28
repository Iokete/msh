#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "include/parser.h"

#define BUFSIZE 1024

typedef struct Job {
	int id;
	char *command;
	pid_t *pids;
	int stopped; // 1 stopped (true) | 0 running (false)
	int num_pids;
} Job;

Job *jobs;
int job_count = 0;

void add_job(char* command) {

	command[strlen(command) - 1] = '\0';
	const tline *line = tokenize(command);

	jobs[job_count].id = job_count;
	jobs[job_count].pids = malloc(sizeof(pid_t) * line->ncommands);
	jobs[job_count].command = strdup(command);
	jobs[job_count].stopped = 0;
	jobs[job_count].num_pids = line->ncommands;

	job_count++;
}

void delete_job(const int job_id) {
	for (int i = 0; i < job_count; i++) {
		if (jobs[i].id == job_id) {
			free(jobs[i].pids);
			free(jobs[i].command);
			for (int j = i; j < job_count - 1; j++) {
				jobs[j] = jobs[j + 1];
			}

			job_count--;
			if (job_count == 0) {
				jobs = malloc(sizeof(Job));
			} else {
				jobs = realloc(jobs, sizeof(Job) * (job_count));
			}
			printf("Deleted job [%d]\n", job_id);
			return;
		}
	}
	fprintf(stderr, "Job ID %d not found\n", job_id);
}

Job *_get_job(int id) {
	for (int i = 0; i < job_count; i++) {
		if (jobs[i].id == id) {
			return &jobs[i];
		}
	}
	return NULL;
}

void sigchld_handler(int sig) {
	pid_t pid;
	int status;

	while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
		for (int i = 0; i < job_count; i++) {
			for (int j = 0; j < jobs[i].num_pids; j++) {
				if (pid == jobs[i].pids[j]) {

					if (WIFEXITED(status)) {
						printf("\nJob [%d] (%s) finished with status %d\n", jobs[i].id, jobs[i].command, WEXITSTATUS(status));
					} else if (WIFSIGNALED(status)) {
						printf("\nJob [%d] terminated with signal %d\n", jobs[i].id, WTERMSIG(status));
					}
					jobs[i].stopped = 1;
					delete_job(jobs[i].id);
					break;
				}
			}
		}
	}
}

void print_jobs() {
	if (job_count <= 0) {
		printf("%s", "There are no jobs.\n");
	} else {
		char *running = "Stopped";
		for (int i = 0; i < job_count; i++) {
			if (!jobs[i].stopped) running = "Running";
			printf("[%d] %s \t\t%s\n", jobs[i].id, running, jobs[i].command);
		}
	}
}

void fg(const int job_id) {
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);

	Job *job = _get_job(job_id);

	if (job != NULL) {
		if (!job->stopped) {
			for (int j = 0; j < job->num_pids; j++) {
				int status;
				waitpid(job->pids[j], &status, WUNTRACED);
			}
			job->stopped = 1;
		} else {
			printf("Job [%d] is not running.\n", job_id);
		}

	} else {
		fprintf(stderr, "\nInvalid job ID %d", job_id);
	}


}


/*
 * Creates a child process to execute a command via execvp
 * We don't take as a parameter line->command because of the needed use of redirection info
 *
 */

void execute_pipeline(const tline * line, char *cmd) {
	pid_t *pids = malloc(sizeof(pid_t) * line->ncommands);
	int in_fd = 0;

	for (int j = 0; j < line->ncommands; j++) {
		if (line->commands[j].filename == NULL) {
			fprintf(stderr, "%s: No se encuentra el mandato.\n", line->commands[j].argv[0]);
			return;
		}
	}

	if (line->background) {

		if (job_count == 0) {
			jobs = malloc(sizeof(Job));
		} else {
			jobs = realloc(jobs, sizeof(Job) * (job_count + 1));
		}
		add_job(cmd);
	}
	for (int i = 0; i < line->ncommands; i++) {

		int pipefd[2];
		pipe(pipefd);

		const pid_t pid = fork();

		if (pid == -1) { perror("fork"); exit(1); }
		if (pid == 0) { // Child
			signal(SIGINT, SIG_DFL);
			signal(SIGQUIT, SIG_DFL);

			/*
			 * pipefd[0] -> Read end
			 * pipefd[1] -> Write end
			 */

			if ( i != line->ncommands-1) { // Si no es el ultimo sustituimos stdout por el write end
				dup2(pipefd[1], STDOUT_FILENO);
				close(pipefd[1]);
				close(pipefd[0]);
			}



			if ( i != 0 ) { // si no es el primero sustituimos stdin por el read end del anterior
				dup2(in_fd, STDIN_FILENO);
				close(in_fd);
			}


			// if it has a redirect input filename we redirect STDIN into the fd of the provided file
			if (line->redirect_input != NULL && i == 0) {
				const int input_fd = open(line->redirect_input, O_RDONLY);
				if (input_fd == -1) {
					fprintf(stderr, "%s: Error. ", line->redirect_input);
					perror("open");
					exit(1);
				}
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
			fprintf(stderr, "Something went wrong!\n");
		}
		// Parent

		if (i < line->ncommands - 1 ) {
			close(pipefd[1]);
		}
		if (i != 0) {
			close(in_fd);

		}
		in_fd = pipefd[0];

		if (line->background) {
			Job *curr = _get_job(job_count - 1);
			curr->pids[i] = pid;
		} else {
			pids[i] = pid;
		}
	}

	if (!line->background) {
		for (int i = 0; i < line->ncommands; i++) {
			waitpid(pids[i], NULL, 0);
		}
	}


	free(pids);
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
 * Check if it is cd/exit/fg/jobs (builtins) and call the function
 * Else call execute_command(command[i])
 *
 */

void eval(const tline * line, char * command) {
	if (!strcmp(line->commands[0].argv[0], "cd")) {
		cd(line);
	} else if (!strcmp(line->commands[0].argv[0], "exit") || !strcmp(line->commands[0].argv[0], "quit")) {
		if (job_count > 0) {
			char try;
			printf("There are running jobs, are you sure? (y/n): ");
			scanf(" %c", &try);
			while (getchar() != '\n');
			if (try == 'n') return;
			free(jobs);
		}
		exit(0);
	} else if (!strcmp(line->commands[0].argv[0], "fg")) {
		line->commands[0].argv[1] == NULL ? fg(jobs[0].id) : fg(atoi(line->commands[0].argv[1]));
	}
	else if (!strcmp(line->commands[0].argv[0], "jobs")) {
		print_jobs();
	} else {
		execute_pipeline(line, command);
	}

}


int main (void) 
{
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	signal(SIGCHLD, sigchld_handler);

	while (1) {
		char buf[BUFSIZE] = {0};
		char path[BUFSIZE] = {0};

		getcwd(path, sizeof(path)); // getcwd() to print it inside the prompt
		printf("msh> ");

		if (fgets(buf, BUFSIZE, stdin) == NULL) {
			printf("\n");
			break;
		}
		if (buf != NULL) {

				const tline *line = tokenize(buf);

				if(line->ncommands == 0) continue;

				eval(line, buf);
		}
	}



	return 0;
}
