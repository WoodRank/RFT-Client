//
// Created by Phillip Romig on 7/16/24.
//
#include <iostream>
#include <fstream>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <array>

#include "timerC.h"
#include "unreliableTransport.h"
#include "logging.h"


#define WINDOW_SIZE 10
int main(int argc, char* argv[]) {

    // Defaults
    uint16_t portNum(12345);
    std::string hostname("");
    std::string inputFilename("");
    int requiredArgumentCount(0);


    int opt;
    try {
        while ((opt = getopt(argc, argv, "f:h:p:d:")) != -1) {
            switch (opt) {
                case 'p':
                    portNum = std::stoi(optarg);
                    break;
                case 'h':
                    hostname = optarg;
		    requiredArgumentCount++;
                    break;
                case 'd':
                    LOG_LEVEL = std::stoi(optarg);
                    break;
                case 'f':
                    inputFilename = optarg;
		    requiredArgumentCount++;
                    break;
                case '?':
                default:
                    std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
                    break;
            }
        }
    } catch (std::exception &e) {
        std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
        FATAL << "Invalid command line arguments: " << e.what() << ENDL;
        return(-1);
    }

    if (requiredArgumentCount != 2) {
        std::cout << "Usage: " << argv[0] << " -f filename -h hostname [-p port] [-d debug_level]" << std::endl;
	std::cerr << "hostname and filename are required." << std::endl;
	return(-1);
    }


    TRACE << "Command line arguments parsed." << ENDL;
    TRACE << "\tServername: " << hostname << ENDL;
    TRACE << "\tPort number: " << portNum << ENDL;
    TRACE << "\tDebug Level: " << LOG_LEVEL << ENDL;
    TRACE << "\tOutput file name: " << inputFilename << ENDL;

    // *********************************
    // * Open the input file
    // *********************************


    std::ifstream inputFile(inputFilename, std::ios::binary);
   if (!inputFile) {
            throw std::runtime_error("Could not open input file: " + inputFilename);
    }


    try {

        // ***************************************************************
        // * Initialize your timer, window and the unreliableTransport etc.
        // **************************************************************
        timerC timer(50); //Set duration to 50 milliseconds
        unreliableTransportC transportInstance{hostname, portNum};

        std::array<datagramS, WINDOW_SIZE> sendPacket;
        int base = 1;
        int nextSeqNum = 1;

        // ***************************************************************
        // * Send the file one datagram at a time until they have all been
        // * acknowledged
        // **************************************************************
        bool allSent(false);
        bool allAcked(false);

        bool finalPacketHasBeenSent(false);

        // Using your original while condition
        while ((!allSent) && (!allAcked)) {
	
		    // Is there space in the window?
            if (nextSeqNum < base + WINDOW_SIZE){
                
                // Only try to read from the file if we haven't sent the final packet yet.
                if (!finalPacketHasBeenSent) {
                    datagramS packet;
                    packet.seqNum = nextSeqNum;

                    inputFile.read(packet.data, MAX_PAYLOAD_LENGTH);
                    packet.payloadLength = inputFile.gcount(); 

                    if (packet.payloadLength > 0){
                        // This is a regular data packet
                        packet.checksum = computeChecksum(packet);
                        sendPacket[nextSeqNum % WINDOW_SIZE] = packet;
                        transportInstance.udt_send(packet);

                        if (base == nextSeqNum){
                            timer.start();
                        }
                        nextSeqNum++;
                    } else {
                        // End of file is reached. Send the final packet.
                        packet.payloadLength = 0; // Set payload to 0
                        packet.checksum = computeChecksum(packet);
                        sendPacket[nextSeqNum % WINDOW_SIZE] = packet;
                        transportInstance.udt_send(packet);

                        if (base == nextSeqNum) {
                            timer.start();
                        }
                        nextSeqNum++;
                        
                        // Set our new internal flag. We DO NOT set allSent here.
                        finalPacketHasBeenSent = true; 
                    }
                }
            }

            // Call udt_recieve() to see if there is an acknowledgment.
            datagramS ackPacket;
            if (transportInstance.udt_receive(ackPacket) > 0){
                DEBUG << "Calling udt_receive if there is an acknowledgment" << ENDL;
                if (validateChecksum(ackPacket)){
                    base = ackPacket.ackNum + 1;
                    if (base == nextSeqNum){
                        timer.stop();
                        
                        // Check if the final packet has been sent. If so, the transfer is complete.
                        if (finalPacketHasBeenSent){
                            DEBUG << "Final packet has been set" <<ENDL;
                            // Now we set both flags, which will cause the loop to exit.
                            allSent = true;
                            allAcked = true;
                        }
                    } else {
                        timer.start();
                    }
                }
            }
        
            // Check to see if the timer has expired.
            DEBUG <<"Checking to see timer expired" << ENDL;
            if (timer.timeout()){
                DEBUG << "Timer Ran out! Sending packet from window again." << ENDL;
                timer.start();

                for (uint16_t i = base; i < nextSeqNum; i++){
                    transportInstance.udt_send(sendPacket[i % WINDOW_SIZE]);
                }
            }
        }

        // cleanup and close the file and network.
        inputFile.close();
        

    } catch (std::exception &e) {
        FATAL<< "Error: " << e.what() << ENDL;
        exit(1);
    }
    return 0;
}
