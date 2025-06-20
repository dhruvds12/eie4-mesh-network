# eie4-mesh-network

Please see [Wiki](https://github.com/dhruvds12/eie4-mesh-network/wiki) for further details and videos.

This repository contains the code for the **Mesh Network for Natural Disaster Recovery** project. 

The project aims to create a resilient and efficient mesh network that can be deployed in disaster-stricken areas to facilitate communication and data transfer.
The mesh network is designed to be self-healing, allowing nodes to dynamically connect and disconnect as needed. This ensures that the network remains operational even in the face of node failures or environmental changes.


## Features 
- **Self-Healing Mesh Network**: Nodes can dynamically connect and disconnect, ensuring continuous operation.
- **Data Transfer**: Nodes can transfer data to each other, enabling communication in disaster-stricken areas.
- **Cryptography**: The project implements cryptographic techniques to secure data transfer between nodes.
- **User-to-User Communication**: Users can send messages directly to each other through the mesh network as well as node to node and broadcast messages.
- **Message store and forward**: Messages are stored and forwarded to ensure delivery even if the recipient is offline.

## Installation
1. Clone the repository:
   ```bash
   git clone https://github.com/dhruvds12/eie4-mesh-network.git
   ```
2. The project uses platformio for building and uploading code to the devices. 
   Install platformio by following the instructions [here](https://docs.platformio.org/en/latest/core/installation.html). vscode users can install the platformio extension from the marketplace.
3. Open the project in platformio and select the appropriate board for your device. This project only supports the Heltec WiFi LoRa 32 v3 board. However, interface are abstracted so that you can easily port the code to other boards.
4. Plug in your device and select the correct port, platofrmio will automatically detect the port. 
**Note**: Do NOT plug in the device without an antenna, as it can damage the LoRa module.
5. Build and upload the code to your device using platformio.
