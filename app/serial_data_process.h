#ifndef SERIAL_DATA_PROCESS_H____
#define SERIAL_DATA_PROCESS_H____


typedef enum {
	CUSTOM_KAICHUANG = 666,
	CUSTOM_YUNSHIBAO = 777,
	
	CUSTOM_LAST,
}CUSTOM_NAME_T;

#define CUSTOM_NAME_ID 777  // CUSTOM_YUNSHIBAO

int comm_app_serial_data_process(int serial_no,unsigned char *buf,int size,void *pUser);





#endif