/*
 * Copyright (c) 2007, 2008, 2009, 2010 Joseph Gaeddert
 * Copyright (c) 2007, 2008, 2009, 2010 Virginia Polytechnic
 *                                      Institute & State University
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <iostream>
#include <complex>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <liquid/liquid.h>

#include <uhd/usrp/single_usrp.hpp>

 
static bool verbose;

static unsigned int num_packets_received;
static unsigned int num_valid_packets_received;

static int callback(unsigned char * _header,  int _header_valid,
                    unsigned char * _payload, int _payload_valid,
                    framesyncstats_s _stats,
                    void * _userdata)
{

    num_packets_received++;
    if (verbose) {
        printf("********* callback invoked, ");
        printf("evm=%5.1fdB, ", _stats.evm);
        printf("rssi=%5.1fdB, ", _stats.rssi);
    }

    if ( !_header_valid ) {
        if (verbose) printf("header crc : FAIL\n");
    } else if ( !_payload_valid ) {
        if (verbose) printf("payload crc : FAIL\n");
    } else {
        if (verbose) {
            unsigned int pid = (_header[0] << 8) | _header[1];
            printf("packet id: %u\n", pid);
        }
        num_valid_packets_received++;
    }
    return 0;
}

void usage() {
    printf("packet_tx:\n");
    printf("  f     :   center frequency [Hz]\n");
    printf("  b     :   bandwidth [Hz]\n");
    printf("  t     :   run time [seconds]\n");
    printf("  q     :   quiet\n");
    printf("  v     :   verbose\n");
    printf("  u,h   :   usage/help\n");
}

int main (int argc, char **argv)
{
    // command-line options
    verbose = true;

    float min_bandwidth = 0.5f*(64e6 / 256.0);
    float max_bandwidth = 0.5f*(64e6 /   4.0);

    float frequency = 462.0e6;
    float bandwidth = min_bandwidth;
    float num_seconds = 5.0f;

    //
    int d;
    while ((d = getopt(argc,argv,"f:b:t:qvuh")) != EOF) {
        switch (d) {
        case 'f':   frequency = atof(optarg);       break;
        case 'b':   bandwidth = atof(optarg);       break;
        case 't':   num_seconds = atof(optarg);     break;
        case 'q':   verbose = false;                break;
        case 'v':   verbose = true;                 break;
        case 'u':
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    if (bandwidth > max_bandwidth) {
        printf("error: maximum bandwidth exceeded (%8.4f MHz)\n", max_bandwidth*1e-6);
        return 0;
    } else if (bandwidth < min_bandwidth) {
        printf("error: minimum bandwidth exceeded (%8.4f kHz)\n", min_bandwidth*1e-3);
        return 0;
    }

    printf("frequency   :   %12.8f [MHz]\n", frequency*1e-6f);
    printf("bandwidth   :   %12.8f [kHz]\n", bandwidth*1e-3f);
    printf("verbosity   :   %s\n", (verbose?"enabled":"disabled"));

    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

    stream_cmd.stream_now = true;

    uhd::device_addr_t dev_addr;
    //dev_addr["addr0"] = "192.168.10.2";
    //dev_addr["addr1"] = "192.168.10.3";
    uhd::usrp::single_usrp::sptr usrp = uhd::usrp::single_usrp::make(dev_addr);

    // set properties
#if 0
    usrp->set_rx_rate(2.0f*bandwidth);
#else
    float rx_rate = 2.0f*bandwidth;
    unsigned int decim_rate = (unsigned int)(32e6f / rx_rate);
    // ensure multiple of 2
    decim_rate = (decim_rate >> 1) << 1;
    // compute usrp sampling rate
    float usrp_rx_rate = 32e6f / (float)decim_rate;
    // compute arbitrary resampling rate
    float rx_resamp_rate = rx_rate / usrp_rx_rate;
    printf("sample rate : %12.8f kHz = %12.8f * %8.6f (decim %u)\n",
            rx_rate * 1e-3f,
            usrp_rx_rate * 1e-3f,
            rx_resamp_rate,
            decim_rate);
    usrp->set_rx_rate(usrp_rx_rate);
#endif
    usrp->set_rx_freq(frequency);
    usrp->set_rx_gain(10);

    // TODO : add arbitrary resampling component

    const size_t max_samps_per_packet = usrp->get_device()->get_max_recv_samps_per_packet();
    unsigned int num_blocks = (unsigned int)((2.0f*bandwidth*num_seconds)/(max_samps_per_packet));


    //allocate recv buffer and metatdata
    uhd::rx_metadata_t md;
    std::vector<std::complex<float> > buff(max_samps_per_packet);

    // framing
    framesyncprops_s props;
    framesyncprops_init_default(&props);
    props.agc_gmin          = 1e-3f;
    props.agc_gmax          = 1e5f;
    props.squelch_threshold = -40.0f;
    framesync64 framesync = framesync64_create(&props,callback,NULL);

    // start data transfer
    usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    printf("usrp data transfer started\n");
 
    std::complex<float> data_rx[max_samps_per_packet];

    // reset counter
    num_packets_received = 0;
    num_valid_packets_received = 0;

    unsigned int i;
    for (i=0; i<num_blocks; i++) {
        // grab data from port
        size_t num_rx_samps = usrp->get_device()->recv(
            &buff.front(), buff.size(), md,
            uhd::io_type_t::COMPLEX_FLOAT32,
            uhd::device::RECV_MODE_ONE_PACKET
        );

        //handle the error codes
        switch(md.error_code){
        case uhd::rx_metadata_t::ERROR_CODE_NONE:
        case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
            break;

        default:
            std::cerr << "Error code: " << md.error_code << std::endl;
            std::cerr << "Unexpected error on recv, exit test..." << std::endl;
            return 1;
        }

        if (not md.has_time_spec){
            std::cerr << "Metadata missing time spec, exit test..." << std::endl;
            return 1;
        }

        // for now copy vector "buff" to array of complex float
        // TODO : apply bandwidth-dependent gain
        unsigned int j;
        for (j=0; j<num_rx_samps; j++)
            data_rx[j] = buff[j];

        // run through frame synchronizer
        framesync64_execute(framesync, data_rx, num_rx_samps);
    }
 
    // stop data transfer
    usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);
    printf("\n");
    printf("usrp data transfer complete\n");

    // print results
    float data_rate = num_valid_packets_received * 8.0f * 64.0f / num_seconds;
    float percent_valid = (num_packets_received == 0) ?
                          0.0f :
                          100.0f * (float)num_valid_packets_received / (float)num_packets_received;
    printf("    packets received    : %6u\n", num_packets_received);
    printf("    valid packets       : %6u (%6.2f%%)\n", num_valid_packets_received,percent_valid);
    printf("    data rate           : %12.8f kbps\n", data_rate*1e-3f);

    // clean it up
    framesync64_destroy(framesync);

    std::cout << std::endl << std::endl;
    return 0;
}

