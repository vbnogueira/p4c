#include <core.p4>
#include <tc/pna.p4>

/******  G L O B A L   I N G R E S S   M E T A D A T A  *********/

struct my_ingress_metadata_t {
}

struct empty_metadata_t {
}

/* -*- P4_16 -*- */

/*
 * CONST VALUES FOR TYPES
 */
const bit<8> IP_PROTO_TCP = 0x06;
const bit<16> ETHERTYPE_IPV4 = 0x0800;

/*
 * Standard ethernet header
 */
header ethernet_t {
    bit<48> dstAddr;
    bit<48> srcAddr;
    bit<16> etherType;
}

header ipv4_t {
    bit<4>  version;
    bit<4>  ihl;
    bit<8>  diffserv;
    bit<16> totalLen;
    bit<16> identification;
    bit<3>  flags;
    bit<13> fragOffset;
    bit<8>  ttl;
    bit<8>  protocol;
    bit<16> hdrChecksum;
    bit<32> srcAddr;
    bit<32> dstAddr;
}

struct my_ingress_headers_t {
    ethernet_t   ethernet;
    ipv4_t       ipv4;
}

    /***********************  P A R S E R  **************************/
parser Ingress_Parser(
        packet_in pkt,
        out   my_ingress_headers_t  hdr,
        inout my_ingress_metadata_t meta,
        in    pna_main_parser_input_metadata_t istd)
{

    state start {
        transition parse_ethernet;
    }

    state parse_ethernet {
        pkt.extract(hdr.ethernet);
        transition select(hdr.ethernet.etherType) {
            ETHERTYPE_IPV4: parse_ipv4;
            default: reject;
        }
    }
    state parse_ipv4 {
        pkt.extract(hdr.ipv4);
        transition select(hdr.ipv4.protocol) {
            IP_PROTO_TCP : accept;
            default: reject;
        }
    }
}

#define L3_TABLE_SIZE 2048

/***************** M A T C H - A C T I O N  *********************/

control ingress(
    inout my_ingress_headers_t  hdr,
    inout my_ingress_metadata_t meta,
    in    pna_main_input_metadata_t  istd,
    inout pna_main_output_metadata_t ostd,
          tc_skb_metadata sm
)
{
   action send_nh(@tc_type("dev") PortId_t port, @tc_type("macaddr") bit<48> srcMac, @tc_type("macaddr") bit<48> dstMac) {
        bit<32> mark;

	sm.get();
        mark = sm.get_mark();
        sm.set_mark(mark + 1);
	sm.set_tc_classid(sm.get_tc_classid() + 1);
	sm.set_tc_index(sm.get_tc_index() + 1);
	hdr.ethernet.srcAddr = srcMac;
        hdr.ethernet.dstAddr = dstMac;
        send_to_port(port);
   }

   action drop() {
        drop_packet();
   }

    table nh_table {
        key = {
            hdr.ipv4.dstAddr : exact @tc_type("ipv4") @name("dstAddr");
        }
        actions = {
            send_nh;
            drop;
        }
        size = L3_TABLE_SIZE;
        const default_action = drop;
    }

    apply {
	/*XXX: Why are we checking for TCP? Parser will reject if it is not TCP*/
        if (hdr.ipv4.isValid() && hdr.ipv4.protocol == IP_PROTO_TCP) {
            bit<16> tc_index;
            bit<16> classid;
            bit<64> tstamp;
            bit<32> mark;
            bit<16> qmap;

            nh_table.apply();
            mark = sm.get_mark();
            tstamp = sm.get_tstamp();
            classid = sm.get_tc_classid();
            tc_index = sm.get_tc_index();
            qmap = sm.get_queue_mapping();
            sm.set();
        }
    }
}

    /*********************  D E P A R S E R  ************************/

control Ingress_Deparser(
    packet_out pkt,
    inout    my_ingress_headers_t hdr,
    in    my_ingress_metadata_t meta,
    in    pna_main_output_metadata_t ostd,
    tc_skb_metadata sm)
{
    apply {
        pkt.emit(hdr.ethernet);
        pkt.emit(hdr.ipv4);
    }
}

/************ F I N A L   P A C K A G E ******************************/

PNA_NIC(
    Ingress_Parser(),
    ingress(),
    Ingress_Deparser()
) main;
