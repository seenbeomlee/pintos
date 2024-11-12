#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

/** 2
 * process_exec ()에서 불러온 argument_stack () 함수를 구현하기 위해 선언부터 해준다.
 * char **argv로 받은 문자열 배열과 int argc로 받은 인자 개수를 처리한다.
 */
void parse_filename(char *src, char *dest);
void setting_esp(char* file_name, void** esp);
int parse_argc(char* file_name);
void parse_argv(char** argv, int argc, char* file_name);
void free_argv(char** argv, int argc);

void process_file_close(int fd_idx);

#endif /* userprog/process.h */
