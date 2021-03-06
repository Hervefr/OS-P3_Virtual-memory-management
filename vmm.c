#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/stat.h>
#include <math.h>
#include <signal.h> //
#include<fcntl.h>  // 
#include "vmm.h"


PageTableItem pageTable[PAGE_SUM];//二级页表数组
BYTE actMem[ACTUAL_MEMORY_SIZE];//模拟实存空间的数组
FILE *ptr_auxMem;//指向模拟辅存空间的文件指针
BOOL blockStatus[BLOCK_SUM];//物理块使用标识
Ptr_MemoryAccessRequest ptr_memAccReq;//访存请求
OuterPageTableItem outerpagetable [OUTER_PAGE_TOTAL];//一级页表数组


unsigned int Time[PAGE_SUM];//记录页面使用时间，便于FIFO算法使用
PCB pcb[PID_NUM];//记录作业对应的起始以及结束页表号
unsigned int exec_times;//记录页面使用的次数，便于LRU算法使用

int time_n;
int work_id;

/* 页表 */
//PageTableItem pageTable[PAGE_SUM];
/* 实存空间 */
//BYTE actMem[ACTUAL_MEMORY_SIZE];
/* 用文件模拟辅存空间 */
//FILE *ptr_auxMem;
/* 物理块使用标识 */
//BOOL blockStatus[BLOCK_SUM];
/* 访存请求 */
//Ptr_MemoryAccessRequest ptr_memAccReq;



/* 初始化环境 */
void do_init()
{
	int i,j,n;
	srand(time(NULL));
	time_n=0;
    	exec_times=0;
//printf("%d\n",PAGE_SUM);
	for(n=0;n<OUTER_PAGE_TOTAL;n++)
	{
		outerpagetable[n].page_num=n;
		outerpagetable[n].index_num=n*PAGE_SIZE;
		for(i=n*PAGE_SIZE;(i<(n+1)*PAGE_SIZE)&&(i<PAGE_SUM);i++)
		{
			pageTable[i].pageNum=i;
			pageTable[i].filled=FALSE;
			pageTable[i].edited=FALSE;
			pageTable[i].count=0;
			/* 使用随机数设置该页的保护类型 */
			switch (random()%7)
			{
				case 0:
					pageTable[i].proType=READABLE;
					break;
				case 1:
					pageTable[i].proType=WRITABLE;
					break;
				case 2:
					pageTable[i].proType=EXECUTABLE;
					break;
				case 3:
					pageTable[i].proType=READABLE|WRITABLE;
					break;
				case 4:
					pageTable[i].proType=READABLE|EXECUTABLE;
					break;
				case 5:
					pageTable[i].proType=WRITABLE|EXECUTABLE;
					break;
				case 6:
					pageTable[i].proType=READABLE|WRITABLE|EXECUTABLE;
					break;
				default:
					break;
			}
			/* 设置该页对应的辅存地址 */
			pageTable[i].virAddr=i*PAGE_SIZE*2;
		}
	}
	for (j=0;j<BLOCK_SUM;j++)
	{
		/* 随机选择一些物理块进行页面装入 */
		if(random()%2==0)
		{
			do_page_in(&pageTable[j],j);
			pageTable[j].blockNum=j;
			pageTable[j].filled=TRUE;
			//pagetable[j].no_use++;
			blockStatus[j]=TRUE;
			Time[time_n++]=pageTable[j].pageNum;
		}
		else
			blockStatus[j]=FALSE;
	}

	//规定作业号以及对应的页表号
	pcb[0].pid=1;
	pcb[0].begin=0;
	pcb[0].end=7;
	pcb[1].pid=2;
	pcb[1].begin=8;
	pcb[1].end=15;
}
/* 响应请求 */

void do_response()
{
	Ptr_PageTableItem ptr_pageTabIt;
	unsigned int outer_page_num,in_page_offset,offset,in_page_num;
	unsigned int actAddr,i;
// @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	int count=0;
	bzero(ptr_memAccReq,sizeof(MemoryAccessRequest));
	if((count=read(fifo,ptr_memAccReq,sizeof(MemoryAccessRequest)))<0)
	{
		printf("read fifo failed");
		exit(0);
	}
	if(count)
	{
		printf("read a req");
	}
	else
	{
		printf("no data read");
		return;
	}

	/* 检查地址是否越界 */
	if (ptr_memAccReq->virAddr < 0 || ptr_memAccReq->virAddr >= VIRTUAL_MEMORY_SIZE)
	{
		do_error(ERROR_OVER_BOUNDARY);
		return;
	}
	/* 计算页号和页内偏移值 */
	outer_page_num=(ptr_memAccReq->virAddr/PAGE_SIZE)/PAGE_SIZE;//一级页表号
	in_page_offset=(ptr_memAccReq->virAddr/PAGE_SIZE)%PAGE_SIZE;//二级偏移
	offset=ptr_memAccReq->virAddr%PAGE_SIZE;//页内偏移

	for(i=0;i<PID_NUM;++i)
	{
		if(outer_page_num>=pcb[i].begin && outer_page_num<=pcb[i].end)
		{
			work_id=pcb[i].pid;//得到程序号
		}
	}
	in_page_num=outerpagetable[outer_page_num].index_num+in_page_offset;	//算得的二级页表号
	printf("程序号: %u\t一级页表号页号为：%u\t二级页表号页号为：%u\t页内偏移为：%u\n",work_id,outer_page_num,in_page_num,offset);
	/* 获取对应页表项 */
	ptr_pageTabIt=&pageTable[in_page_num];
	/* 根据特征位决定是否产生缺页中断 */
	if(!ptr_pageTabIt->filled)
	{
		do_page_fault(ptr_pageTabIt);
	}

	ptr_pageTabIt->no_use=exec_times++;
	actAddr=ptr_pageTabIt->blockNum*PAGE_SIZE+offset;
	printf("实地址为: %u\n",actAddr);
	
	/* 检查页面访问权限并处理访存请求 */
	switch (ptr_memAccReq->reqType)
	{
		case REQUEST_READ:	/*读请求*/
		{
			ptr_pageTabIt->count++;
			if(!(ptr_pageTabIt->proType&READABLE))	/*页面不可读*/
			{
				do_error(ERROR_READ_DENY);
				return;
			}
			/* 读取实存中的内容 */
			printf("读操作成功：值为%02X\n",actMem[actAddr]);
			break;
		}
		case REQUEST_WRITE:
		{
			ptr_pageTabIt->count++;
			if(!(ptr_pageTabIt->proType&WRITABLE))	/*页面不可写*/
			{
				do_error(ERROR_WRITE_DENY);	
				return;
			}
			/* 向实存中写入请求的内容 */
			actMem[actAddr]=ptr_memAccReq->value;
			ptr_pageTabIt->edited=TRUE;			
			printf("写操作成功\n");
			break;
		}
		case REQUEST_EXECUTE:	/*执行请求*/
		{
			ptr_pageTabIt->count++;
			if(!(ptr_pageTabIt->proType&EXECUTABLE))	/*页面不可执行*/
			{
				do_error(ERROR_EXECUTE_DENY);
				return;
			}			
			printf("执行成功\n");
			break;
		}
		default:	/*非法请求类型*/
		{	
			do_error(ERROR_INVALID_REQUEST);
			return;
		}
	}
}


/* 处理缺页中断 */
void do_page_fault(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int i;
	char c;
	printf("产生缺页中断，开始进行调页...\n");
	for(i=0;i<BLOCK_SUM;i++)
	{
	if(!blockStatus[i])
        {
		/* 读辅存内容，写入到实存 */
		do_page_in(ptr_pageTabIt, i);
		
		/* 更新页表内容 */
		ptr_pageTabIt->blockNum = i;
		ptr_pageTabIt->filled = TRUE;
		ptr_pageTabIt->edited = FALSE;
		ptr_pageTabIt->count = 0;
		
		blockStatus[i] = TRUE;
		return;
        }
    }
	/* 没有空闲物理块，进行页面替换 */
	printf("请选择页面淘汰算法\n FIFO:1,LFU:2,LRU:3\n");
	while(c=getchar())
	{
		if(c=='1')
		{
			do_LFU(ptr_pageTabIt);
			break;
		}
		else if(c=='2')
		{
			do_FIFO(ptr_pageTabIt);
			break;
		}
		else if(c=='3')
		{
			do_LRU(ptr_pageTabIt);
			break;
		}
	}
}

/* 根据LFU算法进行页面替换 */
void do_LFU(Ptr_PageTableItem ptr_pageTabIt)
{
    unsigned int i,min,page;
    printf("没有空闲物理块，开始进行LFU页面替换...\n ");
    //对最小频率进行初始化
    for(i=0, min = 0xFFFFFFFF, page = 0;i<PAGE_SUM;i++)
    {
        if(pageTable[i].count<min&&pageTable[i].filled==TRUE)
        {
            min=pageTable[i].count;
            page=pageTable[i].pageNum;
        }
    }
    printf("选择第%u页进行替换/n",page);
    if(pageTable[page].edited)
    {
	/* 页面内容有修改，需要写回至辅存 */
        printf("该页内容有修改，写回至辅存\n");
        do_page_out(&pageTable[page]);
    }
    pageTable[page].edited=FALSE;
    pageTable[page].count=0;
    pageTable[page].filled=FALSE;

	/* 读辅存内容，写入到实存 */
    do_page_in(ptr_pageTabIt,pageTable[page].blockNum);

	/* 更新页表内容 */
    ptr_pageTabIt->blockNum=pageTable[page].blockNum;
    ptr_pageTabIt->edited=FALSE;
    ptr_pageTabIt->count=0;
	//ptable->no_use=0;
    ptr_pageTabIt->filled=TRUE;
	printf("页面替换成功\n");
}

/* 根据FIFO算法进行页面替换 */
void do_FIFO(Ptr_PageTableItem ptr_pageTabIt)
{
    unsigned int firstcome;
    firstcome=Time[0];//Time[PAGE_SUM]里面存着现在的主存里的页面存储状况，每一项是页号，第一项是先进来的页面
    printf("没有空闲物理块，开始进行FIFO页面替换...\n ");
    printf("选择第%u页进行替换/n",firstcome);
    if(pageTable[firstcome].edited)
    {
          printf("该页内容有修改，写回至辅存.\n");
          do_page_out(&pageTable[firstcome]);
    }
    pageTable[firstcome].edited=FALSE;
    pageTable[firstcome].count=0;
    pageTable[firstcome].no_use=0;
    pageTable[firstcome].filled=FALSE;

    do_page_in(ptr_pageTabIt,pageTable[firstcome].blockNum);


    ptr_pageTabIt->blockNum=pageTable[firstcome].blockNum;
    ptr_pageTabIt->edited=FALSE;
    ptr_pageTabIt->count=0;
	//ptr_pageTabIt->no_use=0;
    ptr_pageTabIt->filled=TRUE;
    time_change(ptr_pageTabIt->pageNum);
}

/* 根据LRU算法进行页面替换 */
void do_LRU(Ptr_PageTableItem ptr_pageTabIt)
{
    unsigned int i,min_use,page;
    printf("没有空闲物理块，开始进行LRU页面替换...\n ");
    min_use=0xFFFFFFFF;//对最小频率进行初始化
    page=0;
    for(i=0;i<PAGE_SUM;i++)
    {
        if(min_use>pageTable[i].no_use&&pageTable[i].filled==TRUE)
        {
            min_use=pageTable[i].no_use;
            page=pageTable[i].pageNum;
        }
    }
    printf("选择第%u页进行替换/n",page);
    if(pageTable[page].edited)
    {
        printf("该页内容有修改，写回至辅存.\n");
        do_page_out(&pageTable[page]);
    }
    pageTable[page].edited=FALSE;
    pageTable[page].count=0;
    pageTable[page].no_use=0;
    pageTable[page].filled=FALSE;


    do_page_in(ptr_pageTabIt,pageTable[page].blockNum);


    ptr_pageTabIt->blockNum=pageTable[page].blockNum;
    ptr_pageTabIt->edited=FALSE;
    ptr_pageTabIt->count=0;
    ptr_pageTabIt->no_use=0;
    ptr_pageTabIt->filled=TRUE;

	time_change(ptr_pageTabIt->pageNum);

}

/* 将辅存内容写入实存 */
void do_page_in(Ptr_PageTableItem ptr_pageTabIt, unsigned int blockNum)
{
	unsigned int read_num;
	if(fseek(ptr_auxMem,ptr_pageTabIt->virAddr,SEEK_SET)<0)
	{
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if((read_num=fread(&actMem[blockNum*PAGE_SIZE],sizeof(BYTE),PAGE_SIZE,ptr_auxMem))<PAGE_SIZE)
	{
		do_error(ERROR_FILE_READ_FAILED);
		exit(1);
	}
	printf("调页成功：辅存地址%u-->>物理块%u\n",ptr_pageTabIt->virAddr,blockNum);
}

/* 将被替换页面的内容写回辅存 */
void do_page_out(Ptr_PageTableItem ptr_pageTabIt)
{
	unsigned int write_num;
	if(fseek(ptr_auxMem,ptr_pageTabIt->virAddr,SEEK_SET)<0)
	{
		do_error(ERROR_FILE_SEEK_FAILED);
		exit(1);
	}
	if((write_num=fwrite(&actMem[ptr_pageTabIt->blockNum*PAGE_SIZE],sizeof(BYTE),PAGE_SIZE,ptr_auxMem))<PAGE_SIZE)
	{
		do_error(ERROR_FILE_WRITE_FAILED);
		exit(1);
	}
	printf("写回成功：物理块%u-->>辅存地址%03X\n",ptr_pageTabIt->virAddr,ptr_pageTabIt->blockNum);
}

/* 错误处理 */
void do_error(ERROR_CODE code)
{
	switch (code)
	{
		case ERROR_READ_DENY:
			printf("访存失败：该地址内容不可读\n");
			break;
		case ERROR_WRITE_DENY:
			printf("访存失败：该地址内容不可写\n");
			break;
		case ERROR_EXECUTE_DENY:
			printf("访存失败：该地址内容不可执行\n");
			break;
		case ERROR_INVALID_REQUEST:
			printf("访存失败：非法访存请求\n");
			break;
		case ERROR_OVER_BOUNDARY:
			printf("访存失败：地址越界\n");
			break;
		case ERROR_FILE_OPEN_FAILED:
			printf("系统错误：打开文件失败\n");
			break;
		case ERROR_FILE_CLOSE_FAILED:
			printf("系统错误：关闭文件失败\n");
			break;
		case ERROR_FILE_SEEK_FAILED:
			printf("系统错误：文件指针定位失败\n");
			break;
		case ERROR_FILE_READ_FAILED:
			printf("系统错误：读取文件失败\n");
			break;
		case ERROR_FILE_WRITE_FAILED:
			printf("系统错误：写入文件失败\n");
			break;
		default:
			printf("未知错误：没有这个错误代码\n");
			break;
	}
}

/* 产生访存请求 */
void do_request()
{
	/* 随机产生请求地址 */
	ptr_memAccReq->virAddr = random() % VIRTUAL_MEMORY_SIZE;
	/* 随机产生请求类型 */
	switch (random() % 3)
	{
	case 0: //读请求
	{
		ptr_memAccReq->reqType = REQUEST_READ;
		printf("产生请求：\n地址：%u\t类型：读取\n", ptr_memAccReq->virAddr);
		break;
	}
	case 1: //写请求
	{
		ptr_memAccReq->reqType = REQUEST_WRITE;
		/* 随机产生待写入的值 */
		ptr_memAccReq->value = random() % 0xFFu;
		printf("产生请求：\n地址：%u\t类型：写入\t值：%02X\n", ptr_memAccReq->virAddr, ptr_memAccReq->value);
		break;
	}
	case 2:
	{
		  ptr_memAccReq->reqType = REQUEST_EXECUTE;
		  printf("产生请求：\n地址：%u\t类型：执行\n", ptr_memAccReq->virAddr);
		  break;
	}
	default:
		break;
	}
}

/* 打印页表 */
void do_print_info()
{
	unsigned int i,j,m;
	unsigned char str[4];
	printf("一级页号\t二级页号\t块号\t装入\t修改\t保护\t计数\t辅存\n");
	for(i=0;i<OUTER_PAGE_TOTAL;++i)
	{
		for(j=0;j<PAGE_SIZE;++j)
		{
			m=outerpagetable[i].index_num+j;
			printf("%u\t\t%u\t\t%u\t%u\t%u\t%s\t%u\t%u\n", i, pageTable[m].pageNum,pageTable[m].blockNum, pageTable[m].filled, 
				pageTable[m].edited, get_protype_str(str, pageTable[m].proType), 
				pageTable[m].count, pageTable[m].virAddr);
		}
	}
}
void time_change(unsigned int num)
{
	int i;
	for(i=0;i<time_n-1;++i)
		Time[i]=Time[i+1];
	Time[time_n]=num;
}
/* 获取页面保护类型字符串 */
char * get_protype_str(char *str,BYTE type)
{
	if (type & READABLE)
		str[0] = 'r';
	else
		str[0] = '-';
	if (type & WRITABLE)
		str[1] = 'w';
	else
		str[1] = '-';
	if (type & EXECUTABLE)
		str[2] = 'x';
	else
		str[2] = '-';
	str[3] = '\0';
	return str;
}

int main(int argc, char* argv[])
{
	char c;
	int i;
	struct stat statbuf;
	if (!(ptr_auxMem = fopen(AUXILIARY_MEMORY, "r+")))
	{
		do_error(ERROR_FILE_OPEN_FAILED);
		exit(1);
	}

	do_init();
	do_print_info();
	ptr_memAccReq = (Ptr_MemoryAccessRequest)malloc(sizeof(MemoryAccessRequest));
// @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
	if(stat("/tmp/server",&statbuf)==0){
		/* 如果FIFO文件存在,删掉 */
		if(remove("/tmp/server")<0){
			printf("remove failed");
			exit(0);
		}
	}

	if(mkfifo("/tmp/server",0666)<0){
		printf("mkfifo failed");
		exit(0);
	}
	/* 在非阻塞模式下打开FIFO,返回文件描述符 */
	if((fifo=open("/tmp/server",O_RDONLY|O_NONBLOCK))<0){
		printf("open fifo failed");
		exit(0);
	}


	/* 在循环中模拟访存请求与处理过程 */

	while (TRUE)
	{
// 		do_request();
		do_response();
		printf("按Y打印页表，按其他键不打印...\n");
		if ((c = getchar()) == 'y' || c == 'Y')
			do_print_info();
		while (c != '\n')
			c = getchar();
		printf("按X退出程序，按其他键继续...\n");
		if ((c = getchar()) == 'x' || c == 'X')
			break;
		while (c != '\n')
			c = getchar();
		//sleep(5000);
	}

	if (fclose(ptr_auxMem) == EOF)
	{
		do_error(ERROR_FILE_CLOSE_FAILED);
		exit(1);
	}
	close(fifo);
	return (0);
}

