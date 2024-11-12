#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>

typedef int pid_t;

void syscall_init (void);

/** 2
 * shutdown_power_off()를 호출해서 Pintos를 종료한다.
 * shutdown_power_off()는 src/devices/shutdown.c 에 선언되어 있다.
 * 이 함수는 웬만하면 사용되지 않아야 하는데, deadlock 상황에 대한 정보 등 뭔가 누락될 수 있기 때문이다.
 */
void halt(void);

/** 2
 * 현재 프로세스를 종료시키는 시스템 콜이다.
 * 종료시, 프로세스 이름 : 'exit(status)'를 출력한다.
 * 정상적으로 종료시 status는 0이 된다.
 * status : 프로그램이 정상적으로 종료되었는지 확인한다.
 * 관례적으로, status == 0은 성공을 의미하고, 0이 아닌 값들은 에러를 뜻한다.
 */
void exit(int status);

/** 2
 * 자식 프로세스를 생성하고 프로그램을 실행시키는 시스템 콜
 * 프로세스를 생성하는 함수를 이용한다. (command line parsing)
 * 프로세스 생성에 성공시 생성된 프로세스의 pid 값을 반환, 실패시 -1을 반환
 * 부모 프로세스는 자식 프로세스의 응용 프로그램이 메모리에 탑재 될 때 까지 대기한다.
 * semaphore를 이용한다.
 * cmd_line = 새로운 프로세스에 실행할 프로그램 명령어
 * pid_t는 tid_t와 동일한 int 자료형이다.
 */
pid_t exec(const char *cmd_line);

/** 2
 * 현재 process_wait()는 -1을 리턴한다. - init process는 user process가 종료될 때까지 대기하지 않고 핀토스를 종료시킨다.
 * process_wait() 기능은 다음과 같다.
 * 1. 자식 프로세스가 모두 종료될 때까지 대기(sleep state)한다.
 * 2. 자식 프로세스가 올바르게 종료되었는지 확인한다.
 * wait() 시스템 콜을 구현한다.
 * 
 * 자식 프로세스(pid)를 기다려서, 자식의 종료상태(exit_status)를 가져온다.
 * 만약 자식 프로세스가 살아있다면, 종료될 때 까지 기다린다.
 * 종료가 되면 그 프로세스가 exit 함수로 전달해준 상태(exit status)를 반환한다.
 * 만약, 자식 프로세스가 exit() 함수를 호출하지 않고 커널에 의해서 종료된다면(ex, exception에 의해서 죽는 경우),
 * wait(pid)는 -1을 반환해야 한다.
 */
int wait(pid_t pid);

/** 2
 * file을 이름으로 하고, initial_size를 갖는 새로운 파일을 생성한다.
 * 성공적으로 파일이 생성되었다면 true를 반환하고, 실패했다면 false를 반환한다.
 * 새로운 파일을 생성하는 것이 그 파일을 여는 것을 의미하지는 않는다.
 * 파일을 여는 것은 open 시스템 콜의 역할로, create와 개별적인 연산이다.
 */
bool create(const char* file, unsigned int initial_size);

/** 2
 * file을 이름으로 하는 파일을 삭제한다.
 * 성공적으로 삭제했다면 true를 반환하고, 그렇지 않으면 false를 반환한다.
 * 파일은 열려있는지 닫혀있는지 여부와 관계없이 삭제될 수 있고, 파일을 삭제하는 것이 그 파일을 닫았다는 것을 의미하지는 않는다.
 */
bool remove(const char* file);

/** 2
 * 파일을 열 때 사용하는 시스템 콜
 * 파일이 없을 경우 실패한다.
 * 성공시 fd를 반환, 실패시 -1을 반환한다.
 * file == 파일의 이름 및 경로 정보
 */
int open(const char* file);

/** 2
 * 파일의 크기를 알려주는 시스템 콜
 * 성공시 파일의 크기를 반환, 실패시 -1을 반환한다.
 */
int filesize(int fd);

/** 2
 * 열린 파일의 데이터를 읽는 시스템 콜
 * 성공시 읽은 bytes를 반환, 실패시 -1을 반환한다.
 * buffer : 읽은 데이터를 저장할 버퍼의 주소값
 * size : 읽을 데이터 크기
 * fd 값이 0(standard input)이라면, 키보드의 데이터를 읽어 buffer에 저장한다. (input_getc() 사용)
 * 
 * read와 write 함수는 다른 시스템 콜과는 다르게 lock을 활용해야 한다.
 * 파일을 읽는 도중, 또는 파일에 값을 쓰는 과정에서 다른 thread가 파일에 접근하여 값을 바꿔버릴 수 있으므로,
 * 한 파일에는 하나의 thread만 접근할 수 있게 하기 위해 lock을 사용한다.
 * 
 * 1. 파일의 디스크립터 fd,
 * 2. 파일에서 값을 읽어 저장할 buffer,
 * 3. 읽어들일 값의 크기인 size를 받아온다.
 * 
 * read의 경우, standard input에서 값을 읽어올 수 있는데,
 * 이 경우 파일 디스크립터 값은 0이 된다.
 * standard_input에서 값을 읽어오는 경우 input_getc () 함수를 통해 구현할 수 있고,
 * 다른 파일을 열어서 읽는 경우에는 file_read () 함수를 통해 구현할 수 있다.
 */
int read(int fd, void *buffer, unsigned int size);

/** 2
 * 열린 파일의 데이터를 기록하는 시스템 콜
 * 성공시 기록한 데이터의 바이트 수를 반환, 실패시 -1을 반환
 * buffer : 기록할 데이터를 저장한 버퍼의 주소 값
 * size : 기록할 데이터 크기
 * fd 값이 1(standard output)일 때, 버퍼에 저장된 데이터를 화면에 출력한다. (putbuf () 이용)
 * 
 * read와 마찬가지로 lock을 사용해서 구현해야한다.
 * 값을 쓸 파일의 디스크립터 fd, 쓸 값이 들어있는 buffer, 쓸 값의 크기인 size를 파라미터로 받는다.
 * write도 read와 비슷하게 standard output에 값을 쓸 수 있다.
 * 이 때 파일 디스크립터 값은 1이며, 콘솔에 값을 쓰는 경우에 해당한다.
 * standard_output에 값을 쓰는 경우 putbuf () 함수를 통해 구현할 수 있다.
 * 다른 파일에 값을 쓰는 경우에는 file_write () 함수를 통해 구현할 수 있다.
 */
int write(int fd, const void* buffer, unsigned int size);

/** 2
 * 열린 파일의 위치(offset)를 이동하는 시스템 콜
 * open file fd에서 읽거나 쓸 다음 바이트를 position으로 변경한다.
 * position은 파일 시작부터 byte 단위로 표시된다. 따라서, position == 0이라면, 파일의 시작을 의미한다.
 */
void seek(int fd, unsigned int position);

/** 2
 * 열린 파일의 위치(offset)를 알려주는 시스템 콜
 * 성공시 파일의 위치(시작지점인 0에서부터의 거리인 offset)를 반환, 실패시 -1을 반환한다.
 */
unsigned int tell(int fd);

/** 2
 * 열린 파일을 닫는 시스템 콜
 * 파일을 닫고 file descriptor를 제거한다.
 */
void close(int fd);

/** 2
 * 주소 값이 user 영역에서 사용하는 주소 값인지 확인한다.
 * user 영역을 벗어난 영역일 경우, process를 종료한다. (exit (-1))
 * pintos에서는 시스템 콜이 접근할 수 있는 주소를 0cx0000000 ~ 0x8048000(== KERN_BASE) 으로 제한한다. (이 이상은 커널 영역이다.)
 * 유저 영역을 벗어난 영역일 경우, 비정상 접근이라고 판단하여 exit(-1)로 프로세스를 종료한다.
 */
void check_address(void* vaddr);

#endif /* userprog/syscall.h */
