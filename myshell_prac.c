#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


volatile sig_atomic_t t_should_exit = 0;
volatile sig_atomic_t t_sigchld = 0;

struct SNode {
    char *token;
    struct SNode *next;
};

struct StrLst{
    struct SNode *head;
    struct SNode *tail;
    size_t len;
};

void sl_init(struct StrLst *v){
	(*v).head = NULL;
	(*v).tail = NULL;
	(*v).len = 0;
};


void sl_push(struct StrLst *v, char *ch){
	if(!ch) return;
	struct SNode *n = malloc(sizeof(struct SNode));
	if(!n) { perror("malloc"); exit(1); }
	(*n).token = malloc(strlen(ch) + 1);
	if(!(*n).token) { perror("malloc"); exit(1); }
	strcpy((*n).token, ch);
	free(ch);
	(*n).next = NULL;
	if((*v).head == NULL){
		(*v).head = n;
		(*v).tail = n;
		(*v).len = 1;
	}
	else{
		(*(*v).tail).next = n;
		(*v).tail = n;
		(*v).len++;
	}
}


void sl_free(struct StrLst *v){
	if(!(*v).head) {return;}
	while((*v).head != NULL){
		struct SNode *n = (*v).head;
		(*v).head = (*(*v).head).next;
		free((*n).token);
		free(n);
	}
	(*v).tail = NULL;
	(*v).len = 0;
}


typedef struct DynBuf {
    char *buf;
    size_t len;
    size_t cap;
}d;



void db_init(struct DynBuf *d){
	(*d).buf = malloc(32);
	if (!(*d).buf) { perror("malloc"); exit(1); }
	(*d).buf[0] = 0;
	(*d).len = 0;
	(*d).cap = 32;
}

void db_pushc(struct DynBuf *d, char c){
	if((*d).len + 2 > (*d).cap){
		size_t ncap = (*d).cap * 2;
		char *nd = realloc((*d).buf, ncap);
		if (!nd){ perror("realloc"); exit(1);}
		(*d).buf = nd;
		(*d).cap = ncap;
	}
	(*d).buf[(*d).len] = c;
	(*d).len++;
	(*d).buf[(*d).len] = 0; 
}

void db_pushs(struct DynBuf *d, const char *c){
	while (*c){
		db_pushc(d, *c);
		c++;
	}
}

char *db_take(struct DynBuf *d){
    char *r = (*d).buf;
    (*d).buf = NULL;
    (*d).len = 0;
    (*d).cap = 0;
    return r;
}

void db_free(struct DynBuf *d) {
    free((*d).buf);
    (*d).buf = NULL;
    (*d).len = 0;
    (*d).cap = 0;
}
 
int is_special_char(char c) {
    return c == '|' || c == '<' || c == '>' || c == '&' || c == '(' || c == ')';
}



char *expand_vars(char *s){
	struct DynBuf res;
	db_init(&res);
	
	size_t i = 0;
	while(s[i]){
		if(s[i] != '$'){ db_pushc(&res, s[i]); i++;}
		else{
			i++;
			struct DynBuf name;
			db_init(&name);
			int flag = 0;
			if (s[i] == '{') {i++; flag = 1;}
			while(s[i] && (isalnum((unsigned char)s[i]) || s[i] == '_')){
				db_pushc(&name, s[i]);
				i++;
			}
			
			if(flag){
				if(s[i] == '}'){i++;}
				else{
					db_pushc(&res, '{');
					if (name.buf) {
						db_pushs(&res, name.buf);
					} 
					else {
						db_pushs(&res, "");
					}
					db_free(&name);
					continue;
				}
			}
			char *val = getenv(name.buf);
			if (val) db_pushs(&res, val);
			db_free(&name);	
		}
    }
    return db_take(&res);
}
			
	
int tokenize(const char *line, struct StrLst *tokens){
    sl_init(tokens);
    size_t i = 0;
	
    while(line[i]){
        while(line[i] && isspace((unsigned char)line[i])) i++;
        if(!line[i]) break;

        if ((line[i] == '&' && line[i+1] == '&') ||
            (line[i] == '|' && line[i+1] == '|') ||
            (line[i] == '>' && line[i+1] == '>')) {
            char *op = malloc(3*sizeof(char));
            op[0] = line[i]; op[1] = line[i+1]; op[2] = 0;
            i += 2;
            sl_push(tokens, op);
            continue;
        }

        if(is_special_char(line[i])){
            char *op = malloc(2*sizeof(char));
            op[0] = line[i]; op[1] = 0;
            i++;
            sl_push(tokens, op);
            continue;
        }

        struct DynBuf d;
        db_init(&d);
        int quote_error = 0;

        while (line[i]){
            char c = line[i];
            if(c == '"'){
                i++;
                while(line[i] && line[i] != '"'){ db_pushc(&d, line[i]); i++; }
                if (line[i] == '"'){
                    i++;
                } else {
                    quote_error = 1;
                    break;
                }
            }
            else if(is_special_char(line[i]) || isspace((unsigned char)line[i])) break;
            else { db_pushc(&d, c); i++; }
        }

        if (quote_error) {
            db_free(&d);
            sl_free(tokens);
            fprintf(stderr, "shell: syntax error: unmatched '\"'\n");
            return -1;
        }

        char *word = db_take(&d);
        char *new_word = expand_vars(word);
        free(word);
        sl_push(tokens, new_word);
    }
    return 0;
}


char *find_in_path(char *s){
	if (strchr(s, '/')){
		if(access(s, X_OK) == 0) return strdup(s);
		return NULL;
	}
	
	char *path = getenv("PATH");
	if (!path) return NULL;
	size_t len = strlen(path);
	char *copy = malloc(len + 1);
	if (!copy) {perror("malloc"); exit(1); }
	memcpy(copy, path, len + 1);
	
	char *save = NULL;
	char *res = NULL;
	char *dir = strtok_r(copy, ":", &save);
	
	while(dir != NULL){
		size_t tmp_len = strlen(dir) + strlen(s) + 2;  // 0 + \ для склеенного пути
		char *temp = malloc(tmp_len);
		if (!temp) {perror("malloc"); exit(1);}
		strlcpy(temp, dir, tmp_len);
		strlcat(temp, "/", tmp_len); // чтоб не стирать всю строку а записывать в конец
		strlcat(temp, s, tmp_len);
		if(access(temp, X_OK) == 0) {res = temp; break;}
		free(temp);
		dir = strtok_r(NULL, ":", &save);
	}
	free(copy);
	return res;
}


int base_command(struct StrLst *v, int *a){   
	if((*v).head == NULL) return 0;
	char *name = ((*(*v).head).token);
	
	if (strcmp(name, "exit") == 0){t_should_exit = 1; return 1; }; // если сделаем exit то потерянная память
	
	if(strcmp(name, "cd") == 0){
		char *dir = NULL;
		if((*(*v).head).next != NULL){
			dir = (*(*(*v).head).next).token;
		}
		else{
			dir = getenv("HOME");
			if (!dir){ fprintf(stderr, "cd: HOME not set\n"); *a = 1; return 1; }
		}
		if (chdir(dir) != 0) {perror("cd"); *a = 1; return 1;} //системный вызов меняет текущий рабочий каталог
		*a = 0;
		return 1;
	}
	
	if(strcmp(name, "pwd") == 0){
		char buf[255];
		if(getcwd(buf, sizeof(buf)) != NULL) {printf("%s\n", buf); *a = 0;} //абсолютный путь текущего каталога
		else{ perror("pwd"); *a = 1;}
		return 1;
	}
	return 0;
}


void sigchld_handler(int sig){ (void)sig; t_sigchld = 1; }


void children_wait(void){
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){ //ждем любого ребенка но не блокируемся
        if (WIFEXITED(status)) printf("\n[%d] done, status %d\n", (int)pid, WEXITSTATUS(status)); //норм завершение
        else if (WIFSIGNALED(status)) printf("\n[%d] killed by signal %d\n", (int)pid, WTERMSIG(status)); //убит сигналом
        fflush(stdout); // чтоб в буфере не застряло
    }
}


struct Redir{
	char *in_file;
	char *out_file;
	int append; // для >> 
};

struct Command{
	struct StrLst argv;
	struct Redir redir;
};

struct Pipeline{ // конвейер 
	struct Command *commands;
	size_t n;
};

struct Task{
	struct Pipeline pl;
	int flag;  // &
};

struct ParsedLine{
	struct Task *tasks; // массив заданий
	size_t n; // сколько
	char *ops; // операторы между заданиями 
};

void command_init(struct Command *c){
	sl_init(&((*c).argv));
	(*c).redir.in_file = NULL;
	(*c).redir.out_file = NULL;
	(*c).redir.append = 0;
}

void command_free(struct Command *c){
	sl_free(&((*c).argv));
	free((*c).redir.in_file);
	free((*c).redir.out_file);
	(*c).redir.in_file  = NULL;
    (*c).redir.out_file = NULL;
    (*c).redir.append   = 0;
};

void pipeline_init(struct Pipeline *p){
	(*p).commands = NULL;
	(*p).n = 0;
}

void pipeline_free(struct Pipeline *p){
	if(!(*p).commands) return;
	for(size_t i = 0; i < (*p).n; i++) command_free(&(*p).commands[i]);
	free((*p).commands);
	(*p).commands = NULL;
	(*p).n = 0;
}

void parsedline_init(struct ParsedLine *pl){
	(*pl).tasks = NULL;
	(*pl).n = 0;
	(*pl).ops = NULL;
}

void parsedline_free(struct ParsedLine *pl){
	if (!(*pl).tasks){
		free((*pl).ops);
		(*pl).ops = NULL;
		(*pl).n = 0;
		return;
	}
	for(size_t i = 0; i < (*pl).n; i++){pipeline_free(&(*pl).tasks[i].pl);}
	free((*pl).ops);
	free((*pl).tasks);
	(*pl).tasks = NULL;
	(*pl).n = 0;
	(*pl).ops = NULL;
}

struct Task *parsedline_add_task(struct ParsedLine *pl, char op_before){
	// realloc tasks n+1
	struct Task *n = realloc((*pl).tasks, ((*pl).n + 1)*sizeof(struct Task));
	if (!n){ perror("realloc"); exit(1);}
	(*pl).tasks = n;
	// realloc operators n+1
	char *n_op = realloc((*pl).ops, (*pl).n + 1);
	if(!n_op) {perror("realloc");  exit(1);}
	(*pl).ops = n_op;
	// init task + op [n]
	struct Task *j = &(*pl).tasks[(*pl).n];
	pipeline_init(&(*j).pl);
	(*j).flag = 0;
	(*pl).ops[(*pl).n] = op_before;
	(*pl).n++;
	return j;
}


struct Command *pipeline_add_command(struct Pipeline *p){
	struct Command *n = realloc((*p).commands, ((*p).n + 1)*sizeof(struct Command));
	if (!n) { perror("realloc"); exit(1);}
	(*p).commands = n;
	struct Command *c = &(*p).commands[(*p).n];
	command_init(c);
	(*p).n++;
	return c;
}


int parse_one_command(struct SNode **cursor, struct Command *c){
	struct SNode *cur = *cursor;
	
	while(cur != NULL){
		char *tok = (*cur).token;
		if (strcmp(tok, "|") == 0 || strcmp(tok, ";") == 0 ||
            strcmp(tok, "&") == 0 || strcmp(tok, "&&") == 0 ||
            strcmp(tok, "||") == 0){
            break;
        }
        
        if (strcmp("<", tok) == 0){
			cur = (*cur).next;
			if(cur == NULL){
				fprintf(stderr, "shell: syntax error near '<'\n");
                return -1;
            }
            free((*c).redir.in_file);
            (*c).redir.in_file = strdup((*cur).token);
            if (!(*c).redir.in_file) {perror("strdup"); exit(1);}
            cur = (*cur).next;
		}
		else if (strcmp(">", tok) == 0 || strcmp(">>", tok) == 0){
			int app = (strcmp(">>", tok) == 0);
			cur = (*cur).next;
			if (cur == NULL){
				fprintf(stderr, "shell: syntax error near '>'\n");
				return -1;
			}
			free((*c).redir.out_file);
			(*c).redir.out_file = strdup((*cur).token);
			if (!(*c).redir.out_file) {perror("strdup"); exit(1);}
			(*c).redir.append = app;
			cur = (*cur).next;
		}
		else{
			char *arg = strdup(tok);
			if(!arg) {perror("strdup"); exit(1);}
			sl_push(&(*c).argv, arg);
			cur = (*cur).next;
		}
	}
	if ((*c).argv.len == 0 && (*c).redir.in_file == NULL && (*c).redir.out_file == NULL){
        fprintf(stderr, "shell: empty command\n");
        return -1;
    }

    *cursor = cur;
    return 0;
}


int build_parsedline(struct StrLst *tokens, struct ParsedLine *pl){
	parsedline_init(pl);
	
	struct SNode *cur = (*tokens).head;
	char end_op = 0;
	
	while(cur != NULL){
		while(cur != NULL){
			char *tok = (*cur).token;
			if (strcmp("||", tok) == 0) {end_op = '|'; cur = (*cur).next;}
			else if(strcmp("&&", tok) == 0) {end_op = '&'; cur = (*cur).next;}
			else break;
		}
		if (cur == NULL) break;
		
		struct Task *j = parsedline_add_task(pl, end_op);
		end_op = 0;
		
		while(1){
			struct Command *c = pipeline_add_command(&(*j).pl);
			if(parse_one_command(&cur, c) != 0){ // обработка одной команды и загрузка ее аргументов
				parsedline_free(pl);
                return -1;
            }
            if(cur != NULL && strcmp((*cur).token, "|") == 0){ // если тот же пайплайн
				cur = (*cur).next;
				continue;
			}
			break;
		}
		if (cur != NULL && strcmp((*cur).token, "&") == 0){
			(*j).flag = 1;
			cur = (*cur).next;
			if (cur != NULL){
				char *n = (*cur).token;
				if ( strcmp(n, "||") != 0 && strcmp(n, "&&") != 0){
					fprintf(stderr, "shell: syntax error near '&'\n");
                    parsedline_free(pl);
                    return -1;
				}
			}
		}
	}
	if ((*pl).n == 0){
        fprintf(stderr, "shell: empty line\n");
        return -1;
    }
    return 0;
}

int run_pipeline(struct Pipeline *p, int flag){
	size_t n = (*p).n;
	if (n == 0) return 0;
	
	if (n == 1){
        int a = 0;
        if (base_command(&(*p).commands[0].argv, &a)) return a;
    }
    pid_t *pids = malloc( n * sizeof(pid_t));
    if (!pids){ perror("malloc"); exit(-1);}
    
    int prev_read = -1; // тк на каждой итерации новый пайп нужно хранить вход для след команды пайплайна
    for(size_t i = 0; i < n; i++){
		struct Command *c = &(*p).commands[i];
		int pipefd[2];
		pipefd[0] = -1;
		pipefd[1] = -1; // контроль последней команды в пайплайне
		if (i+1 < n){
			if(pipe(pipefd) != 0) {perror("pipe"); free(pids); exit(1);}
		}
		pid_t pid = fork();
		if (pid < 0) { perror("fork"); free(pids); exit(1); }
		
		if(pid == 0){
			if (prev_read != -1){ dup2(prev_read, 0); close(prev_read);} // stdin ребенка это prev_read прошлой команды
			if(pipefd[1] != -1){ dup2(pipefd[1], 1); close(pipefd[1]);} // на запись
			if(pipefd[0] != -1){ close(pipefd[0]);} // не нужен 
			
			if((*c).redir.in_file){
				int fd = open((*c).redir.in_file, O_RDONLY);
				if (fd < 0){ perror("open"); _exit(1);}
				dup2(fd, 0);
				close(fd);
			}
			if ((*c).redir.out_file){
				int flags = O_WRONLY | O_CREAT;
				if ((*c).redir.append) {flags |= O_APPEND;}
				else {flags |= O_TRUNC;}
                int fd = open((*c).redir.out_file, flags, 0644);
                if (fd < 0){ perror("open"); _exit(1); }
                dup2(fd, 1); 
                close(fd);
            }
            
            size_t argc = (*c).argv.len;
            if (argc == 0){ _exit(0);}
            char **args = malloc((argc + 1) * sizeof(char *));
            if(!args) {perror("malloc"); _exit(1);}
            size_t k = 0;
            struct SNode *cur = (*c).argv.head;
            while(cur != NULL){args[k++] = (*cur).token; cur = (*cur).next;}
            args[k] = NULL;
            
            char *path = find_in_path(args[0]);
            if (!path){
                fprintf(stderr, "shell: command not found: %s\n", args[0]);
                free(args);
                _exit(127);
            }
            execv(path, args);
            perror("execv");
            free(path);
            free(args);
            _exit(126);
        }
        pids[i] = pid;


        if (prev_read != -1) close(prev_read); //гигиена
        if (pipefd[0] != -1){ prev_read = pipefd[0]; close(pipefd[1]); } // запоминаем в prev_read выход текущей команды 
		// для входа следующей
        else prev_read = -1;
    }

    if (flag){
        printf("[%d] %s\n", (int)pids[0], (*(*p).commands[0].argv.head).token);
        fflush(stdout);
        free(pids);
        return 0;
    }

    int last_code = 0;
    for (size_t i = 0; i < n; i++){
        int st = 0;
        waitpid(pids[i], &st, 0);
        if (i + 1 == n){
            if (WIFEXITED(st)) last_code = WEXITSTATUS(st);
            else if (WIFSIGNALED(st)) last_code = 128 + WTERMSIG(st);
        }
    }
    free(pids);
    return last_code;
}


int main(){
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
    	perror("signal");
    	exit(1);
	}

    char  *line = NULL;
    size_t cap  = 0;

    for (;;){
        if (t_sigchld){ t_sigchld = 0; children_wait(); }

        printf("myshell> ");
        fflush(stdout);

        ssize_t r = getline(&line, &cap, stdin);
        if (r < 0){ printf("\n"); break;}
        if (r > 0 && line[r-1] == '\n') line[r-1] = 0;

        struct StrLst tokens;
        if (tokenize(line, &tokens) != 0) continue;

        if ((tokens).len == 0){ sl_free(&tokens); continue; }

        struct ParsedLine pl;
        if (build_parsedline(&tokens, &pl) != 0){
            sl_free(&tokens);
            continue;
        }

        int last_rc = 0;
        for (size_t i = 0; i < pl.n; i++){
            char op = pl.ops[i];

            if (i > 0){
                if (op == '&' && last_rc != 0) continue;
                if (op == '|' && last_rc == 0) continue;
            }

            last_rc = run_pipeline(&pl.tasks[i].pl, pl.tasks[i].flag);
        }

        parsedline_free(&pl);
        sl_free(&tokens);
		if (t_should_exit) break;
    }
    free(line);
    return 0;
}