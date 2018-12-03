#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <net/if_utun.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>

#include "tun.h"

static void do_panic (const char *func,int line) {
        fprintf (stderr,"PANIC in %s:%u\n",func,line);
        perror ("errno");
        abort ();
}


#define PANIC(...) do_panic(__FUNCTION__,__LINE__)


void setup_tun_device (GT(tun_device), GT(tun_if_name)) {
	int unit=0;
	struct sockaddr_ctl sc;
	struct ctl_info ctlInfo;

	memset(&ctlInfo, 0, sizeof(ctlInfo));
	strlcpy(ctlInfo.ctl_name, UTUN_CONTROL_NAME, sizeof(ctlInfo.ctl_name));

	int fd=socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
	if(ioctl(fd,CTLIOCGINFO,&ctlInfo)==-1) {
		PANIC("CTLIOCGINFO");
	}

	sc.sc_id = ctlInfo.ctl_id;
	sc.sc_len = sizeof(sc);
	sc.sc_family = AF_SYSTEM;
	sc.ss_sysaddr = AF_SYS_CONTROL;
	sc.sc_unit = unit + 1;

	if (connect(fd, (struct sockaddr *)&sc, sizeof(sc)) == -1) {
		PANIC("connect utun");
	}


	socklen_t utunname_len = sizeof(*tun_if_name);
	if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, *tun_if_name, &utunname_len)) {
		PANIC("UTUN_OPT_IFNAME");
	}

	*tun_device=fd;
}


void configure_tun_interface (RT(tun_if_name)) {
	setenv ("IFNAME",tun_if_name,1);
	system ("./conf-tun.sh");
}

void receive_tun_side (RT(tun_device),GT(packet_from_tun),GT(packet_from_tun_len)) {
        int r=read (tun_device,*packet_from_tun+1,2048);
	if(r>0) {
		// utun puts a useless 32-bit prefix in front of packets
		memmove(*packet_from_tun+1,*packet_from_tun+5,r); 
	}
        *packet_from_tun[0]=1;
        *packet_from_tun_len=r;
}

void forward_to_tun (RT(tun_device),char *packet,int packet_len) {
	packet++;
	packet_len--;
	char buf[packet_len+4];
	memcpy(buf+4,packet,packet_len);
	buf[0]=0;
	buf[1]=0;
	buf[2]=0;
	buf[3]=2;
        write (tun_device,buf,packet_len+4);
}

