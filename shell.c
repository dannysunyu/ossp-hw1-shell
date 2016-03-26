#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>

#include "tokenizer.h"

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

void launch_process(struct tokens *tokens);
int execute_cmd(struct tokens *tokens);

bool cmd_pathname_needs_resolution(char *cmd);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_exit, "exit", "exit the command shell"},
  {cmd_pwd, "pwd", "print current working directory"},
  {cmd_cd, "cd", "change current working directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(struct tokens *tokens) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(struct tokens *tokens) {
  exit(0);
}

/* Prints working directory */
int cmd_pwd(struct tokens *tokens) {
  printf("%s\n", getcwd(NULL, 0));
  return 1;
}

/* Changes working directory */
int cmd_cd(struct tokens *tokens) {
  char *target_dir = tokens_get_token(tokens, 1);
  char error_msg[128];

  if (chdir(target_dir) == 0) {
    cmd_pwd(tokens);
  } else {
    sprintf(error_msg, "cd: %s", target_dir);
    perror(error_msg);
  }

  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Initialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(int argc, char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {
      launch_process(tokens);
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}

void launch_process(struct tokens *tokens) {
  pid_t pid = fork();

  if (pid == 0) { /* Child */
    if (execute_cmd(tokens) == -1) {
      fprintf(stdout, "This shell doesn't know how to run programs.\n");
      exit(1);
    }
  } else { /* Parent */
    wait(NULL);
  }
}

int execute_cmd(struct tokens *tokens) {
  size_t cmd_line_length = tokens_get_length(tokens);
  char *cmd_argv[cmd_line_length + 1];

  for (int i = 0; i < cmd_line_length; i++) {
    cmd_argv[i] = tokens_get_token(tokens, i);
  }
  /* execv requires the last element of this array must be a null pointer */
  cmd_argv[cmd_line_length] = NULL;

  if (cmd_pathname_needs_resolution(cmd_argv[0])) {
    char *path_env = getenv("PATH");
    char *each_dir = NULL;
    char full_path_cmd[256];

    while ((each_dir = strsep(&path_env, ":")) != NULL) {
      // resolving path
      strcpy(full_path_cmd, each_dir);
      strcat(full_path_cmd, "/");
      strcat(full_path_cmd, cmd_argv[0]);

      // can't touch cmd_argv
      char *cmd_tmp = cmd_argv[0];
      cmd_argv[0] = full_path_cmd;

      execv(full_path_cmd, cmd_argv);

      // restore original command for next iteration
      cmd_argv[0] = cmd_tmp;
    }

    return -1;
  } else {
    return execv(cmd_argv[0], cmd_argv);
  }
}

/* Only cmd with no pathname specified needs resolution
 *
 * case 1. cmd in current dir: ./cmd needs no resolution
 * case 2. cmd in relative path: foo/bar needs no resolution
 * case 3. cmd in full path needs no resolution: /usr/bin/wc
 * */
bool cmd_pathname_needs_resolution(char *cmd) {
  return strchr(cmd, '/') == NULL;
}


