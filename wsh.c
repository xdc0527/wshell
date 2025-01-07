#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "wsh.h"

char* var_substitute(char* var, char *key, char **local_vars, int local_vars_size) {
    key = key + 1;
    // load env var
    char* value = getenv(key);
    if (value != NULL) {
        return value;
    }
    // load local var
    for (int i = 0; i < local_vars_size; i++) {
        char* local_var = local_vars[i];
        strcpy(var, local_var);
        char* local_key = strtok(var, "=");
        if (strcmp(local_key, key) == 0) {
            value = strtok(NULL, "=");
            return value;
        }
    }
    return "";
}

int replace_var(char *exp, char **local_vars, int local_vars_size) {
    char* local_exp = strdup(exp);
    char* exp_key = strtok(local_exp, "=");
    for (int j = 0; j < local_vars_size; j++) {
        char* local_var = strdup(local_vars[j]);
        char *local_var_key = strtok(local_var, "=");
        if (strcmp(local_var_key, exp_key) == 0) {
            char* exp_dup = strdup(exp);
            free(local_vars[j]);
            local_vars[j] = exp_dup;
            free(local_var);
            free(local_exp);
            return 1;
        }
        free(local_var);
    }
    free(local_exp);
    return 0;
}

int parse_input(char *input, char* args[128]) {
    int i = 0;
    char *arg = strtok(input, " ");
    while(arg != NULL) {
        args[i] = arg;
        i ++;
        arg = strtok(NULL, " ");
    }
    return i;
}

int find_exec_path(char* exec_path, char* command) {
    if (access(command, X_OK) == 0) {
        strcpy(exec_path, command);
        return 1;
    }
    char* paths = getenv("PATH");
    char* paths_dup = strdup(paths);
    char* path = strtok(paths_dup, ":");
    while(path != NULL) {
        char* path_dup = strdup(path);
        strcpy(exec_path, path_dup);
        strcat(exec_path, "/");
        strcat(exec_path, command);
        if (access(exec_path, X_OK) == 0) {
            free(path_dup);
            free(paths_dup);
            return 1;
        }
        path = strtok(NULL, ":");
        free(path_dup);
    }
    free(paths_dup);
    return 0;
}

int compare(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int ls(void) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        return -1;
    }
    char *files[1024];
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        files[count] = strdup(entry->d_name);
        count++;
    }
    closedir(dir);
    qsort(files, count, sizeof(char *), compare);
    for (int i = 0; i < count; i++) {
        printf("%s\n", files[i]);
        fflush(stdout);
        free(files[i]);
    }
    return 0;
}

int process_redirection(char* args[128], int arg_num) {
    FILE* file;
    char* file_name;
    char* arg = args[arg_num-1];
    if (strlen(arg) < 2) return 0;
    if (arg[0] >= '0' && arg[0] <= '9' && (arg[1] == '>' || arg[1] == '<')) {
        int fd = arg[0] - '0';
        if (arg[1] == '>') {
            if (arg[2] == '>') {
                file_name = arg + 3;
                file = fopen(file_name, "a");
            } else {
                file_name = arg + 2;
                file = fopen(file_name, "w");
            }
        } else {
            file_name = arg + 2;
            file = fopen(file_name, "r");
        }
        if (file == NULL) return -1;
        int newfd = fileno(file);
        int rc = dup2(newfd, fd);
        if (rc == -1) return -1;
        args[arg_num-1] = NULL;
        return 0;
    }
    if (arg[0] == '>') {
        if (arg[1] == '>') {
            file_name = arg + 2;
            file = fopen(file_name, "a");
        } else {
            file_name = arg + 1;
            file = fopen(file_name, "w");
        }
        if (file == NULL) return -1;
        int newfd = fileno(file);
        int rc = dup2(newfd, STDOUT_FILENO);
        if (rc == -1) return -1;
        args[arg_num-1] = NULL;
        return 0;
    }
    if (arg[0] == '<') {
        file_name = arg + 1;
        file = fopen(file_name, "r");
        if (file == NULL) return -1;
        int newfd = fileno(file);
        int rc = dup2(newfd, STDIN_FILENO);
        if (rc == -1) return -1;
        args[arg_num-1] = NULL;
        return 0;
    }
    if (arg[0] == '&') {
        if (arg[1] != '>') return -1;
        if (arg[2] == '>') {
            file_name = arg + 3;
            file = fopen(file_name, "a");
        } else {
            file_name = arg + 2;
            file = fopen(file_name, "w");
        }
        if (file == NULL) return -1;
        int newfd = fileno(file);
        int rc = dup2(newfd, STDOUT_FILENO);
        if (rc == -1) return -1;
        rc = dup2(newfd, STDERR_FILENO);
        if (rc == -1) return -1;
        args[arg_num-1] = NULL;
    }
    return 0;
}

int rerun_command(char* input, char **local_vars, int local_vars_size) {
    char* args[128] = {NULL};
    char* input_dup = strdup(input);
    int arg_num = parse_input(input_dup, args);
    if (arg_num == 0) return -1;
    char *var = malloc(128 * sizeof(char));
    for (int i = 0; i < arg_num; i++) {
        if (args[i][0] == '$') {
            args[i] = var_substitute(var, args[i], local_vars, local_vars_size);
        }
    }
    int rc = 0;
    // spawn child to run
    char* exec_path = malloc(128 * sizeof(char));
    if (exec_path != NULL) {
        int found = find_exec_path(exec_path, args[0]);
        if (found) {
            int return_code = fork();
            if (return_code == 0) {
                rc = process_redirection(args, arg_num);
                if (rc != 0) return -1;
                execv(exec_path, args);
            } else {
                wait(&rc);
            }
        } else {
            // command not found
            rc = -1;
        }
        free(exec_path);
    } else {
        // malloc fail
        rc = -1;
    }
    free(var);
    free(input_dup);
    return rc;
}

int main(int argc, char *argv[]) {
    setenv("PATH", "/bin", 1);
    int rc = 0;
    char **history = NULL;
    int max_n = 5;
    int n = 0;
    char **local_vars = NULL;
    int local_vars_size = 0;
    if (argc == 2) {
        FILE *file = fopen(argv[1], "r");
        if (file == NULL) return -1;
        int newfd = fileno(file);
        dup2(newfd, STDIN_FILENO);
    }
    while (1) {
        if (argc < 2) {
            printf("%s", "wsh> ");
            fflush(stdout);
        }
        char *input = NULL;
        size_t sz;
        ssize_t nread = getline(&input, &sz, stdin);
        if (nread >= 0) {
            if (strcmp("\n", input) == 0) continue;
            if (input[nread - 1] == '\n') input[nread - 1] = '\0';
            if (strcmp("exit", input) == 0) {
                free(input);
                break;
            }

            rc = 0;
            char* args[128] = {NULL};
            // parse command
            char *input_dup = strdup(input);
            int arg_num = parse_input(input_dup, args);
            if (arg_num == 0 || args[0][0] == '#') {
                free(input_dup);
                free(input);
                continue;
            }
            // check arg var substitute
            char *var = malloc(128 * sizeof(char));
            for (int i = 0; i < arg_num; i++) {
                if (args[i][0] == '$') {
                    args[i] = var_substitute(var, args[i], local_vars, local_vars_size);
                }
            }

            // execute command
            if (strcmp("history", input) == 0) {
                int i = 0;
                while (i < MIN(n, max_n)) {
                    printf("%d) %s\n", i+1, history[n-i-1]);
                    i ++;
                    fflush(stdout);
                }
            }
            else if (strcmp("vars", input) == 0) {
                for (int i = 0; i < local_vars_size; i++) {
                    printf("%s\n", local_vars[i]);
                    fflush(stdout);
                }
            }
            else if (strcmp("ls", args[0]) == 0) {
                if (arg_num == 1) rc = ls();
                else rc = -1;
            }
            else if (strcmp("history", args[0]) == 0) {
                if (arg_num == 3) {
                    if (strcmp("set", args[1]) == 0) max_n = atoi(args[2]);
                    else rc = -1;
                }
                else if (arg_num == 2) {
                    // rerun command
                    int index = atoi(args[1]);
                    if (index > max_n || index > n || index < 1) {
                        rc = -1;
                    } else {
                        rc = rerun_command(history[n-index], local_vars, local_vars_size);
                    }
                } else {
                    // invalid history command
                    rc = -1;
                }
            }
            else if (strcmp("cd", args[0]) == 0) {
                if (arg_num != 2) {
                    rc = -1;
                } else {
                    rc = chdir(args[1]);
                }
            }
            else if (strcmp("export", args[0]) == 0) {
                for (int i = 1; i < arg_num; i++) {
                    char* key = strtok(args[i], "=");
                    char* value = strtok(NULL, "=");
                    setenv(key, value, 1);
                }
            }
            else if (strcmp(args[0], "local") == 0) {
                for (int i = 1; i < arg_num; i++) {
                    char* arg = args[i];
                    int replace = replace_var(args[i], local_vars, local_vars_size);
                    if (replace) continue;
                    char* arg_dup = strdup(arg);
                    local_vars = realloc(local_vars, i * sizeof(*local_vars));
                    local_vars[local_vars_size] = arg_dup;
                    local_vars_size ++;
                }
            } else {
                // not built-in command
                // record command history
                if (n == 0 || strcmp(history[n-1], input) != 0) {
                    char* command = strdup(input);
                    history = realloc(history, (n + 1) * sizeof(*history));
                    history[n] = command;
                    n ++;
                }
                // spawn child to run
                char* exec_path = malloc(128 * sizeof(char));
                if (exec_path != NULL) {
                    int found = find_exec_path(exec_path, args[0]);
                    if (found) {
                        int return_code = fork();
                        if (return_code == 0) {
                            rc = process_redirection(args, arg_num);
                            if (rc != 0) continue;
                            execv(exec_path, args);
                        } else {
                            int status = 0;
                            wait(&status);
                            if (WIFEXITED(status)) {
                                rc = WEXITSTATUS(status);
                            }
                        }
                    } else {
                        // command not found
                        rc = -1;
                    }
                    free(exec_path);
                } else {
                    // malloc fail
                    rc = -1;
                }
            }
            free(var);
            free(input_dup);
            free(input);
        } else {
            // EOF
            free(input);
            break;
        }
    }
    for (int i = 0; i < local_vars_size; i++) {
        if (local_vars != NULL) free(local_vars[i]);
    }
    free(local_vars);
    for (int i = 0; i < n; i++) {
        if (history != NULL) free(history[i]);
    }
    free(history);

    return rc;
}
