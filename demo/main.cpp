#include "common.h"
#include "start.h"

//手动设置cpu 频率
// echo "userspace" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
// echo 816000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
// echo 1104000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
// echo 1296000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed

extern "C" int comm_param_getSysRebootFlag(void);
static bool quit = false;
static void sigterm_handler(int sig) {
	fprintf(stderr, "signal %d\n", sig);
	quit = true;
}

int main(int argc,char *argv[])
{
	signal(SIGINT, sigterm_handler);

	os_dbg("--------  enter main !!!!! -------------");
	start_app();
	while(!quit && (comm_param_getSysRebootFlag() == 0))
	{  
		usleep(200 * 1000);
	}

	stop_app();
	os_dbg("--------  exit main !!!!! -------------");

	return 0;
}
