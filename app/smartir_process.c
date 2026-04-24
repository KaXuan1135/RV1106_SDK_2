#include "common.h"
#include "comm_type.h"
#include "param.h"
#include "pwm.h"
#include "logLib.h"
#include <codec2_process.h> 
#include "comm_app.h"
#include <stdbool.h>

extern int is_somebody_active(void);

#define DELAY_2_TO_1 10
#define DELAY_1_TO_0 10
#define STABLE_DELAY    10  //
typedef struct {
	int last_mode;
	comm_isp_config_t config;
	bool changed;

	long time_begin;
	//智能双光 ： // 0 -- day, 1 --- night ir, 2 ---- night white  
	//定时模式：  // 0 -- not set, 1. set day, 2. --- set night
	int step;
	int status ; // 0 -- not init, 1 --- inited
	int stable_time;    //切换装载模式后，有一个稳定期
	int led_status;     //0------ no led    1----- ir led   2 ------ white led
	
}IRCUT_CNT;

extern bool comm_get_SmartIr_gain(double *rggain, double *bggain);
extern int app_ircut_set_ir_led(int stream_index,int strength);
extern int app_ircut_set_white_led(int stream_index,int strength);
extern int comm_get_smartIr_ledMode(int camId);
extern unsigned long GetTickCountOfSec(void);
extern void reconfig_smartIr(void);
extern void reconfig_smartIr_irled(void);
extern void reconfig_smartIr_visled(void);
extern void reconfig_smartIr_irled_day();
extern void reconfig_smartIr_visled_day(void);

static void ir_mode(int stream_index);
static void day_mode(int stream_index);
static void white_mode(int stream_index);
static void auto_duallight_mode(int stream_index, IRCUT_CNT *cnt, int cds);

static IRCUT_CNT g_cnt;

unsigned long GetTickCountOfSec(void)  
{  
    struct timespec ts;  
  
    clock_gettime(CLOCK_MONOTONIC, &ts);  
  
    return (ts.tv_sec);  
}
unsigned long GetTickCountOfMs(void)  
{  
    struct timespec ts;  
  
    clock_gettime(CLOCK_MONOTONIC, &ts);  
  
    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);  
}  

void notify_ircut_mode_change(void)
{
	if(g_cnt.changed == false)
		g_cnt.changed = true;
}
void ysb_smart_ir_process(int stream_index, int cds)
{
	//static long time_begin = 0;
    int camid = app_get_camid(stream_index);
    comm_param_getSensorConfig(camid,&g_cnt.config);
	if(g_cnt.status == 0 || g_cnt.changed)
	{
		g_cnt.step = 0;
		g_cnt.status = 1;
		g_cnt.time_begin = GetTickCountOfSec();
		g_cnt.last_mode = 0;
        g_cnt.led_status = 0;
		if(g_cnt.changed)
		{
			day_mode(stream_index);
			if(g_cnt.config.dayNight.iMode == APP_DAYNIGHT_AUTO_MODE) //自动模式，重新导入 smartir参数
                if(g_cnt.config.dayNight.iLedMode == APP_LED_IR_MODE ||
                    g_cnt.config.dayNight.iLedMode == APP_LED_DAUL_MODE)
                {
                    reconfig_smartIr_irled();
                }
                else 
                {
                     reconfig_smartIr_visled();
                 }
                g_cnt.stable_time = GetTickCountOfSec();
		}
		g_cnt.changed = false;
		os_dbghint("light mode: %d ledmode == %d ", g_cnt.config.dayNight.iMode,g_cnt.config.dayNight.iLedMode);
	}
	//static int step = 0; 
	
	if(g_cnt.config.dayNight.iLedMode != APP_LED_NONE &&  g_cnt.config.dayNight.iMode == APP_DAYNIGHT_AUTO_MODE) //智能双光模式
	{
//	    os_dbg("AI dual led mode");
		if(g_cnt.last_mode != g_cnt.config.dayNight.iMode)
		{
			g_cnt.last_mode = g_cnt.config.dayNight.iMode;
			g_cnt.step = 0;
		}
		auto_duallight_mode(stream_index, &g_cnt, cds);
	}else if(g_cnt.config.dayNight.iMode == APP_DAYNIGHT_SPEC_MODE){ //定时模式
		unsigned int now_sec;
		time_t now = time(NULL);
		struct tm tmp;
		if(g_cnt.last_mode != g_cnt.config.dayNight.iMode)
		{
			g_cnt.last_mode = g_cnt.config.dayNight.iMode;
			g_cnt.step = 0;
		}
		os_dbg("timer led mode");
		localtime_r(&now, &tmp);
		now_sec = 3600 * tmp.tm_hour + 60 * tmp.tm_min + tmp.tm_sec;
		unsigned int night_sec = 3600 * g_cnt.config.dayNight.start.start_hour + 60 * g_cnt.config.dayNight.start.start_minute + g_cnt.config.dayNight.start.start_second;
		unsigned int day_sec = 3600 * g_cnt.config.dayNight.end.start_hour + 60 * g_cnt.config.dayNight.end.start_minute + g_cnt.config.dayNight.end.start_second;
		if(night_sec == day_sec)
		{
			os_dbgerr("error para!"); return;
		}
		if(g_cnt.step != 1 && ((night_sec > day_sec && (day_sec <= now_sec && now_sec < night_sec)) ||
							  (night_sec < day_sec && (day_sec <= now_sec || now_sec < night_sec))))
		{
			os_dbg("set day");
			g_cnt.step = 1;
			//设置白天
			day_mode(stream_index);
		}else if(g_cnt.step != 2 && ((night_sec > day_sec && (day_sec > now_sec || now_sec >= night_sec)) || 
									(night_sec < day_sec && (now_sec >= night_sec && now_sec < day_sec)))){
			os_dbg("set night");

			g_cnt.step = 2;
			//设置夜晚
			if(g_cnt.config.dayNight.iLedMode == APP_LED_IR_MODE)
				ir_mode(stream_index);
			else if(g_cnt.config.dayNight.iLedMode == APP_LED_WHITE_MODE)
				white_mode(stream_index);
			else if(g_cnt.config.dayNight.iLedMode == APP_LED_DAUL_MODE)
            { //双光
				app_ircut_set_open(stream_index, false);
				app_ircut_set_ir_led(0, g_cnt.config.dayNight.iLedLightValue);
				app_ircut_set_white_led(0, g_cnt.config.dayNight.iLedLightValue);	
			}	
		}
	}else if(g_cnt.config.dayNight.iMode == APP_DAYNIGHT_NIGHT_MODE){//常开
//	    os_dbg("night led mode last_mode == %d iLedMode == %d ",g_cnt.last_mode,g_cnt.config.dayNight.iLedMode);
		if(g_cnt.last_mode != g_cnt.config.dayNight.iMode)
		{
			g_cnt.last_mode = g_cnt.config.dayNight.iMode;
			g_cnt.step = 0;
		    os_dbg("night led mode");
			//设置夜晚
			if(g_cnt.config.dayNight.iLedMode == APP_LED_IR_MODE)
				ir_mode(stream_index);
			else if(g_cnt.config.dayNight.iLedMode == APP_LED_WHITE_MODE)
				white_mode(stream_index);
			else if(g_cnt.config.dayNight.iLedMode == APP_LED_DAUL_MODE)
            { //双光
				app_ircut_set_open(stream_index, false);
				app_ircut_set_ir_led(0, g_cnt.config.dayNight.iLedLightValue);
				app_ircut_set_white_led(0, g_cnt.config.dayNight.iLedLightValue);	
			}			
		}
	}else if(g_cnt.config.dayNight.iMode == APP_DAYNIGHT_DAY_MODE){//常关
//	     os_dbg("day led mode  last_mode == %d ",g_cnt.last_mode);
		if(g_cnt.last_mode != g_cnt.config.dayNight.iMode)
		{
		    os_dbg("day led mode");
			g_cnt.last_mode = g_cnt.config.dayNight.iMode;
			g_cnt.step = 0;

			//设置白天
			day_mode(stream_index);
		}
	}
}

void auto_duallight_mode(int stream_index, IRCUT_CNT *cnt, int cds)
{
    int camid = app_get_camid(stream_index);
	if(cds)//day
	{
//        os_dbg("switch day step == %d ",cnt->step);
		if(cnt->step == 0)
		{	
			day_mode(stream_index);

			cnt->step = 0;
		}else{ 
			//ysb_smart_ir_process(stream_index, 0);
			 if(cnt->config.dayNight.iLedMode == APP_LED_DAUL_MODE)
            {   
                /*
    			if(is_somebody_active() && cnt->step == 2)
    			{
    				cnt->time_begin = GetTickCountOfSec();
    			}
    			else 
    			*/
                if(cnt->step == 2 && (GetTickCountOfSec() - cnt->time_begin) > DELAY_2_TO_1)
    			{// ir mode
                    if(comm_get_smartIr_ledMode(camid) != APP_LED_IR_MODE)
                    {
                        cnt->stable_time = GetTickCountOfSec();
                         reconfig_smartIr_irled();
                     }
                    day_mode(stream_index);
    				cnt->time_begin = GetTickCountOfSec();
    				cnt->step = 0;
//    				os_dbghint("step=%d", cnt->step);
    			}
            }
            else if(cnt->config.dayNight.iLedMode == APP_LED_WHITE_MODE)
            {
                 if(is_somebody_active() && cnt->step == 2)
    			{
    				cnt->time_begin = GetTickCountOfSec();
    			}
    			else if(cnt->step == 2 && (GetTickCountOfSec() - cnt->time_begin) > DELAY_2_TO_1)
    			{
    			    // ir mode
                    day_mode(stream_index);
                    cnt->step = 0;
//                    os_dbghint("step=%d", cnt->step);
    			}                 
            }
			if(cnt->step == 1 && (GetTickCountOfSec() - cnt->time_begin) > DELAY_1_TO_0)
			{
			    // day mode
				day_mode(stream_index);
				cnt->step = 0;
//				os_dbghint("step=%d", cnt->step);
			}
		}	
		
	}else { //night
        if(cnt->config.dayNight.iLedMode == APP_LED_DAUL_MODE)
		{
		    if(!is_somebody_active())
            {      
    			if(cnt->step == 0){ // ir mode
//    				os_dbghint("--- step=%d, %u", cnt->step, GetTickCountOfSec());
    				ir_mode(stream_index);
    				//need_turn = false;
    				cnt->time_begin = GetTickCountOfSec();
    				cnt->step = 1;
//    				os_dbghint("step=%d", cnt->step);
    			}else if (cnt->step == 1){
    				cnt->time_begin = GetTickCountOfSec();
    			}else if(cnt->step == 2 && (GetTickCountOfSec() - cnt->time_begin) > DELAY_2_TO_1){
                    if(comm_get_smartIr_ledMode(camid) != APP_LED_IR_MODE)
                    {
                        cnt->stable_time = GetTickCountOfSec();
                         reconfig_smartIr_irled();
                         ir_mode(stream_index);
                     }
    				cnt->time_begin = GetTickCountOfSec();
    				cnt->step = 1;
//    				os_dbghint("step=%d", cnt->step);				
    			}
            }
            else
            {
                if(comm_get_smartIr_ledMode(camid) != APP_LED_WHITE_MODE)
                 {
                    cnt->stable_time = GetTickCountOfSec();
                    reconfig_smartIr_visled();
                    white_mode(stream_index);
                 }
    			cnt->step = 2;
    			cnt->time_begin = GetTickCountOfSec();
 //   			os_dbghint("step=%d, %u", cnt->step, cnt->time_begin);
            }
		}else if(cnt->config.dayNight.iLedMode == APP_LED_IR_MODE){ // white mode

            ir_mode(stream_index);
            cnt->time_begin = GetTickCountOfSec();
            cnt->step = 1;
//            os_dbghint("--- step=%d, %u", cnt->step, GetTickCountOfSec());
		}
        else if(cnt->config.dayNight.iLedMode == APP_LED_WHITE_MODE)
        {
			white_mode(stream_index);
			cnt->step = 2;
			cnt->time_begin = GetTickCountOfSec();
//			os_dbghint("step=%d, %u", cnt->step, cnt->time_begin);        
        }
	}

}

void ir_mode(int stream_index)
{
	app_ircut_set_graymode(stream_index, true);
	app_ircut_set_open(stream_index,true);
	app_ircut_set_white_led(0, 0);
	app_ircut_set_ir_led(0, g_cnt.config.dayNight.iLedLightValue);  //30
	g_cnt.led_status = 1;
}

void white_mode(int stream_index)
{
	app_ircut_set_open(stream_index, false);
	app_ircut_set_ir_led(0, 0);
	
	app_ircut_set_white_led(0, g_cnt.config.dayNight.iLedLightValue);
	app_ircut_set_graymode(stream_index, false);
    g_cnt.led_status = 2;	
}

void day_mode(int stream_index)
{
    g_cnt.led_status = 0;
	app_ircut_set_open(stream_index,false);
	app_ircut_set_ir_led(0, 0);
	app_ircut_set_white_led(0, 0);	
	app_ircut_set_graymode(stream_index, false);
}

int led_status()
{
    return g_cnt.led_status;
}

void ysb_smart_ir_calib(int stream_index)
{
	int times = 30, i = 0;
	double rggain_all = 0, bggain_all = 0;
	double rggain;
	double bggain;
	bool ret;
	
	ir_mode(stream_index);
	//测试多次，计算平均值
	while(times --)
	{
		ret = comm_get_SmartIr_gain(&rggain, &bggain);
		if(ret)
		{
			rggain_all += rggain;
			bggain_all += bggain;
			i++;
		}
	}
	if(i > 0)
		os_dbg("rggain: %f, bggamin: %f(%d)", rggain_all/i, bggain_all/i, i);
	else
		os_dbg("no data");
}
