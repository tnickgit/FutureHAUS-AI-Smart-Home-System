# _FutureHAUS AI: Smart Home System_

This is the future haus smart home system project. This repository contains code from the main central server, an AI model with both the learning module and the voice recognition model, and code to flash to esp32 boards.


## How to run the server
When you run the server code, make sure you are on the same network as the microcontroller boards
Make sure you have a working python and pip installer running on your system.
### Running on your own system
1. Open a new terminal window where the server code is located (ex. "/FutureHAUS-AI-Smart-Home-System/server")
   a. this can be done using the command in terminal cd YOUR_FOLDER_LOCATION_PATH + /server (ex. "/FutureHAUS-AI-Smart-Home-System/server")
   b. to verify you are in the right directory you type the command ls. If you see the file names
   ```
   ├── Server
   │   ├── server.py
   │   └── requirements.txt
   └── README.md  
   ```
2. Run "pip install -r requirements.txt" to install all needed libraries
3. Once you got all the libraries needed if you run the code using either python server.py or py server.py

### On The Raspberry PI 
1. Open a new terminal on the raspberry pi
2. type cd futureHAUS/server. This takes you to the source directory where the server code is located 
3. type source venv/source/activate. This activates a virtual enviroment allowing the server to run
4. type python server.py. This starts running the server
5. if any case you want to stop the server to debug or something wrong is happening use ctrl+c. It may take moments for the server code and you will see a lot of messages after the server ccloses, but don't worry that is normal. Unlsess you see major errors. The code is just saying you interrupted the current functions it was doing.

### Running the AI language model
1. to run this model you need to have a microphone connected to your device/raspberry pi
2. type cd futureHAUS/server. This takes you to the source directory where las.py is located 
3. type source venv/source/activate. This activates a virtual enviroment allowing the las.py source could to run
4. type python las.py. This starts up running las.py
5. if you want to cancel las.py run ctrl+c to cancel the function

## Before flashing esp32 boards
Before you flash any of your esp 32 boards make sure you have a working visual studio code opened up to run ESP-IDF-SDK or ESP-IDF. This extension will help run the esp 32 libraries and other functions inside the repository. Once you download the extension follow the steps to ensure you have the correct build and .vscode folder
1. In the top bar in vs code type in run esp-IDF: add vscode configureation folder
2. make sure your paths are correct for: preference open user settings (JSON)
3. then run build

## How to flash the esp32 microcontroller boards with the correct critical system
1. Plug in esp32 boards to computer
2. In the bottom of VS code select the port number that the esp 32 board is connected to (ex. COM5)
3. When selecting a flash method make sure that it is set to UART.
4. Once everything is connected and there is no errors you are ready to build and flash to esp32 boards
5. In line 30 of the main/src/main.c code you will see a line that says #define NODE_TYPE   SENSOR_TYPE_POWER //change to whatever needed. You just change the word after type to either POWER, WATER, LIGHT, MOTION, or HVAC. This is based off whatever sensor the esp32 sensor is connected to.
6. Once set, press build and flash to build the code and flash the esp32 with the critical system model.
   
## How to run the display
1. To run the display, open the IpadDisplay.html file in an ipad code editor (either koder or texttastic)
2. If you are on the Koder app open the file and press the eye icon to start running the code. Two ICONs should appear along with the title FutureHAUS smart home system
3. If you run the code on the textastic app there should be a play button where you can run the code.
4. On your screen you have two buttons one for live display where it will connect to the server.py and get data from the server or simulation mode where it runs simulatiosn.
