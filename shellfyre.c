#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <dirent.h>
const char *sysname = "shellfyre";

enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};

struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;

	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}

void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	// FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;

	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n'){ // enter key
			if (index == 1){
				tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
				return prompt(command);
			}
			break;
		}
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}

int process_command(struct command_t *command);

int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

int filesearch(struct command_t *command, char *relative_path) 	{
		if (command->arg_count > 0)
		{
			char *keyword = command->args[0];
			int recursive = 0;
			int open = 0;
			int i;
			for (i = 1; i < command->arg_count; ++i)
			{
				if (strcmp(command->args[i], "-r") == 0)
					recursive = 1;
				else if (strcmp(command->args[i], "-o") == 0)
					open = 1;
			}
			if (recursive)
			{
				DIR *dir;
				struct dirent *ent;
				if ((dir = opendir(".")) != NULL)
				{
					while ((ent = readdir(dir)) != NULL)
					{
						if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
							continue;
						if (ent->d_type == DT_DIR)
						{
							chdir(ent->d_name);
              char *new_relative_path = malloc(sizeof(char) * (strlen(ent->d_name) + strlen(relative_path) + 2));
              strcpy(new_relative_path, relative_path);
              strcat(new_relative_path, "/");
              strcat(new_relative_path, ent->d_name);
							filesearch(command, new_relative_path);
							chdir("..");
						}
            else {
              if (strstr(ent->d_name, keyword) != NULL)
              {
                if (open){
                  char *cmd = malloc(sizeof(char) * (strlen(ent->d_name) + strlen(sysname) + strlen("xdg-open ") + 1));
                  strcpy(cmd, "xdg-open ");
                  strcat(cmd, ent->d_name);
                  system(cmd);
                }
                printf("%s/%s\n", relative_path, ent->d_name);
              }
            }
					}
					closedir(dir);
				}
			}
			else
			{
				DIR *dir;
				struct dirent *ent;
				if ((dir = opendir(".")) != NULL)
				{
					while ((ent = readdir(dir)) != NULL)
					{
						if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
							continue;
						if (strstr(ent->d_name, keyword) != NULL)
						{
							if (open){
                char *cmd = malloc(sizeof(char) * (strlen(ent->d_name) + strlen(sysname) + strlen("xdg-open ") + 1));
                strcpy(cmd, "xdg-open ");
                strcat(cmd, ent->d_name);
                system(cmd);
              }
							printf("%s\n", ent->d_name);
						}
					}
					closedir(dir);
				}
			}
			return SUCCESS;
		}
	}

// The command takes no arguments.
// After calling cdh, the shell should output a list of most recently visited directories. The list
// should also include the index of the directory as both a number and an alphabetic letter.
// The shell would then prompt the user which directory they want to navigate to; the user
// can select either a letter or a number from the list. After this, the shell should switch to
// that directory. If there are no previous directories to select from, the shell should output a
// warning. 
int cdh(struct command_t *command)
{
		char *home = getenv("HOME");
	char *history_file = malloc(strlen(home) + strlen(".dir_history") + 1);
	strcpy(history_file, home);
	strcat(history_file, "/.dir_history");

	FILE *f = fopen(history_file, "r");
	if (f == NULL)
	{
		printf("No history found\n");
		return SUCCESS;
	}
	// Read all lines and copy them to a new array.
	char *lines[11];
	int line_count = 0;
	char line[1024];
	char buf[6];
	while (fgets(line, sizeof(line), f))
	{
		lines[line_count] = malloc(strlen(line) + 6);
		char letter = 'a' + line_count;
		snprintf(buf, 6, "%c %d) ", letter, line_count + 1);
		strcpy(lines[line_count], buf);
		strcat(lines[line_count++], line);
	}
	fclose(f);

	// Print the lines.
	int i;
	for (i = 0; i < line_count; ++i)
		printf("%s", lines[i]);
	
	// Prompt the user for a char.
	char choice;
	printf("\nEnter a number or letter: ");
	scanf("%c", &choice);
	if (choice >= 'a' && choice <= 'z')
		choice -= 'a';
	else if (choice >= 'A' && choice <= 'Z')
		choice -= 'A';
	else if (choice >= '1' && choice <= '9')
		choice -= '1';
	else
	{
		printf("Invalid input\n");
		return SUCCESS;
	}
	if (choice >= line_count)
	{
		printf("Invalid input\n");
		return SUCCESS;
	}
	while ((getchar()) != '\n'); // Clear the buffer. 

	// Switch to the directory.
	strcpy(command->name, "cd");
	command->arg_count = 1;
	command->args = malloc(sizeof(char *) * 1);
	command->args[0] = malloc(strlen(lines[choice]) + 1);
	for (i = 0; i < strlen(lines[choice]); ++i){
		if (lines[choice][i] == '\n'){
			lines[choice][i] = '\0';
			break;
		}
	}
	//printf("hello: %s\n", lines[c]+5);
	strcpy(command->args[0], lines[choice]+5);
	process_command(command);

	return SUCCESS;
}

void add_directory_to_history(char *path)
{
	// Open .dir_history file in User directory and read all lines.
	char *home = getenv("HOME");
	char *history_file = malloc(strlen(home) + strlen(".dir_history") + 1);
	strcpy(history_file, home);
	strcat(history_file, "/.dir_history");

	FILE *f = fopen(history_file, "r");
	if (!f)
	{
		f = fopen(history_file, "w");
		fclose(f);
		f = fopen(history_file, "r");
	}

	// Read all lines and copy them to a new array.
	char *lines[11];
	int line_count = 0;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		lines[line_count] = malloc(strlen(line) + 1);
		strcpy(lines[line_count++], line);
	}
	fclose(f);

	// Add the new path to the array.
	lines[line_count] = malloc(strlen(path) + 1);
	strcpy(lines[line_count++], path);

	// Remove the oldest path if the array is full.
	if (line_count > 10)
	{
		free(lines[0]);
		int i;
		for (i = 1; i <= 10; ++i)
			lines[i - 1] = lines[i];
		line_count--;
	}

	// Write the new array to the file.
	f = fopen(history_file, "w");
	int i;
	for (i = 0; i < line_count; ++i)
	{
		fprintf(f, "%s", lines[i]);
		if (i == line_count - 1)
			fprintf(f, "\n");
	}

	fclose(f);
}

// In this part, you will implement a command called take which takes 1 argument: the name
// of the directory you want to create and change into. The command will create a directory
// and change into it. The command must create the intermediate directories along the way if
// they do not exist. For example: if you call take A/B/C, the command should create the
// directories that do not exist and change into the last one (i.e, A/B/C).
// Note: the idea for the command was adapted from the take command from zsh.
int take(struct command_t *command){

	//allocate memory for the args array
	char** args = malloc(sizeof(char **) * (4));
	args[0] = "mkdir";
	args[1] = command->args[0];
	args[2] = "-p";
	args[3] = NULL;

	pid_t pid = fork();

	if (pid == 0) // child
		execvp("mkdir", args);
	
	wait(NULL);

	strcpy(command->name, "cd");
	process_command(command);
	free(args);
	return SUCCESS;
}

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0)
		return EXIT;

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			else
				// for the cdh command 
				add_directory_to_history(getcwd(NULL, 0));
			return SUCCESS;
		}
	}
	if (strcmp(command->name, "filesearch") == 0)
    return filesearch(command, ".");
	if (strcmp(command->name, "cdh") == 0){
		return cdh(command);
	}
	if (strcmp(command->name, "take") == 0)
		return take(command);

	pid_t pid = fork();

	if (pid == 0) // child
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		/// TODO: do your own exec with path resolving using execv()
		// check if the command starts with a './'
		if (command->args[0][0] == '.' && command->args[0][1] == '/')
		{
			// if so, remove the './'
			command->args[0] += 2;
			execv(command->args[0], command->args);
		}
		else
		{
			// if not, try to find the command in the PATH
			char *path = getenv("PATH");
			char *path_token = strtok(path, ":");
			while (path_token != NULL)
			{
				char *full_path = malloc(strlen(path_token) + strlen(command->args[0]) + 2);
				strcpy(full_path, path_token);
				strcat(full_path, "/");
				strcat(full_path, command->args[0]);
				execv(full_path, command->args);
				free(full_path);
				path_token = strtok(NULL, ":");
			}
			free(path);
		}
		exit(0);
	}
	else
	{
		/// TODO: Wait for child to finish if command is not running in background
		if (!command->background)
			waitpid(pid, NULL, 0);

		return SUCCESS;
	}

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}