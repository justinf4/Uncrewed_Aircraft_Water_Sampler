/*****************************************************************************
* | File      	:	DEV_Config.cpp
* | Author      : Waveshare team
* | Function    :
* | Info        :
*   Image scanning
*      Please use progressive scanning to generate images or fonts
*----------------
* |	This version:   V1.0
* | Date        :   2018-01-11
* | Info        :   Basic version
*
******************************************************************************/
#include "DEV_Config.h"


/******************************************************************************
function:	Initialize Arduino, Initialize Pins, and SPI
parameter:
Info:
******************************************************************************/
UBYTE DEV_Module_Init(void)
{
    //set pin
    pinMode(SPIRAM_CS, OUTPUT);
    pinMode(SD_CS, OUTPUT);
    
    pinMode(EPD_CS, OUTPUT);
    pinMode(EPD_DC, OUTPUT);
    pinMode(EPD_RST, OUTPUT);
    pinMode(EPD_BUSY, INPUT);

    EPD_CS_1;
    SD_CS_1;
    SPIRAM_CS_1;
    
    //set Serial
    Serial.begin(115200);
      
    // CrowPanel PCB: SCK=12, MOSI=11, MISO=-1, CS unmanaged (driver toggles manually)
    SPI.begin(12, -1, 11, -1);
    SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

    return 0;
}

/******************************************************************************
function:	Millisecond delay
parameter:
Info:
******************************************************************************/
void DEV_Delay_ms(UWORD xms)
{
    delay(xms);
}

/******************************************************************************
function:	Analog microsecond delay
parameter:
Info:
******************************************************************************/
void DEV_Delay_us(UWORD xus)
{
    for(int j=xus; j > 0; j--);
}

void DEV_Module_Exit(void)
{
    EPD_CS_1;
    SD_CS_1;
    SPIRAM_CS_1;
    EPD_RST_0;
}
