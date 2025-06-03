#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <ctype.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdbool.h>
#include <fcntl.h>
#include <pthread.h>

#define  MAX_LINE_LENGTH 1024
char global_operation[4];
// struct of matrix
typedef struct {
    int rows;
    int cols;
    int* data; // flat array: rows * cols
} Matrix;

// func to split the line and edit the following array
void parse_command_to_array(const char* input_line, char* args[]) {
    int arg_count = 0;
    int in_quotes = 0;
    char token[MAX_LINE_LENGTH];
    int j = 0;

    for (int i = 0; ; i++) {
        char c = input_line[i];

        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if ((c == ' ' && !in_quotes) || c == '\0') {
            if (j > 0) {
                token[j] = '\0';
                args[arg_count++] = strdup(token);
                j = 0;
            }
            if (c == '\0') break;

            } else {
            token[j++] = c;
        }

        if (arg_count >= 31) break;
    }

    args[arg_count] = NULL;
}
// to count how many args in the one line
int countArguments(const char* line) {
    int count = 0;
    int in_arg = 0;
    int in_quotes = 0;

    for (int i = 0; line[i] != '\0'; i++) {
        if (line[i] == '"') {
            in_quotes = !in_quotes; // نبدل حالة التنصيص
            if (!in_arg && in_quotes) {
                in_arg = 1;
                count++;  // نعد الكل داخل التنصيص كوسيط واحد
            } else if (!in_quotes) {
                in_arg = 0; // انتهت الكلمة بعد التنصيص
            }
        } else if (line[i] == ' ' && !in_quotes) {
            in_arg = 0;
        } else if (!in_arg) {
            in_arg = 1;
            count++;
        }
    }

    return count;
}


                        // return if the line command is dangerous or not
bool is_dangerous(const char *line, const char *danger_path, int *blocked, int *notblocked) {
    FILE* fp = fopen(danger_path, "r");
    if (fp == NULL) {
        perror("fopen");
        return false;
    }

    char danger[1024];
    char temp[1024];
    strncpy(temp, line, sizeof(temp));
    temp[sizeof(temp) - 1] = '\0';
    char* firstLineWord = strtok(temp, " ");

    while (fgets(danger, sizeof(danger), fp)) {
        size_t len_d = strlen(danger);
        if (len_d > 0 && danger[len_d - 1] == '\n') {
            danger[len_d - 1] = '\0';
        }

        char temp2[1024];
        strncpy(temp2, danger, sizeof(temp2));
        temp2[sizeof(temp2) - 1] = '\0';
        char* firstDangerWord = strtok(temp2, " ");

        if (strcmp(line, danger) == 0) {
            printf("ERR: Dangerous command detected (%s). Execution prevented.\n", line);
            (*blocked)++;
            fclose(fp);
            return true;
        } else if (strcmp(firstLineWord, firstDangerWord) == 0) {
            printf("WARNING: Command similar to dangerous command (%s). Proceed with caution.\n", danger);
            (*notblocked)++;
        }
    }

    fclose(fp);
    return false;
}

                        // function that update the following parameters and calculating the times
void handle_timing(const char *line, const char *log_path, struct timeval start, struct timeval end,
                   int *success, double *avgTime, double *avgSuccess, double *minTime, double *maxTime, char *prompt, int blocked) {
    (*success)++;
    usleep(50000);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    FILE* argTime = fopen(log_path, "a");
    if (argTime != NULL) {
        fprintf(argTime, "%s : %.5f sec\n", line, elapsed);
        fclose(argTime);
    }
    *avgTime += elapsed;
    *avgSuccess = *avgTime / (*success);
    if (elapsed > *maxTime) *maxTime = elapsed;
    if (elapsed < *minTime) *minTime = elapsed;

    sprintf(prompt, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:%.5f|avg_time:%.5f|min_time:%.5f|max_time:%.5f>> ",
            *success, blocked, elapsed, *avgSuccess, *minTime, *maxTime);
}

                        // function to edit and open the files to type to the the tee function
void handle_my_tee(char **arr) {
    FILE *fps[6];  // نحتفظ بمؤشرات الملفات المفتوحة
    int file_count = 0;

    bool append_mode = strcmp(arr[1], "-a") == 0;
    int start_index = append_mode ? 2 : 1;

    for (int i = start_index; arr[i] != NULL; i++) {
        FILE *fp = fopen(arr[i], append_mode ? "a" : "w");
        if (!fp) {
            perror("fopen");
            exit(1);
        }
        fps[file_count++] = fp;
    }
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        for (int i = 0; i < file_count; i++) {
            fputs(buffer, fps[i]);
        }
        fputs(buffer, stdout);
        fflush(stdout);  // مهم جداً
    }

    for (int i = 0; i < file_count; i++) {
        fclose(fps[i]);
    }
    exit(0);
}

                        // to convert the sizes to bytes(M,Mg ---> byte)
long convert_to_bytes(const char* str) {
    long value = atol(str);
    size_t len = strlen(str);
    if (len >= 2 && (str[len - 2] == 'K' || str[len - 2] == 'k') && (str[len - 1] == 'B' || str[len - 1] == 'b')) return value * 1024;
    if (len >= 2 && (str[len - 2] == 'M' || str[len - 2] == 'm') && (str[len - 1] == 'B' || str[len - 1] == 'b')) return value * 1024 * 1024;
    if (len >= 2 && (str[len - 2] == 'G' || str[len - 2] == 'g') && (str[len - 1] == 'B' || str[len - 1] == 'b')) return value * 1024 * 1024 * 1024;
    if (str[len - 1] == 'K' || str[len - 1] == 'k') return value * 1024;
    if (str[len - 1] == 'M' || str[len - 1] == 'm') return value * 1024 * 1024;
    if (str[len - 1] == 'G' || str[len - 1] == 'g') return value * 1024 * 1024 * 1024;
    return value;
}

                        //function that update the resource limit condition
int ResourceLimit(char **arr, struct rlimit *lim) {
    if (arr[1] == NULL) {
        printf("ERR_ARGS\n");
        return 2;
    }

    int i = 2;
    for (; arr[i] != NULL; i++) {
        if (strchr(arr[i], '=') == NULL)  // ✅ هذا هو التعديل
            break;

        char *arg = strdup(arr[i]);
        char *type = strtok(arg, "=");
        char *values = strtok(NULL, "=");

        if (!type || !values) {
            printf("ERR_ARGS for '%s'\n", arr[i]);
            free(arg);
            continue;
        }

        char *soft_str = strtok(values, ":");
        char *hard_str = strtok(NULL, ":");

        if (!soft_str) {
            printf("ERR_ARGS for '%s'\n", arr[i]);
            free(arg);
            continue;
        }

        if (!hard_str) {
            hard_str = soft_str;
        }

        int res_type = -1;

        if (strcmp(type, "cpu") == 0) {
            lim->rlim_cur = atoi(soft_str);
            lim->rlim_max = atoi(hard_str);
            res_type = RLIMIT_CPU;
        } else if (strcmp(type, "nofile") == 0) {
            lim->rlim_cur = atoi(soft_str);
            lim->rlim_max = atoi(hard_str);
            res_type = RLIMIT_NOFILE;
        } else if (strcmp(type, "fsize") == 0) {
            lim->rlim_cur = convert_to_bytes(soft_str);
            lim->rlim_max = convert_to_bytes(hard_str);
            if (lim->rlim_cur == 0 || lim->rlim_max == 0) {
                printf("ERR_UNIT for '%s'\n", arr[i]);
                free(arg);
                continue;
            }
            res_type = RLIMIT_FSIZE;
        } else if (strcmp(type, "as") == 0 || strcmp(type, "mem") == 0 || strcmp(type, "data") == 0) {
            lim->rlim_cur = convert_to_bytes(soft_str);
            lim->rlim_max = convert_to_bytes(hard_str);
            if (lim->rlim_cur == 0 || lim->rlim_max == 0) {
                printf("ERR_UNIT for '%s'\n", arr[i]);
                free(arg);
                continue;
            }
            res_type = RLIMIT_AS;
        } else if (strcmp(type, "nproc") == 0) {
            lim->rlim_cur = atoi(soft_str);
            lim->rlim_max = atoi(hard_str);
            res_type = RLIMIT_NPROC;
        } else {
            printf("ERR_TYPE for '%s'\n", arr[i]);
            free(arg);
            continue;
        }

        if (setrlimit(res_type, lim) != 0) {
            perror("setrlimit");
        }

        free(arg);
    }

    return i; // هنا بترجع الموقع الصحيح لأول argument بعد limits
}

                        // print the current limit with and without the type
void ShowLimit(char **arr) {
    struct rlimit lim;

    // إذا كان فقط "rlimit show" بدون نوع
    if (arr[2] == NULL) {
        // CPU time
        getrlimit(RLIMIT_CPU, &lim);
        printf("CPU time: soft=%lds, hard=%lds\n", lim.rlim_cur, lim.rlim_max);

        // Memory (AS)
        getrlimit(RLIMIT_AS, &lim);
        if (lim.rlim_cur == RLIM_INFINITY)
            printf("Memory: soft=unlimited, ");
        else
            printf("Memory: soft=%ldb, ", lim.rlim_cur);

        if (lim.rlim_max == RLIM_INFINITY)
            printf("hard=unlimited\n");
        else
            printf("hard=%ldb\n", lim.rlim_max);

        // File size
        getrlimit(RLIMIT_FSIZE, &lim);
        if (lim.rlim_cur == RLIM_INFINITY)
            printf("File size: soft=unlimited, ");
        else
            printf("File size: soft=%ldb, ", lim.rlim_cur);

        if (lim.rlim_max == RLIM_INFINITY)
            printf("hard=unlimited\n");
        else
            printf("hard=%ldb\n", lim.rlim_max);

        // Open files
        getrlimit(RLIMIT_NOFILE, &lim);
        printf("Open files: soft=%ld, hard=%ld\n", lim.rlim_cur, lim.rlim_max);

        return;
    }

    // إذا طلب نوع محدد
    int res_type;
    if (strcmp(arr[2], "cpu") == 0) {
        res_type = RLIMIT_CPU;
        getrlimit(res_type, &lim);
        printf("CPU time limits: soft=%lds, hard=%lds\n", lim.rlim_cur, lim.rlim_max);
    } else if (strcmp(arr[2], "mem") == 0 || strcmp(arr[2], "as") == 0) {
        res_type = RLIMIT_AS;
        getrlimit(res_type, &lim);
        if (lim.rlim_cur == RLIM_INFINITY)
            printf("Memory: soft=unlimited, ");
        else
            printf("Memory: soft=%ldb, ", lim.rlim_cur);

        if (lim.rlim_max == RLIM_INFINITY)
            printf("hard=unlimited\n");
        else
            printf("hard=%ldb\n", lim.rlim_max);
    } else if (strcmp(arr[2], "fsize") == 0) {
        res_type = RLIMIT_FSIZE;
        getrlimit(res_type, &lim);
        if (lim.rlim_cur == RLIM_INFINITY)
            printf("File size: soft=unlimited, ");
        else
            printf("File size: soft=%ldb, ", lim.rlim_cur);

        if (lim.rlim_max == RLIM_INFINITY)
            printf("hard=unlimited\n");
        else
            printf("hard=%ldb\n", lim.rlim_max);
    } else if (strcmp(arr[2], "nofile") == 0) {
        res_type = RLIMIT_NOFILE;
        getrlimit(res_type, &lim);
        printf("Open files: soft=%ld, hard=%ld\n", lim.rlim_cur, lim.rlim_max);
    } else {
        printf("ERR_TYPE\n");
    }
}

                        // print the error of the rlimit if the code receive a signal
void print_rlimit_violation_message(int sig) {
    printf("Terminated by signal: %d\n", sig);
    switch (sig) {
        case SIGXCPU:
            printf("CPU time limit exceeded!\n");
            break;
        case SIGXFSZ:
            printf("File size limit exceeded!\n");
            break;
        case SIGSEGV:
        case SIGBUS:
            printf("Memory allocation failed!\n");
            break;
        case SIGPIPE:
        case SIGCHLD:
        case SIGSYS:
        case SIGABRT:
            printf("Process creation limit exceeded!\n");
            break;
        case SIGKILL:
            printf("CPU time limit exceeded!\n");
            break;

        default:
            printf("Unknown termination signal.\n");
            break;
    }
}
                        // test the matrix format
bool is_valid_matrix_format(const char* str) {
    int len = strlen(str);
    if (len < 7) return false; // all the conditions thats the format is illegal
    if (str[0] != '(' || str[len - 1] != ')') return false;
    if (strchr(str, ':') == NULL || strchr(str, ',') == NULL) return false;
    return true;
}
                        //parse the command to matrixs
int parse_mcalc_input(const char* input, Matrix** matrices_out, int* matrix_count, char* operation_out) {
    char* line = strdup(input);
    if (!line) return 0;

    int count = 0;
    int capacity = 4;
    Matrix* matrices = (Matrix*) malloc(capacity * sizeof(Matrix));
    if (!matrices) {
        printf("ERR_MAT_INPUT\n");
        free(line);
        return 0;
    }

    char* saveptr1;
    char* token = strtok_r(line, " ", &saveptr1);
    char* last_token = NULL;

    while (token != NULL) {
        last_token = token; // store last token for later operation check

        // copy token to safe buffer
        char clean_token[256];
        strncpy(clean_token, token, sizeof(clean_token) - 1);
        clean_token[sizeof(clean_token) - 1] = '\0';

        size_t len = strlen(clean_token);

        // check if it's a matrix
        if (len >= 5 && clean_token[0] == '(' && clean_token[len - 1] == ')') {
            clean_token[len - 1] = '\0'; // remove last ')'
            char* content = clean_token + 1; // skip first '('

            char* colon = strchr(content, ':');
            // if there is not :
            if (!colon) {
                printf("ERR_MAT_INPUT\n");
                free(line); free(matrices);
                return 0;
            }

            *colon = '\0';
            char* size_part = content; // the chars before the : like 2,2
            char* data_part = colon + 1; // the chars after like 1,2,3,4

            int rows, cols;
            // test the rows and the columns
            if (sscanf(size_part, "%d,%d", &rows, &cols) != 2 || rows <= 0 || cols <= 0) {
                printf("ERR_MAT_INPUT\n");
                free(line); free(matrices);
                return 0;
            }

            int expected = rows * cols;
            int* data = (int*) malloc(expected * sizeof(int));
            if (!data) {
                printf("ERR_MAT_INPUT\n");
                free(line); free(matrices);
                return 0;
            }

            int actual = 0;
            char* saveptr2;
            char* num = strtok_r(data_part, ",", &saveptr2);
            // run on the numbers in the command and test the negative nums and save them in array data
            while (num != NULL) {
                if (!isdigit(num[0]) && !(num[0] == '-' && isdigit(num[1]))) {
                    printf("ERR_MAT_INPUT\n");
                    free(data); free(line); free(matrices);
                    return 0;
                }
                data[actual++] = atoi(num);
                num = strtok_r(NULL, ",", &saveptr2);
            }

            // if the number of elements is not correct
            if (actual != expected) {
                printf("ERR_MAT_INPUT\n");
                free(data); free(line); free(matrices);
                return 0;
            }

            // reallocate if needed
            if (count >= capacity) {
                capacity *= 2;
                Matrix* temp = (Matrix*) realloc(matrices, capacity * sizeof(Matrix));
                if (!temp) {
                    printf("ERR_MAT_INPUT\n");
                    free(data); free(line); free(matrices);
                    return 0;
                }
                matrices = temp;
            }

            matrices[count].rows = rows;
            matrices[count].cols = cols;
            matrices[count].data = data;
            // Ensure all matrices have same dimensions
            if (count > 0 && (rows != matrices[0].rows || cols != matrices[0].cols)) {
                printf("ERR_MAT_INPUT\n");
                free(data);
                for (int k = 0; k < count; k++) free(matrices[k].data);
                free(matrices);
                free(line);
                return 0;
            }

            count++;
        }

        token = strtok_r(NULL, " ", &saveptr1);
    }

    // check if the last token is a valid operation
    if (strcmp(last_token, "ADD") == 0 || strcmp(last_token, "SUB") == 0) {
        strcpy(operation_out, last_token);
    } else {
        printf("ERR_MAT_INPUT\n");
        for (int i = 0; i < count; i++) free(matrices[i].data);
        free(matrices); free(line);
        return 0;
    }

    // must have at least two matrices
    if (count < 2) {
        printf("ERR_MAT_INPUT\n");
        for (int i = 0; i < count; i++) free(matrices[i].data);
        free(matrices); free(line);
        return 0;
    }

    *matrices_out = matrices;
    *matrix_count = count;
    free(line);
    return 1;
}
                        // do the operation on 2 matrixs
void* matrix_op_thread(void* arg) {
    // init the matrixs
    Matrix** m = (Matrix**)arg;
    Matrix* A = m[0];
    Matrix* B = m[1];
    Matrix* R = m[2];

    int n = A->rows * A->cols;

    R->rows = A->rows;
    R->cols = A->cols;
    R->data = (int*)malloc(n * sizeof(int));

    for (int i = 0; i < n; i++) {
        if (strcmp(global_operation, "ADD") == 0)
            R->data[i] = A->data[i] + B->data[i];
        else
            R->data[i] = A->data[i] - B->data[i];
    }

    return NULL;
}
                        // parallel reducing to the matrixs
Matrix* parallel_reduce(Matrix* matrices, int count, const char* operation) {
    strcpy(global_operation, operation); // the global op

    while (count > 1) {
        int new_count = (count + 1) / 2;
        Matrix* new_matrices =(Matrix*) malloc(new_count * sizeof(Matrix));
        pthread_t threads[count / 2];
        Matrix** args_arr[count / 2]; // لحفظ مؤشرات args مؤقتًا

        for (int i = 0, t = 0; i + 1 < count; i += 2, t++) {
            Matrix** args =(Matrix**) malloc(3 * sizeof(Matrix*));
            args[0] = &matrices[i];
            args[1] = &matrices[i + 1];
            args[2] = &new_matrices[t];
            args_arr[t] = args;
            pthread_create(&threads[t], NULL, matrix_op_thread, args);
        }

        // بعدها بنستنى كل الخيوط
        for (int t = 0; t < count / 2; t++) {
            pthread_join(threads[t], NULL);
            free(args_arr[t]);
        }


            // if the count is odd just copy the last one
        if (count % 2 == 1) {
            new_matrices[new_count - 1] = matrices[count - 1];
        }
        for (int i = 0; i < count - (count % 2); i++) {
            free(matrices[i].data);
        }

        free(matrices);
        matrices = new_matrices;
        count = new_count;
    }

    Matrix* final_result =(Matrix*) malloc(sizeof(Matrix));
    *final_result = matrices[0];
    free(matrices);
    return final_result;

}

                        // func to handle the mcalc case
void handle_mcalc(char* str) {


    int len = strlen(str);
    // remove the "" from the line command
    if (len > 1 && str[0] == '"' && str[len - 1] == '"') {
        str[len - 1] = '\0';
        str++;
    }
    int i, j = 0;
    for (i = 0; str[i]; ++i) {
        if (str[i] != '"') {
            str[j++] = str[i];
        }
    }
    str[j] = '\0';

    Matrix* matrices = NULL;
    int matrix_count = 0;
    char operation[8];

    if (!parse_mcalc_input(str, &matrices, &matrix_count, operation)) {
        return;
    }
    Matrix* result = parallel_reduce(matrices, matrix_count, operation);
    printf("(%d,%d:", result->rows, result->cols);
    for (int i = 0; i < result->rows * result->cols; i++) {
        printf("%d", result->data[i]);
        if (i < result->rows * result->cols - 1) printf(",");
    }
    printf(")\n");

    free(result->data);
    free(result);


}





int main( int argc,char *argv[]) {
                         // initialize the parameters
    bool blocked_cmd = false;
    int status;
    double avgSuccess = 0;
    double avgTime = 0;
    double minTime = 999999999999999999;
    double maxTime = 0;
    struct timeval start, end;
    bool flag = true;
    int success = 0;
    int blocked = 0;
    int notblocked = 0;
    pid_t bg_pids[128];
    int bg_count = 0;
                        //the first prompt
    char str[256] = "#cmd:0|#dangerous_cmd_blocked:0|last_cmd_time:0|avg_time:0|min_time:0|max_time:0>> ";//first prompt
                        //the kernal code
    while (flag) {
        setenv("PATH", "/bin:/usr/bin", 1);
        printf("%s", str);
        char *line = NULL;
        size_t len = 0;
        ssize_t read = getline(&line, &len, stdin);//first command line
        if (read == -1) {
            printf("ERR\n");
            continue;
        }

                        //command size test
        if (read > MAX_LINE_LENGTH) {
            printf("ERR_ARGS\n");
            free(line);
            continue;
        }


        if (line[read - 1] == '\n') line[read - 1] = '\0';

                        // kill the shell
        if (strcmp(line, "done") == 0) {
            for (int i = 0; i < bg_count; i++) {
                waitpid(bg_pids[i], NULL, 0);
            }

            printf("%d\n", blocked);
            free(line);
            break;
        }

                        // if there is 2 spaces or more
        bool has_double_space = false;
        bool inside_quotes = false;
        for (int i = 0; i < strlen(line) - 1; i++) {
            if (line[i] == '"') {
                inside_quotes = !inside_quotes;
            }

            if (!inside_quotes && line[i] == ' ' && line[i + 1] == ' ') {
                has_double_space = true;
                break;
            }
        }
        if (has_double_space) {
            printf("ERR_SPACE\n");
            free(line);
            continue;
        }
        //  if the command was mcalc
        if (strncmp(line, "mcalc", 5) == 0 && (line[5] == ' ' )) {
            handle_mcalc(line + 6);  // handle the command upthere
            continue; // receive the next line command and skip the exec and te fork
        }


        // if there is an & turn on in the background
        bool background = false;
        int Linelen = strlen(line);
        if (Linelen > 0 && line[Linelen - 1] == '&') {
            background = true;
            line[Linelen - 1] = '\0'; // احذف الـ &
            if (Linelen > 1 && line[Linelen - 2] == ' ') line[Linelen - 2] = '\0'; // نظف المسافة كمان
        }

        //if there is a pipe
        if (strchr(line, '|') != NULL) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        continue;
    }

    char temp[MAX_LINE_LENGTH];
    strncpy(temp, line, MAX_LINE_LENGTH);
    temp[MAX_LINE_LENGTH - 1] = '\0';
        //split to two commands
    char* FirstCom = strtok(temp, "|");
    char* SecondCom = strtok(NULL, "|");

            // Count spaces before and after '|'
            char *pipe_pos = strchr(line, '|');
            if (pipe_pos) {
                // Check space before
                if (pipe_pos == line || *(pipe_pos - 1) != ' ') {
                    printf("ERR_SYNTAX\n");
                    free(line);
                    continue;
                }

                // Check space after
                if (*(pipe_pos + 1) != ' ' || *(pipe_pos + 2) == ' ') {
                    printf("ERR_SYNTAX\n");
                    free(line);
                    continue;
                }
            }

    if (!FirstCom || !SecondCom) {
        printf("ERR_ARGS\n");
        close(pipefd[0]);
        close(pipefd[1]);
        continue;
    }

    while (*SecondCom == ' ') SecondCom++;
        // test the two commands
    if (is_dangerous(FirstCom, argv[1], &blocked, &notblocked) ||
        is_dangerous(SecondCom, argv[1], &blocked, &notblocked)) {
        blocked_cmd = true;
    }
            //what to do if it is a danger
    if (blocked_cmd) {
        sprintf(str, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:0.00000|avg_time:0.00000|min_time:0.00000|max_time:0.00000>> ",
                success, blocked);
        blocked_cmd = false;
        close(pipefd[0]);
        close(pipefd[1]);
        free(line);
        continue;
    }
            // redirect the errors
            char *stderr_redirect = strstr(line, "2>");
            int redirect_fd = -1;
            if (stderr_redirect) {
                char *filename = stderr_redirect + 2;
                while (*filename == ' ') filename++;
                redirect_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (redirect_fd < 0) {
                    perror("open stderr redirect");
                    free(line);
                    continue;
                }
                dup2(redirect_fd, STDERR_FILENO);
                close(redirect_fd);
                *strstr(line, "2>") = '\0';
            }

    char *arr1[32];
    char *arr2[32];
    parse_command_to_array(FirstCom, arr1);
    parse_command_to_array(SecondCom, arr2);

            int argu1 = countArguments(FirstCom);
            int argu2 = countArguments(SecondCom);
              // 6 arg for two of the commands
            if (argu1 > 6||argu2>6) {
                printf("ERR_ARGS\n");
                free(line);
                continue;
            }
    fflush(stdout);
    gettimeofday(&start, NULL);

   // First command (writes to pipe) c1
    pid_t c1 = fork();
    if (c1 == 0) {

        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(arr1[0], arr1);
        perror("execvp");
        for (int i = 0; arr1[i] != NULL; i++) {
            free(arr1[i]);
        }
        exit(1);
    }

            // Second command (reads from pipe) c2
    pid_t c2 = fork();
    if (c2 == 0) {

        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[1]);
        close(pipefd[0]);
        if (strcmp(arr2[0], "my_tee") == 0) {
            handle_my_tee(arr2);
            close(STDIN_FILENO);
            exit(0);
        }
        execvp(arr2[0], arr2);
        perror("execvp");
        for (int i = 0; arr2[i] != NULL; i++) {
            free(arr2[i]);
        }
        exit(1);


    }

    // the parent of the pipe command
            for (int i = 0; arr1[i] != NULL; i++) {
                free(arr1[i]);
            }
            for (int i = 0; arr2[i] != NULL; i++) {
                free(arr2[i]);
            }
            int status1, status2;
            close(pipefd[0]);
            close(pipefd[1]);

            if (!background) {
                waitpid(c1, &status1, 0);
                waitpid(c2, &status2, 0);

                gettimeofday(&end, NULL);
                handle_timing(line, argv[2], start, end, &success, &avgTime, &avgSuccess, &minTime, &maxTime, str, blocked);
            }
            else {
                sprintf(str, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:0.00000|avg_time:%.5f|min_time:%.5f|max_time:%.5f>> ",
                        success, blocked, avgSuccess, minTime, maxTime);
            }



}

        // regular command
        if (!strchr(line, '|')) {
            int argu = countArguments(line);
            char *arr[32];
            // split the line
            parse_command_to_array(line, arr);
            // if the command was rlimit
            if (strcmp(arr[0], "rlimit") != 0 && argu > 6) {
                printf("ERR_ARGS\n");
                free(line);
                continue;
            }

            // dangerous command
            if (is_dangerous(line, argv[1], &blocked, &notblocked)) {
                sprintf(str, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:0.00000|avg_time:0.00000|min_time:0.00000|max_time:0.00000>> ",
                        success, blocked);
                free(line);
                continue;
            }

            // redirect the errors
            char *stderr_redirect = strstr(line, "2>");
            int redirect_fd = -1;
            if (stderr_redirect) {
                char *filename = stderr_redirect + 2;
                while (*filename == ' ') filename++;
                redirect_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (redirect_fd < 0) {
                    perror("open stderr redirect");
                    free(line);
                    continue;
                }
                dup2(redirect_fd, STDERR_FILENO);
                close(redirect_fd);
                *strstr(line, "2>") = '\0';
            }


            // if the command is rlimit
               struct rlimit lim;
            if (strcmp(arr[0], "rlimit") == 0) {

                if (arr[1] == NULL) {
                    printf("ERR_ARGS\n");
                    for (int i = 0; arr[i] != NULL; i++) free(arr[i]);
                    free(line);
                    continue;
                }

                // if the user set the resouce limit
                if (strcmp(arr[1], "set") == 0) {
                    int exec_index = ResourceLimit(arr, &lim);
                    if (arr[exec_index] != NULL) {
                        gettimeofday(&start, NULL);
                        pid_t pid = fork();

                        //the exec of the command after the set
                        if (pid == 0) {
                            execvp(arr[exec_index], &arr[exec_index]);
                            perror("execvp");
                            for (int i = 0; arr[i] != NULL; i++) {
                                free(arr[i]);
                            }
                            exit(1);
                        }
                        else if (pid > 0) {

                            // parent

                            waitpid(pid, &status, 0);
                            gettimeofday(&end, NULL);

                            if (WIFSIGNALED(status)) {
                                int sig = WTERMSIG(status);
                                print_rlimit_violation_message(sig);
                                handle_timing(line, argv[2], start, end, &success, &avgTime,
                                              &avgSuccess, &minTime, &maxTime, str, blocked);
                            }
                            else if (WIFEXITED(status)) {
                                int exit_code = WEXITSTATUS(status);
                                if (exit_code == 0) {
                                    handle_timing(line, argv[2], start, end, &success, &avgTime,
                                                     &avgSuccess, &minTime, &maxTime, str, blocked);
                                } else if (strstr(line, "fsize=") != NULL) {
                                    printf("File size limit exceeded!\n");
                                    handle_timing(line, argv[2], start, end, &success, &avgTime,
                                                &avgSuccess, &minTime, &maxTime, str, blocked);
                                }
                            }
                            else {
                                sprintf(str, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:0.00000|avg_time:0.00000|min_time:0.00000|max_time:0.00000>> ",
                                        success, blocked);
                            }
                        }
                        else {
                            perror("fork");
                        }
                    }

                    for (int i = 0; arr[i] != NULL; i++) free(arr[i]);
                    free(line);
                    continue;
                }



                //print the cur  the current limits
                else if (strcmp(arr[1], "show") == 0) {
                    gettimeofday(&start, NULL);
                    ShowLimit(arr);
                    gettimeofday(&end, NULL);

                    handle_timing(line, argv[2], start, end, &success, &avgTime, &avgSuccess, &minTime, &maxTime, str, blocked);
                    for (int i = 0; arr[i]!=NULL; ++i) {
                        free(arr[i]);
                    }
                    continue;
                }
            }


            gettimeofday(&start, NULL);
            // if its regular command withour rlimit
            pid_t pid = fork();
            if (pid == -1) {
                    perror("fork");
                for (int i = 0; arr[i] != NULL; i++) {
                    free(arr[i]);
                }
                   exit(0);
            }


            if (pid == 0) {
                execvp(arr[0], arr);
                perror("execvp");
                for (int i = 0; arr[i] != NULL; i++) {
                    free(arr[i]);
                }
                exit(1);
            }
            for (int i = 0; arr[i] != NULL; i++) {
                free(arr[i]);
            }

            if (!background) {
                waitpid(pid, &status, 0);
                gettimeofday(&end, NULL);

                if (WIFSIGNALED(status)) {
                    int sig = WTERMSIG(status);
                    print_rlimit_violation_message(sig);
                }
                else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    handle_timing(line, argv[2], start, end, &success, &avgTime, &avgSuccess, &minTime, &maxTime, str, blocked);
                } else {
                    sprintf(str, "#cmd:%d|#dangerous_cmd_blocked:%d|last_cmd_time:0.00000|avg_time:0.00000|min_time:0.00000|max_time:0.00000>> ",
                            success, blocked);
                }
            }
   }


            free(line);
            line = NULL;

    }

}
