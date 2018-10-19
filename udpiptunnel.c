#include <time.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/if_tun.h>

static void nothing(char *s,...) {
}

#define LOG nothing

int			g_argc;
char			**g_argv;

struct sockaddr_in	g_local_addr;
in_addr_t		g_remote_addr;
int			g_remote_port; 
struct sockaddr_in	recv_addr;
struct sockaddr_in	send_addr;

int			udp_socket;
int			tun_device;

char			tun_if_name[IFNAMSIZ];


char			packet_from_tun[2048];
int			packet_from_tun_len;

char			packet_from_udp[2048];
int			packet_from_udp_len;

time_t			last_ping=0;



static void do_panic(const char *func, int line) {
	fprintf(stderr,"PANIC in %s:%u\n",func,line);
	perror("errno");
	abort ();
}


#define PANIC() do_panic(__FUNCTION__,__LINE__)

#define G(x) typeof(g_##x) *x
#define R(x) const typeof(g_##x) x


static void args(int argc_, char *argv_[], G(argc), G(argv)) {
	*argc=argc_;
	*argv=argv_;
}


static void parse_local_addr(R(argc), R(argv), G(local_addr)) {
	if (argc<3) PANIC ();

	local_addr->sin_family=AF_INET;
	local_addr->sin_addr.s_addr=inet_addr(argv[1]);
	local_addr->sin_port=htons(atoi(argv[2]));
}

static void setup_udp_socket(R(local_addr)) {
	udp_socket=socket(AF_INET,SOCK_DGRAM,0);
	if (udp_socket==-1) PANIC ();

	int r=bind(udp_socket,(struct sockaddr*)&local_addr,sizeof(local_addr));
	if (r==-1) PANIC ();

	send_addr.sin_port=0;
}


static void parse_remote_addr(R(argc),R(argv),G(remote_addr),G(remote_port)) {
	if (argc<4) PANIC ();

	*remote_addr=inet_addr(argv[3]);
	*remote_port=0;

	if (argc>=5) {
		*remote_port=htons(atoi(argv[4]));
	}

	send_addr.sin_family=AF_INET;
	send_addr.sin_addr.s_addr=*remote_addr;
	send_addr.sin_port=*remote_port;
}


static void setup_tun_device(void) {
	struct ifreq ifr;

	tun_device = open("/dev/net/tun", O_RDWR);
	if (tun_device==-1) PANIC ();

	memset(&ifr,0,sizeof(ifr));
	ifr.ifr_flags=IFF_TUN|IFF_NO_PI;
	int r=ioctl(tun_device, TUNSETIFF, (void *)&ifr);

	if (r==-1) PANIC ();

	strcpy(tun_if_name, ifr.ifr_name);
}


static void configure_tun_interface(void) {
	setenv("IFNAME",tun_if_name,1);
	system("./conf-tun.sh");
}


static void wait_for_packets(void) {
	fcntl(udp_socket,F_SETFL,O_NONBLOCK);
	fcntl(tun_device,F_SETFL,O_NONBLOCK);

	int maxfd=udp_socket;
	if (maxfd<tun_device) maxfd=tun_device;

	for(;;) {
		struct timeval tv;
		tv.tv_sec=60;
		tv.tv_usec=0;

		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(tun_device,&rfds);
		FD_SET(udp_socket,&rfds);
		int r=select(maxfd+1, &rfds, NULL, NULL, &tv);
		if (r>=0) break;
	}
}


static void receive_tun_side(void) {
	int r=read(tun_device,packet_from_tun+1,2048);
	packet_from_tun[0]=1;
	packet_from_tun_len=r;
}

static void ping(void) {
	char ping[1];
	sendto(udp_socket,&ping,1,0,(struct sockaddr*)&send_addr,sizeof(send_addr));

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);
	last_ping=now.tv_sec;
}


static int minute_passed(void) {
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC,&now);

	return (now.tv_sec-last_ping < 60);
}


static int have_remote(void) {
	return send_addr.sin_port!=0;
}


static int have_packet_from_tun(void) {
	return packet_from_tun_len>=0;
}


static void forward_to_udp(void) {
	LOG("sending %u bytes to %s:%u\n",packet_from_tun_len,inet_ntoa(send_addr.sin_addr),ntohs(send_addr.sin_port));


	sendto(udp_socket,packet_from_tun,packet_from_tun_len+1,0,(struct sockaddr*)&send_addr,sizeof(send_addr));
	packet_from_tun_len=-1;
}

static void receive_udp_side(void) {
	socklen_t an=sizeof(recv_addr);
	int r=recvfrom(udp_socket,packet_from_udp,2048,0,(struct sockaddr*)&recv_addr,&an);
	packet_from_udp_len=r;

	LOG("recieved %u bytes from %s:%u\n",r,inet_ntoa(recv_addr.sin_addr),ntohs(recv_addr.sin_port));
}


static int network_packet () {
	return packet_from_udp[0]==1;
}


static void forward_to_tun(void) {
	write(tun_device,packet_from_udp+1,packet_from_udp_len-1);
	LOG("sent to tun\n");
}

static int have_packet_from_udp(void) {
	return packet_from_udp_len>=0;
}


static void drop_udp_packet(void) {
	packet_from_udp_len=-1;
}

static int unexpected_ip(R(remote_addr)) {
	return recv_addr.sin_addr.s_addr!=remote_addr;
}


static int fixed_port(R(remote_port)) {
	return remote_port!=0;
}


static int unexpected_port(R(remote_port)) {
	return recv_addr.sin_port!=remote_port;
}


static void remember_new_return_port(void) {
	send_addr.sin_port=recv_addr.sin_port;
}



int main(int argc_, char *argv_[]) {
	args(argc_,argv_,&g_argc,&g_argv);

	parse_local_addr (g_argc,g_argv,&g_local_addr);
	parse_remote_addr (g_argc,g_argv,&g_remote_addr,&g_remote_port);

	setup_udp_socket (g_local_addr);
	setup_tun_device ();

	configure_tun_interface ();

	ping ();
	for(;;) {
		wait_for_packets ();
		receive_tun_side ();
		receive_udp_side ();

		if (minute_passed ())
			if(have_remote())
				ping ();

		if (have_packet_from_udp ()) {
			if (unexpected_ip (g_remote_addr))
				goto drop;
			if (fixed_port (g_remote_port))
				if (unexpected_port (g_remote_port))
					goto drop;

			if (!fixed_port (g_remote_port))
				remember_new_return_port ();

			if (network_packet ())
				forward_to_tun ();
		drop:
			drop_udp_packet ();
		}

		if(have_packet_from_tun())
			if(have_remote())
				forward_to_udp ();
	}
}

