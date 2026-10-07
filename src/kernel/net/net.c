#include <kernel/net/net.h>
#include <kernel/net/rtl8139.h>
#include <kernel/serial.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define ETHERNET_HEADER_SIZE 14
#define ARP_ETHERTYPE 0x0806
#define IPV4_ETHERTYPE 0x0800
#define UDP_PROTOCOL 17
#define TCP_PROTOCOL 6
#define ECHO_PORT 5555
#define TCP_ECHO_PORT 5556
#define MAX_FRAME_SIZE 1536
#define MAX_TCP_PAYLOAD 1460

enum tcp_connection_state
{
    TCP_LISTEN,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_LAST_ACK
};

static const u8 local_ip[4] = {10, 0, 2, 15};
static u8 local_mac[6];
static u8 transmit_frame[MAX_FRAME_SIZE];
static int network_ready;
static enum tcp_connection_state tcp_state;
static u8 tcp_remote_mac[6];
static u8 tcp_remote_ip[4];
static u16 tcp_remote_port;
static u32 tcp_send_next;
static u32 tcp_receive_next;
static u32 tcp_initial_sequence = 0x10203040;

static u16 read_be16(const u8 *data)
{
    return (u16)(((u16)data[0] << 8) | data[1]);
}

static u32 read_be32(const u8 *data)
{
    return ((u32)data[0] << 24)
        | ((u32)data[1] << 16)
        | ((u32)data[2] << 8)
        | data[3];
}

static void write_be16(u8 *data, u16 value)
{
    data[0] = (u8)(value >> 8);
    data[1] = (u8)value;
}

static void write_be32(u8 *data, u32 value)
{
    data[0] = (u8)(value >> 24);
    data[1] = (u8)(value >> 16);
    data[2] = (u8)(value >> 8);
    data[3] = (u8)value;
}

static u16 ipv4_checksum(const u8 *data, u32 length)
{
    u32 sum = 0;
    for (u32 i = 0; i + 1 < length; i += 2)
        sum += read_be16(data + i);
    if (length & 1)
        sum += (u16)data[length - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (u16)~sum;
}

static u16 tcp_checksum(
    const u8 *source_ip,
    const u8 *destination_ip,
    const u8 *segment,
    u16 length)
{
    u32 sum = 0;
    for (u32 i = 0; i < 4; i += 2)
    {
        sum += read_be16(source_ip + i);
        sum += read_be16(destination_ip + i);
    }
    sum += TCP_PROTOCOL;
    sum += length;
    for (u32 i = 0; i + 1 < length; i += 2)
        sum += read_be16(segment + i);
    if (length & 1)
        sum += (u16)segment[length - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (u16)~sum;
}

static void copy_bytes(u8 *destination, const u8 *source, u32 length)
{
    for (u32 i = 0; i < length; i++)
        destination[i] = source[i];
}

static int ip_equal(const u8 *left, const u8 *right)
{
    return left[0] == right[0] && left[1] == right[1]
        && left[2] == right[2] && left[3] == right[3];
}

static void ethernet_header(u8 *frame, const u8 *destination, u16 type)
{
    copy_bytes(frame, destination, 6);
    copy_bytes(frame + 6, local_mac, 6);
    write_be16(frame + 12, type);
}

static void handle_arp(const u8 *frame, u16 length)
{
    if (length < ETHERNET_HEADER_SIZE + 28)
        return;
    const u8 *arp = frame + ETHERNET_HEADER_SIZE;
    if (read_be16(arp) != 1 || read_be16(arp + 2) != IPV4_ETHERTYPE
        || arp[4] != 6 || arp[5] != 4 || read_be16(arp + 6) != 1
        || !ip_equal(arp + 24, local_ip))
        return;

    u8 *reply = transmit_frame;
    ethernet_header(reply, arp + 8, ARP_ETHERTYPE);
    u8 *packet = reply + ETHERNET_HEADER_SIZE;
    write_be16(packet, 1);
    write_be16(packet + 2, IPV4_ETHERTYPE);
    packet[4] = 6;
    packet[5] = 4;
    write_be16(packet + 6, 2);
    copy_bytes(packet + 8, local_mac, 6);
    copy_bytes(packet + 14, local_ip, 4);
    copy_bytes(packet + 18, arp + 8, 6);
    copy_bytes(packet + 24, arp + 14, 4);

    if (rtl8139_send(reply, ETHERNET_HEADER_SIZE + 28) != 0)
        serial_write("net: ARP reply transmit failed\n");
}

static void handle_udp(const u8 *frame, u16 frame_length)
{
    if (frame_length < ETHERNET_HEADER_SIZE + 20)
        return;
    const u8 *ip = frame + ETHERNET_HEADER_SIZE;
    u8 version = ip[0] >> 4;
    u8 header_length = (u8)((ip[0] & 0x0F) * 4);
    u16 total_length = read_be16(ip + 2);
    if (version != 4 || header_length < 20
        || total_length < header_length + 8
        || total_length > frame_length - ETHERNET_HEADER_SIZE
        || ip[9] != UDP_PROTOCOL || (read_be16(ip + 6) & 0x3FFF)
        || !ip_equal(ip + 16, local_ip)
        || ipv4_checksum(ip, header_length) != 0)
        return;

    const u8 *udp = ip + header_length;
    u16 udp_length = read_be16(udp + 4);
    if (read_be16(udp + 2) != ECHO_PORT
        || udp_length < 8 || udp_length > total_length - header_length)
        return;

    u32 payload_length = udp_length - 8;
    u32 response_length = ETHERNET_HEADER_SIZE + 20 + 8 + payload_length;
    if (response_length > sizeof(transmit_frame))
        return;

    u8 *reply = transmit_frame;
    ethernet_header(reply, frame + 6, IPV4_ETHERTYPE);
    u8 *reply_ip = reply + ETHERNET_HEADER_SIZE;
    reply_ip[0] = 0x45;
    reply_ip[1] = 0;
    write_be16(reply_ip + 2, (u16)(20 + 8 + payload_length));
    write_be16(reply_ip + 4, 0);
    write_be16(reply_ip + 6, 0x4000);
    reply_ip[8] = 64;
    reply_ip[9] = UDP_PROTOCOL;
    write_be16(reply_ip + 10, 0);
    copy_bytes(reply_ip + 12, ip + 16, 4);
    copy_bytes(reply_ip + 16, ip + 12, 4);
    write_be16(reply_ip + 10, ipv4_checksum(reply_ip, 20));

    u8 *reply_udp = reply_ip + 20;
    write_be16(reply_udp, read_be16(udp + 2));
    write_be16(reply_udp + 2, read_be16(udp));
    write_be16(reply_udp + 4, (u16)(8 + payload_length));
    write_be16(reply_udp + 6, 0);
    copy_bytes(reply_udp + 8, udp + 8, payload_length);

    if (rtl8139_send(reply, (u16)response_length) != 0)
        serial_write("net: UDP echo transmit failed\n");
}

static int bytes_equal(const u8 *left, const u8 *right, u32 length)
{
    for (u32 i = 0; i < length; i++)
        if (left[i] != right[i])
            return 0;
    return 1;
}

static int tcp_peer_matches_segment(
    const u8 *frame,
    const u8 *ip,
    u16 source_port)
{
    return source_port == tcp_remote_port
        && ip_equal(ip + 12, tcp_remote_ip)
        && bytes_equal(frame + 6, tcp_remote_mac, sizeof(tcp_remote_mac));
}

static int send_tcp_segment(
    const u8 *destination_mac,
    const u8 *destination_ip,
    u16 source_port,
    u16 destination_port,
    u32 sequence,
    u32 acknowledgement,
    u8 flags,
    const u8 *payload,
    u16 payload_length)
{
    u32 ip_length = 20 + 20 + payload_length;
    u32 frame_length = ETHERNET_HEADER_SIZE + ip_length;
    if (frame_length > sizeof(transmit_frame)
        || payload_length > MAX_TCP_PAYLOAD)
        return -1;

    ethernet_header(transmit_frame, destination_mac, IPV4_ETHERTYPE);
    u8 *ip = transmit_frame + ETHERNET_HEADER_SIZE;
    ip[0] = 0x45;
    ip[1] = 0;
    write_be16(ip + 2, (u16)ip_length);
    write_be16(ip + 4, 0);
    write_be16(ip + 6, 0x4000);
    ip[8] = 64;
    ip[9] = TCP_PROTOCOL;
    write_be16(ip + 10, 0);
    copy_bytes(ip + 12, local_ip, 4);
    copy_bytes(ip + 16, destination_ip, 4);
    write_be16(ip + 10, ipv4_checksum(ip, 20));

    u8 *tcp = ip + 20;
    write_be16(tcp, TCP_ECHO_PORT);
    write_be16(tcp + 2, destination_port);
    write_be32(tcp + 4, sequence);
    write_be32(tcp + 8, acknowledgement);
    tcp[12] = 0x50;
    tcp[13] = flags;
    write_be16(tcp + 14, 4096);
    write_be16(tcp + 16, 0);
    write_be16(tcp + 18, 0);
    if (payload_length)
        copy_bytes(tcp + 20, payload, payload_length);
    write_be16(
        tcp + 16,
        tcp_checksum(local_ip, destination_ip, tcp, (u16)(20 + payload_length)));

    return rtl8139_send(transmit_frame, (u16)frame_length);
}

static void handle_tcp(const u8 *frame, u16 frame_length)
{
    if (frame_length < ETHERNET_HEADER_SIZE + 40)
        return;
    const u8 *ip = frame + ETHERNET_HEADER_SIZE;
    u8 ip_header_length = (u8)((ip[0] & 0x0F) * 4);
    u16 ip_length = read_be16(ip + 2);
    if ((ip[0] >> 4) != 4 || ip_header_length < 20
        || ip_length < ip_header_length + 20
        || ip_length > frame_length - ETHERNET_HEADER_SIZE
        || ip[9] != TCP_PROTOCOL || (read_be16(ip + 6) & 0x3FFF)
        || !ip_equal(ip + 16, local_ip)
        || ipv4_checksum(ip, ip_header_length) != 0)
        return;

    const u8 *tcp = ip + ip_header_length;
    u16 tcp_length = (u16)(ip_length - ip_header_length);
    u8 tcp_header_length = (u8)((tcp[12] >> 4) * 4);
    u16 source_port = read_be16(tcp);
    u16 destination_port = read_be16(tcp + 2);
    u32 sequence = read_be32(tcp + 4);
    u32 acknowledgement = read_be32(tcp + 8);
    u8 flags = tcp[13];
    if (destination_port != TCP_ECHO_PORT
        || tcp_header_length < 20
        || tcp_header_length > tcp_length
        || tcp_checksum(ip + 12, ip + 16, tcp, tcp_length) != 0)
        return;

    const u8 *payload = tcp + tcp_header_length;
    u16 payload_length = (u16)(tcp_length - tcp_header_length);
    if (payload_length > MAX_TCP_PAYLOAD)
        return;

    if (flags & 0x04)
    {
        if (tcp_state != TCP_LISTEN
            && tcp_peer_matches_segment(frame, ip, source_port))
            tcp_state = TCP_LISTEN;
        return;
    }

    if (tcp_state == TCP_LISTEN)
    {
        if (!(flags & 0x02) || (flags & 0x10))
            return;
        copy_bytes(tcp_remote_mac, frame + 6, sizeof(tcp_remote_mac));
        copy_bytes(tcp_remote_ip, ip + 12, sizeof(tcp_remote_ip));
        tcp_remote_port = source_port;
        tcp_receive_next = sequence + 1;
        tcp_send_next = tcp_initial_sequence;
        tcp_initial_sequence += 0x10000;
        if (send_tcp_segment(
                tcp_remote_mac,
                tcp_remote_ip,
                TCP_ECHO_PORT,
                tcp_remote_port,
                tcp_send_next,
                tcp_receive_next,
                0x12,
                0,
                0) != 0)
            return;
        tcp_send_next++;
        tcp_state = TCP_SYN_RECEIVED;
        return;
    }

    if (!tcp_peer_matches_segment(frame, ip, source_port))
        return;

    if (tcp_state == TCP_SYN_RECEIVED)
    {
        if ((flags & 0x02) && !(flags & 0x10)
            && sequence + 1 == tcp_receive_next)
        {
            send_tcp_segment(
                tcp_remote_mac,
                tcp_remote_ip,
                TCP_ECHO_PORT,
                tcp_remote_port,
                tcp_send_next - 1,
                tcp_receive_next,
                0x12,
                0,
                0);
            return;
        }
        if (!(flags & 0x10) || acknowledgement != tcp_send_next
            || sequence != tcp_receive_next)
            return;
        tcp_state = TCP_ESTABLISHED;
    }
    else if (tcp_state == TCP_LAST_ACK)
    {
        if ((flags & 0x10) && acknowledgement == tcp_send_next)
            tcp_state = TCP_LISTEN;
        return;
    }

    if (tcp_state != TCP_ESTABLISHED)
        return;

    if (sequence != tcp_receive_next)
    {
        send_tcp_segment(
            tcp_remote_mac,
            tcp_remote_ip,
            TCP_ECHO_PORT,
            tcp_remote_port,
            tcp_send_next,
            tcp_receive_next,
            0x10,
            0,
            0);
        return;
    }

    int peer_fin = (flags & 0x01) != 0;
    if (!payload_length && !peer_fin)
        return;
    tcp_receive_next += payload_length + (peer_fin ? 1U : 0U);
    u8 reply_flags = (u8)(0x10 | (payload_length ? 0x08 : 0)
        | (peer_fin ? 0x01 : 0));
    if (send_tcp_segment(
            tcp_remote_mac,
            tcp_remote_ip,
            TCP_ECHO_PORT,
            tcp_remote_port,
            tcp_send_next,
            tcp_receive_next,
            reply_flags,
            payload,
            payload_length) != 0)
    {
        serial_write("net: TCP echo transmit failed\n");
        return;
    }
    tcp_send_next += payload_length + (peer_fin ? 1U : 0U);
    if (peer_fin)
        tcp_state = TCP_LAST_ACK;
}

static void receive_frame(const u8 *frame, u16 length)
{
    if (length < ETHERNET_HEADER_SIZE)
        return;

    u16 type = read_be16(frame + 12);
    if (type == ARP_ETHERTYPE)
        handle_arp(frame, length);
    else if (type == IPV4_ETHERTYPE)
    {
        const u8 *ip = frame + ETHERNET_HEADER_SIZE;
        if (length >= ETHERNET_HEADER_SIZE + 20)
        {
            if (ip[9] == UDP_PROTOCOL)
                handle_udp(frame, length);
            else if (ip[9] == TCP_PROTOCOL)
                handle_tcp(frame, length);
        }
    }
}

int net_init(void)
{
    network_ready = 0;
    if (rtl8139_init(receive_frame) != 0)
    {
        serial_write("net: RTL8139 unavailable\n");
        return -1;
    }

    const u8 *mac = rtl8139_mac_address();
    if (!mac)
        return -1;
    copy_bytes(local_mac, mac, sizeof(local_mac));
    tcp_state = TCP_LISTEN;
    network_ready = 1;
    serial_write("net: IPv4 10.0.2.15 UDP echo port 5555 TCP echo port 5556\n");
    return 0;
}

void net_poll(void)
{
    if (network_ready)
        rtl8139_poll();
}
