#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "capture.h"
#include "vcompress.h"
#include "sender.h"
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <signal.h>
/** webcam_server: 打开 /dev/video0, 获取图像, 压缩, 发送到 localhost:3020 端口
 *
 * 	使用 640x480, fps=10
 */
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480
#define VIDEO_FPS 10.0

#define TARGET_IP "127.0.0.1"
#define TARGET_PORT 3020
#define REV_CTRL_PORT 3010

#define	TIME_MEM_KEY	99			/* like a filename      */
#define	TIME_SEM_KEY	9901
#define	SEG_SIZE	((size_t)VIDEO_WIDTH*VIDEO_HEIGHT*3*sizeof(int)*2)		/* size of segment	*/
#define oops(m,x)  { perror(m); exit(x); }
union semun { int val ; struct semid_ds *buf ; ushort *array; };
int	seg_id, semset_id;

void wait_and_lock( int semset_id );
void release_lock( int semset_id );
void cleanup(int n);

/*
 * build and execute a 2-element action set:
 *    wait for 0 on n_readers AND increment n_writers
 */
void wait_and_lock( int semset_id )
{
	struct sembuf actions[2];	/* action set		*/

	actions[0].sem_num = 0;		/* sem[0] is n_readers	*/
	actions[0].sem_flg = SEM_UNDO;	/* auto cleanup		*/
	actions[0].sem_op  = 0 ;	/* wait til no readers	*/

	actions[1].sem_num = 1;		/* sem[1] is n_writers	*/
	actions[1].sem_flg = SEM_UNDO;	/* auto cleanup		*/
	actions[1].sem_op  = +1 ;	/* incr num writers	*/

	if ( semop( semset_id, actions, 2) == -1 )
		oops("semop: locking", 10);
}

/*
 * build and execute a 1-element action set:
 *    decrement num_writers
 */
void release_lock( int semset_id )
{
	struct sembuf actions[1];	/* action set		*/

	actions[0].sem_num = 1;		/* sem[0] is n_writerS	*/
	actions[0].sem_flg = SEM_UNDO;	/* auto cleanup		*/
	actions[0].sem_op  = -1 ;	/* decr writer count	*/

	if ( semop( semset_id, actions, 1) == -1 )
		oops("semop: unlocking", 10);
}

/* initialize a semaphore*/
void set_sem_value(int semset_id, int semnum, int val)
{
	union semun  initval;

	initval.val = val;
	if ( semctl(semset_id, semnum, SETVAL, initval) == -1 )
		oops("semctl", 4);
}

void cleanup(int n)
{
	shmctl( seg_id, IPC_RMID, NULL );	/* rm shrd mem	*/
	semctl( semset_id, 0, IPC_RMID, NULL);	/* rm sem set	*/
}

int main (int argc, char **argv)
{
	void *capture = capture_open("/dev/video0", VIDEO_WIDTH, VIDEO_HEIGHT, PIX_FMT_YUV420P);
	if (!capture) {
		fprintf(stderr, "ERR: can't open '/dev/video0'\n");
		exit(-1);
	}

	void *encoder = vc_open(VIDEO_WIDTH, VIDEO_HEIGHT, VIDEO_FPS);
	if (!encoder) {
		fprintf(stderr, "ERR: can't open x264 encoder\n");
		exit(-1);
	}

	void *sender = sender_open(TARGET_IP, TARGET_PORT);
	if (!sender) {
		fprintf(stderr, "ERR: can't open sender for %s:%d\n", TARGET_IP, TARGET_PORT);
		exit(-1);
	}
    //打开接收端口
	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		fprintf(stderr, "ERR: create sock err\n");
		exit(-1);
	}
	sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(REV_CTRL_PORT);
	sin.sin_addr.s_addr = inet_addr(TARGET_IP);
	if (bind(sock, (sockaddr*)&sin, sizeof(sin)) < 0) {
		fprintf(stderr, "ERR: bind %d\n", REV_CTRL_PORT);
		exit(-1);
	}
	unsigned char *buf = (unsigned char*)alloca(65536);
	if (!buf) {
		fprintf(stderr, "ERR: alloca 65536 err\n");
		exit(-1);
	}

	int tosleep = 1000000 / VIDEO_FPS;
	void *mem_ptr;
	/* create a shared memory segment */
	seg_id = shmget( TIME_MEM_KEY, SEG_SIZE, IPC_CREAT|0777 );
	if ( seg_id == -1 )
		oops("shmget", 1);
	/* attach to it and get a pointer to where it attaches */
	mem_ptr = shmat( seg_id, NULL, 0 );
	if ( mem_ptr == ( void *) -1 )
		oops("shmat", 2);
	/* create a semset: key 9900, 2 semaphores, and mode rw-rw-rw */
	semset_id = semget( TIME_SEM_KEY, 2, (0666|IPC_CREAT|IPC_EXCL) );
	if ( semset_id == -1 )
		oops("semget", 3);

	set_sem_value( semset_id, 0, 0);	/* set counters	*/
	set_sem_value( semset_id, 1, 0);	/* both to zero */

	signal(SIGINT, cleanup);

	for (; ; ) {
		// 抓
		Picture pic , picRGB;
		capture_get_picture(capture, &pic , &picRGB);

		//进程间共享内存
		void *mem_ptr_temp = mem_ptr;
		wait_and_lock(semset_id);	/* lock memory	*/
		printf("\tshm_ts2 updating memory\n");
	    for (int i = 0; i < 4; i++)
	    {
	    	memcpy((int*)mem_ptr_temp,&picRGB.stride[i],sizeof(int));
	    	mem_ptr_temp = (int *)mem_ptr_temp + sizeof(int);
	    	for (int j = 0; j < VIDEO_HEIGHT*picRGB.stride[i] ; j++)
	        {
	            memcpy((unsigned char*)mem_ptr_temp,&picRGB.data[i][j],sizeof(unsigned char));
	            mem_ptr_temp = (unsigned char*)mem_ptr_temp + sizeof(unsigned char);
	            if(j<100)
	            {
	            	printf("%d\t%u\n",j,*(unsigned char*)mem_ptr_temp);
	            }
	        }
	    }
		release_lock(semset_id);	/* unlock	*/
		printf("\tshm_ts2 released lock\n");
		// 压
		const void *outdata;
		int outlen;
		int rc = vc_compress(encoder, pic.data, pic.stride, &outdata, &outlen);
		if (rc < 0) continue;
		
		// 发
		sender_send(sender, outdata, outlen);

		// 等
		usleep(tosleep);

	}
	cleanup(0);
	sender_close(sender);
	vc_close(encoder);
	capture_close(capture);

	return 0;
}

