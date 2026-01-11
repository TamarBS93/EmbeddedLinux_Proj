This parking project is divided into three main parts: 
a TCP Server capable of handling multiple clients and processing
parking data, a Database (DB) for storing pricing information and customer
data, and a system integrating STM32 microcontroller and BeagleBone Green
(BBG) for emulating GPS coordinates and transmitting them to the server.

In this Repository:

* project_server folder- contains the TCP server running on PC
  
  -Server Architecture:
    The server operates asynchronously to handle multiple client connections
    simultaneously, processing start and end messages to calculate parking
    durations.
  -Data Handling:
    Upon receiving a connection, the server will parse the incoming data for GPS
    coordinates and identifiers, distinguishing between start and end messages
    to compute the elapsed time.
  -Pricing Calculation:
    The server will access a separate pricing file, loading city-specific rates into
    shared memory for quick retrieval. After calculating the parking duration, it
    will determine the total fee based on these rates.
    Shared Memory Integration:
      Uses shared memory to exchange pricing information between the server and
      the DB, ensuring that price updates are immediately accessible.

* BBG folder- contains the BBG code for communication
  
  The BBG will host two processes:
    Ethernet Communication: Handles data transmission to the TCP server.
    I2C Communication: Receives data from the STM32 and passes it to the Ethernet communication process via IPC PIPE.

* STM32_GPS_Generator folder- contains the scripts for the STM32 GPS emulator
  
  The STM32 periodically sends GPS coordinates along with unique
  identifiers and start/end messages to the BBG using the I2C protocol.
