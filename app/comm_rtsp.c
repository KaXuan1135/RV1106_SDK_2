#include "common.h"
#include "comm_type.h"
//#include "param.h"
//#include "comm_codec.h"
#include "rtspServLib.h"
#include "comm_app.h"
static int app_getStreamCb(int local_chn, int chn_type, void *buf,
                    void **strm_rpospp, void *ctx)
{
	assert(strm_rpospp);
	read_pos_t *pRead_pos = NULL;
	pRead_pos = (read_pos_t *)*strm_rpospp;
	if(pRead_pos == NULL)
	{
		pRead_pos = (read_pos_t *)malloc(sizeof(read_pos_t));
		if( pRead_pos ==NULL )
		{
			perror("[app_getStreamCb,malloc]");
		}
		memset(pRead_pos,0,sizeof(read_pos_t));

		pRead_pos->read_begin = net_stream_getNetStreamWritePos(local_chn,chn_type);
		pRead_pos->read_end = net_stream_getNetStreamWritePos(local_chn,chn_type);
		*strm_rpospp = pRead_pos;
		pRead_pos->lock_pos = pRead_pos->read_begin;
//		comm_set_IFrame(local_chn, chn_type);
	}
	int *lock_pos = &pRead_pos->lock_pos;

	net_frame_t *p_frame = NULL;
	av_frame_head_t frame_head;
	memset(&frame_head,0,sizeof(av_frame_head_t));
//	os_dbg("local_chn :%d  chn_type:%d read_begin:%d read_end:%d ",local_chn,chn_type,pRead_pos->read_begin,pRead_pos->read_end);
	while (1)
	{
		net_stream_lockMutex(local_chn, chn_type, *lock_pos);
		p_frame = net_stream_getNetStreamFromPool(local_chn,chn_type,pRead_pos);
		if (p_frame == NULL)
		{
			net_stream_unlockMutex(local_chn,chn_type,*lock_pos);
			net_stream_netStreamWaiting_timeout(local_chn,chn_type,500);
			continue;
		}
		memcpy(buf, &(p_frame->frame_head), sizeof(frame_head));
		if (p_frame->frame_head.frame_size <= 0
				|| p_frame->frame_head.frame_size > AVFRAME_BLOCK_SIZE) {
			os_dbg("Frame size[%lu] is out of range!\n", p_frame->frame_head.frame_size);
			net_stream_unlockMutex(local_chn,chn_type,*lock_pos);
			return 1;
		}	
		memcpy(buf+sizeof(frame_head),p_frame->frame_data,p_frame->frame_head.frame_size);
		net_stream_unlockMutex(local_chn,chn_type,*lock_pos);
		return 0;
	}
	return 0;

}

#define valid_channel_num(x)	((x) < MAX_CHANN_NUM)
#define valid_channel_type(x)	((x) < MAX_STREAM_NUM)

int app_chnVerifyCB(int chn_no, int chn_type, void *ctx)
{

	if (!valid_channel_num(chn_no))
		return 0;

	if (!valid_channel_type(chn_type))
		return 0;
	return 1;
}

int app_mediaVerifyCB(int chn_no, int chn_type, int media_type, void *ctx)
{
	if (!app_chnVerifyCB(chn_no, chn_type, ctx))
		return 0;
	return 1;
}

void comm_start_rtsp(int rtsp_port)
{

	st_rtsp_initLib(rtsp_port,AVFRAME_BLOCK_SIZE);
	st_rtsp_setupRtcp(0, 60);
	st_rtsp_startService(app_getStreamCb, app_chnVerifyCB, app_mediaVerifyCB, NULL);
	os_dbg(">>>>>mainstream: rtsp://ip:%d/stream/av0_0\n",rtsp_port); //暂时取主码流
	os_dbg(">>>>>substream: rtsp://ip:%d/stream/av1_0\n",rtsp_port);

}

