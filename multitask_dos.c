#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#define GET_INDOS 0x34
#define GET_CRIT_ERR 0x5d06
/* define tcb state-constant */
/* null     0       not assigned*/
#define FINISHED     0
#define RUNNING     1
#define READY       2
#define     BLOCKED     3
char far *indos_ptr=0;
char far *crit_err_ptr=0;
int current;
int timecount = 0;
#define NTCB 7 /*number of TCB*/
#define NBUF 10 /*size of buffer*/
#define NTEXT 1024
#define TL 2
/*structure*/
struct buffer{
    int sender;
    int size;
    char text[NTEXT];
    struct buffer *next;
}*freebuf;

typedef struct {
    int value;
    struct TCB *wq;
}semaphore;

semaphore mutex = {1, NULL};
semaphore empty = {NBUF, NULL};
semaphore full = {0, NULL};
semaphore mutexfb = {1,NULL};
semaphore sfb = {NBUF,NULL};
int get_pc_buffer = 0;

struct TCB{
    unsigned char *stack;
    unsigned ss;
    unsigned sp;
    char state;
    char name[10];
    struct TCB *next;
    struct buffer *mq;
    semaphore mutex;
    semaphore sm;
}tcb[NTCB];
struct int_regs{
    unsigned bp,di,si,ds,es,dx,cx,bx,ax,ip,cs,flags,off,seg;
};

/*def*/
void InitDos(void);
int DosBusy(void);
typedef int(far *codeptr)(void);
void InitTcb(void);           /*initialize the tcb*/
int create(char *name,codeptr code,int stck);
void destroy (int id);
void over(void);
int find(void);               /*find the next thread*/
void interrupt my_swtch(void);   /*switch to another thread*/
void interrupt(*old_int8)(void);
void interrupt new_int8(void);
void tcb_state(void);
int finished(void);        /*check whether all thread is finished*/
void block(struct TCB **p);
void wakeup(struct TCB **p);
void p(semaphore *sem);
void v(semaphore *sem);
void InitBuf(void);
struct buffer *getbuf(void);
void insert(struct buffer **mq,struct buffer *buff);
void send(char *receiver,char *a,int size);
struct buffer *remov(struct buffer **mq,int sender);
int receiver(char *sender,char *b);
void snd_msg();
void rcv_msg();
void f1(void);
void f2(void);
void producer(void);
void consumer(void);
/*function*/
void InitInDos(void){
    union REGS regs;
    struct SREGS segregs;
    regs.h.ah=GET_INDOS;
    intdosx(&regs,&regs,&segregs),
    indos_ptr=MK_FP(segregs.es,regs.x.bx);      /*MK_FP() get real address*/
    if(_osmajor<3)
        crit_err_ptr=indos_ptr+1;
    else if(_osmajor==3 && _osminor==0)
        crit_err_ptr=indos_ptr-1;
    else{
        regs.x.ax=GET_CRIT_ERR,
        intdosx(&regs,&regs,&segregs);
        crit_err_ptr=MK_FP(segregs.ds,regs.x.si);
    }
}
int DosBusy(void){
    if(indos_ptr && crit_err_ptr)
        return(*indos_ptr || *crit_err_ptr);
    else
        return -1;
}
void InitTcb(void)
{
    int i;
    for (i = 0; i < NTCB; ++i) {
        tcb[i].stack = NULL;
        tcb[i].state = FINISHED;
        tcb[i].name[0] = '\0';
        tcb[i].next = NULL;
        tcb[i].mq = NULL;
        tcb[i].mutex.value = 1;
        tcb[i].sm.value = 0;
    }
}
int create(char *name,codeptr code,int stck){
    int i;
    struct int_regs *regs_pt;
    for(i=0;i<NTCB;i++){
        if(tcb[i].state == FINISHED)
            break;
    }
    tcb[i].stack = (char* )malloc(sizeof(char)*stck);
    regs_pt = (struct int_regs *)( tcb[i].stack + stck)-1;
    regs_pt->cs=FP_SEG(code);
    regs_pt->ip=FP_OFF(code);
    regs_pt->flags=0x200;
    regs_pt->ds=_DS;
    regs_pt->es=_DS;
    regs_pt->seg=FP_SEG(over);
    regs_pt->off=FP_OFF(over);

    strcpy(tcb[i].name,name);
    tcb[i].state=READY;
    tcb[i].ss = FP_SEG(regs_pt);
    tcb[i].sp = FP_OFF(regs_pt);
    printf("The thread %s has been created!\n", tcb[i].name);
    return i;
}
void destroy(int id){
    if(tcb[id].state == FINISHED) return;
    disable();
    free(tcb[id].stack);
    tcb[id].stack = NULL;
    tcb[id].state = FINISHED;
}
void over(){
    destroy(current);
    my_swtch();
    enable();
}
int find()
{
    int i;
    for (i = current + 1; i != current; i++) {
        if (i == NTCB)
            i = 0;
        if (tcb[i].state == READY) {
            break;
        }
    }
    return i;
}
void interrupt my_swtch(void){
    int i;
    disable();
    tcb[current].ss = _SS;
    tcb[current].sp = _SP;
    if(tcb[current].state == RUNNING)
        tcb[current].state = READY;
    i = find();
    _SS = tcb[i].ss;
    _SP = tcb[i].sp;
    tcb[i].state = RUNNING;
    current = i;
    timecount = 0;
    enable();
}
void interrupt new_int8(void){
    (*old_int8)();
    timecount++;
    if(timecount>=TL){

        if(!DosBusy()){
            my_swtch();
        }
    }
}
void tcb_state(void){
    int i;
    for(i=0;i<NTCB;i++){
        switch(tcb[i].state){
            case 0:
                printf("The state of tcb[%d](%s) is finished\n",i,tcb[i].name);
                break;
            case 1:
                printf("The state of tcb[%d](%s) is running\n",i,tcb[i].name);
                break;
            case 2:
                printf("The state of tcb[%d](%s) is ready\n",i,tcb[i].name);
                break;
            case 3:
                printf("The state of tcb[%d](%s) is blocked\n",i,tcb[i].name);
                break;

        }
    }
}
int finished(){
    int i;
    for(i=1;i<NTCB;i++){
        if(tcb[i].state != FINISHED ){
            return 0;
        }
        return 1;
    }
}
void block(struct TCB **p){
    struct TCB *t;
    disable();
    tcb[current].state = BLOCKED;
    t = *p;
    if(t == NULL){
        t = &tcb[current];
    }
    else{
        while(t->next!=NULL){
            t = t->next;
        }
        t->next = &tcb[current];
    }
    enable();
    my_swtch();
}
void wakeup(struct TCB **p){
    struct TCB *t;
    disable();
    t = *p;
    if(t != NULL){
    t->state = READY;
}
    (*p)=(*p)->next;
    t->next = NULL;
    enable();
}
void p(semaphore *sem){
    struct TCB **qp;
    disable();
    (sem->value) --;
    if(sem->value<0){
        qp=&(sem->wq);
        block(qp);
    }
    enable();
}
void v(semaphore *sem){
    struct TCB **qp;
    disable();
    qp=&(sem->wq);
    (sem->value)++;
    if(sem->value<=0)
        wakeup(qp);
    enable();
}
void InitBuf(void){
    int i;
    for(i=1;i<NBUF;i++){
        freebuf=(struct buffer*)malloc(sizeof(struct buffer));
        freebuf=freebuf->next;
    }
    freebuf = (struct buffer*)malloc(sizeof(struct buffer));
    freebuf->next = NULL;
}
struct buffer *getbuf(void){
    struct buffer *buff;
    buff = freebuf;
    freebuf = freebuf -> next;
    return(buff);
}
void insert(struct buffer **mq,struct buffer *buff){
    struct buffer *temp;
    if(buff == NULL) return;
    buff->next =NULL;
    if(*mq==NULL)
        *mq = buff;
    else{
        temp = *mq;
        while(temp->next!=NULL)
            temp = temp->next;
        temp->next = buff;
    }
}
void send(char *receiver,char *a,int size){
    struct buffer *buff;
    int i,id=-1;
    disable();
    for(i=0;i<NTCB;i++){
    /*如果接收者线程不存在，则不发送，立即返回*/
        if(strcmp(receiver,tcb[i].name) == 0){
            id = i;
            break;
        }
    }
    if(id==-1){
        printf("Error:Receiver not exist!\n");
        enable();
        return;
    }
/*获取空闲消息缓冲区*/
    p(&sfb);
    p(&mutexfb);
    buff=getbuf();
    v(&mutexfb);
/*填写消息缓冲区各项信息*/
    buff->sender=current;
    buff->size=size;
    buff->next=NULL;
    for(i=0;i<buff->size;i++,a++){
        buff->text[i]=*a;
    }
/*将消息缓冲区插入到接收者线程的消息队列末尾*/
    p(&tcb[id].mutex);
    insert(&(tcb[id].mq),buff);
    v(&tcb[id].mutex);
    v(&tcb[id].sm);
    enable();
}
struct buffer *remov(struct buffer **mq,int sender){
    struct buffer *temp,*s;
    if(*mq == NULL) return;
    else{
        temp = *mq;
        if(temp->sender==sender){
            s = temp;
            temp = temp->next;
        }
        else{
            while(temp->next->sender != sender){
                temp = temp->next;
                s = temp->next;
                temp->next = s->next;
            }
        }
    return s;
    }
}
int receive(char *sender, char *b)
{
        int i = 0, id = -1, size;
        struct buffer *get_buffer = NULL;

        disable();

        /*find the sender TCB*/
        for (i = 0; i < NTCB; i++)
        {
                if(strcmp(sender, tcb[i].name) == 0)
                {
                        id = i;
                        break;
                }
        }
        if (id == -1)
        {
                printf("Error: Sender not exist\n");
                enable();
                return;
        }

        /*judge message quene idle?*/

        p(&tcb[current].sm);
        p(&tcb[current].mutex);
        get_buffer = remov(&(tcb[current].mq), id);
        v(&tcb[current].mutex);

        if (get_buffer == NULL)
        {
                v(&tcb[current].sm);
                enable();
                return 0;
        }
        size = get_buffer->size;
        for (i = 0; i < get_buffer->size; i++,b++)
                *b = get_buffer->text[i];

        /*reinitial the buff and put it into the global buff: freebuf*/
        get_buffer->sender = -1;
        get_buffer->size = 0;
        get_buffer->text[0] = '\0';
        p(&mutexfb);
        insert(freebuf, get_buffer);
        v(&mutexfb);
        v(&sfb);
        enable();
        return size;
}
void snd_msg()
{
    int i, j, k;
    char message[9] = "message";
    for (i = 0; i < 6; i++)
    {
        message[7] = i + '0';
        message[8] = '\0';
        send("rcv_msg", message, strlen(message));
        printf("%s has been sent.\n", message);

        for (j = 0; j < 10000; j++)
            for (k = 0; k < 10000; k++);
    }
    receive("rcv_msg", message);

}

void rcv_msg()
{
    int i, j, k;
    int size;
    char message[9];
    printf("\n");
    for (i = 0; i < 6; i++)
    {
        /*while loop until receive the message*/
        while( (size = receive("snd_msg", message)) != 0 ){
            message[size] = '\0';
            printf("%s has been received.\n", message);
        }
        for (j = 0; j < 10000; j++)
            for (k = 0; k < 10000; k++);
    }
}
void f1(void){
    int i,j,k;
    for(i=0;i<40;i++){
        putchar('a');
        for(j=0;j<10000;j++)
            for(k=0;k<10000;k++);
    }
}
void f2(void){
    int i,j,k;
    for(i=0;i<30;i++){
        putchar('b');
        for(j=0;j<10000;j++)
            for(k=0;k<10000;k++);
    }
}
void producer(void){
    int i;
    for(i=1;i<=5;i++){
        p(&empty);
        p(&mutex);
        get_pc_buffer = i;
        printf("producer produces NO.%d data\t",i);
        v(&mutex);
        v(&full);
    }
}
void consumer(void){
    int flag = 1;
    while(flag){
        p(&full);
        p(&mutex);
        printf("consumer get NO.%d data\t",get_pc_buffer--);
        if(get_pc_buffer == 0)
            flag = 0;
        v(&mutex);
        v(&empty);
    }
}

main(){
    InitInDos();
    InitTcb();
    old_int8=getvect(8);
    /*--create 0# thread--*/
    strcpy(tcb[0].name,"main");
    tcb[0].state = RUNNING;
    current=0;
    tcb[0].ss = _SS;
    tcb[0].sp = _SP;
    create("f1",(codeptr)f1,1024);
    create("f2",(codeptr)f2,1024);
    create("producer",(codeptr)producer,1024);
    create("consumer",(codeptr)consumer,1024);
    create("snd_msg",(codeptr)snd_msg,1024);
    create("rcv_msg",(codeptr)rcv_msg,1024);
    tcb_state();
/* 启动多个线程的并发执行 */
    setvect(8, new_int8);
    my_swtch();
    while (!finished());
    tcb_state();
/* 终止多任务系统 */
    tcb[0].name[0]='\0';
    tcb[0].state = FINISHED;
    setvect(8, old_int8);

    printf("\nThe Multi-task system is terminated!\n");
    getchar();
}
