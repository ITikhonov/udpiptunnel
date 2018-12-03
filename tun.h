

struct tun_state {
        int                     tun_device;
        char                    tun_if_name[20];

	char                    packet_from_tun[2048];
	int                     packet_from_tun_len;
};

#define GT(x) typeof(((struct tun_state*)0)->x) *x
#define RT(x) const typeof(((struct tun_state*)0)->x) x

void setup_tun_device (GT(tun_device), GT(tun_if_name));
void configure_tun_interface (RT(tun_if_name));
void receive_tun_side(RT(tun_device),GT(packet_from_tun),GT(packet_from_tun_len));
void forward_to_tun (RT(tun_device),char *packet,int packet_len);
